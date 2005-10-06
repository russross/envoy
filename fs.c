#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/stat.h>
#include <dirent.h>
#include <utime.h>
#include <string.h>
#include <errno.h>
#include <assert.h>
#include <gc/gc.h>
#include "9p.h"
#include "config.h"
#include "state.h"
#include "dispatch.h"
#include "map.h"
#include "list.h"
#include "util.h"
#include "fs.h"

#define ERROR_BUFFER_LEN 80

/* generate a qid record from a file stat record */
static inline struct qid stat2qid(struct stat *info) {
    struct qid qid;

    qid.type =
        S_ISDIR(info->st_mode) ? QTDIR :
        S_ISLNK(info->st_mode) ? QTSLINK :
        0x00;
    qid.version =
        info->st_mtime ^ (info->st_size << 8);
    qid.path =
        (u64) info->st_dev << 32 |
        (u64) info->st_ino;

    return qid;
}

/* convert a u32 to a string, allocating the necessary storage */
static inline char *u32tostr(u32 n) {
    char *res = GC_MALLOC_ATOMIC(11);
    assert(res != NULL);

    sprintf(res, "%u", n);
    return res;
}

/* convert a unix stat record to a p9stat record */
static int stat2p9stat(struct stat *info, struct p9stat **p9info, char *path) {
    struct p9stat *res = GC_NEW(struct p9stat);
    assert(res != NULL);

    res->type = 0;
    res->dev = info->st_dev;
    res->qid = stat2qid(info);
    
    res->mode = info->st_mode & 0777;
    if (S_ISDIR(info->st_mode))
        res->mode |= DMDIR;
    if (S_ISLNK(info->st_mode))
        res->mode |= DMSYMLINK;
    if (S_ISSOCK(info->st_mode))
        res->mode |= DMSOCKET;
    if (S_ISFIFO(info->st_mode))
        res->mode |= DMNAMEDPIPE;
    if (S_ISBLK(info->st_mode))
        res->mode |= DMDEVICE;
    if (S_ISCHR(info->st_mode))
        res->mode |= DMDEVICE;
    if ((S_ISUID & info->st_mode))
        res->mode |= DMSETUID;
    if ((S_ISGID & info->st_mode))
        res->mode |= DMSETGID;
    
    res->atime = info->st_atime;
    res->mtime = info->st_mtime;
    res->length = info->st_size;
    res->name = filename(path);
    res->uid = res->muid = u32tostr(info->st_uid);
    res->gid = u32tostr(info->st_gid);
    res->n_uid = res->n_muid = info->st_uid;
    res->n_gid = info->st_gid;

    res->extension = NULL;
    if ((res->mode & DMSYMLINK)) {
        int len = (int) res->length;
        res->extension = GC_MALLOC_ATOMIC(len + 1);
        assert(res->extension != NULL);
        if (readlink(path, res->extension, len) != len)
            return -1;
        else
            res->extension[len] = 0;
    } else if ((res->mode & DMDEVICE)) {
        /* long enough for any 2 ints plus 4 */
        res->extension = GC_MALLOC_ATOMIC(24);
        assert(res->extension != NULL);
        sprintf(res->extension, "%c %u %u",
                S_ISCHR(info->st_rdev) ? 'c' : 'b',
                (unsigned) major(info->st_rdev),
                (unsigned) minor(info->st_rdev));
    }

    *p9info = res;

    return 0;
}

/* get the unix flags (for open or create) from a 9P mode */
static inline int unixflags(u32 mode) {
    int flags;
    switch (mode & OMASK) {
        case OREAD:
        case OEXEC:
            flags = O_RDONLY;
            if ((mode & OTRUNC))
                return -1;
            break;
        case OWRITE:
            flags = O_WRONLY;
            break;
        case ORDWR:
            flags = O_RDWR;
            break;
        default:
            assert(0);
    }
    if ((mode & OTRUNC))
        flags |= O_TRUNC;
    /*flags |= O_LARGEFILE;*/

    return flags;
}

/*****************************************************************************/

/* generate an error response with Unix errno errnum */
static void rerror(struct message *m, u16 errnum, int line) {
    char buf[ERROR_BUFFER_LEN];

    m->id = RERROR;
    m->msg.rerror.errnum = errnum;
    m->msg.rerror.ename =
        stringcopy(strerror_r(errnum, buf, ERROR_BUFFER_LEN - 1));
    fprintf(stderr, "error #%u: %s (%s line %d)\n",
            (u32) errnum, m->msg.rerror.ename, __FILE__, line);
}

#define failif(p,e) do { \
    if (p) { \
        rerror(trans->out, e, __LINE__); \
        send_reply(trans); \
        return; \
    } \
} while(0)

#define guard(f) do { \
    if ((f) < 0) { \
        rerror(trans->out, errno, __LINE__); \
        send_reply(trans); \
        return; \
    } \
} while(0)

#define require_alt_fid(ptr,fid) do { \
    (ptr) = fid_lookup(trans->conn, fid); \
    if ((ptr) == NULL) { \
        rerror(trans->out, EBADF, __LINE__); \
        send_reply(trans); \
        return; \
    } \
} while(0)

#define require_fid(ptr) do { \
    (ptr) = fid_lookup(trans->conn, req->fid); \
    if ((ptr) == NULL) { \
        rerror(trans->out, EBADF, __LINE__); \
        send_reply(trans); \
        return; \
    } \
} while(0)

#define require_fid_remove(ptr) do { \
    (ptr) = fid_lookup_remove(trans->conn, req->fid); \
    if ((ptr) == NULL) { \
        rerror(trans->out, EBADF, __LINE__); \
        send_reply(trans); \
        return; \
    } \
} while(0)

#define require_fid_closed(ptr) do { \
    (ptr) = fid_lookup(trans->conn, req->fid); \
    if ((ptr) == NULL) { \
        rerror(trans->out, EBADF, __LINE__); \
        send_reply(trans); \
        return; \
    } else if ((ptr)->status != STATUS_CLOSED) { \
        rerror(trans->out, ETXTBSY, __LINE__); \
        send_reply(trans); \
        return; \
    } \
} while(0)

struct sockaddr_in *get_envoy_address(char *path) {
    struct cons *lst = map_lookup(state->map, splitpath(path));
    struct map *elt;

    if (null(lst))
        return NULL;
    while (!null(cdr(lst)))
        lst = cdr(lst);
    elt = car(lst);
    return elt->addr;
}

/*****************************************************************************/

/**
 * client_tversion: initial handshake
 *
 * Arguments:
 * - msize[4]: suggested maximum message size
 * - version[s]: protocol version proposed by the client
 *
 * Return:
 * - msize[4]: maximum message size
 * - version[s]: protocol version to be used on this connection
 *
 * Semantics:
 * - must be first message sent on a 9P connection
 * - client cannot issue any further requests until Rversion received
 * - tag must be NOTAG
 * - msize includes all protocol overhead
 * - response msize must be <= proposed msize
 * - version string must start with "9P"
 * - if client version is not understood, respond with version = "unknown"
 * - all outstanding i/o on the connection is aborted
 * - all existing fids are clunked
 */
void unknown_tversion(struct transaction *trans) {
    struct Tversion *req = &trans->in->msg.tversion;
    struct Rversion *res = &trans->out->msg.rversion;

    failif(trans->in->tag != NOTAG, ECONNREFUSED);

    res->msize = max(min(GLOBAL_MAX_SIZE, req->msize), GLOBAL_MIN_SIZE);
    trans->conn->maxSize = trans->out->maxSize = res->msize;

    if (!strcmp(req->version, "9P2000.u")) {
        trans->conn->type = CONN_CLIENT_IN;
        res->version = req->version;
    } else if (!strcmp(req->version, "9P2000.envoy")) {
        trans->conn->type = CONN_ENVOY_IN;
        res->version = req->version;
    } else {
        res->version = "unknown";
    }

    send_reply(trans);
}

/*****************************************************************************/

/**
 * client_tauth: check credentials to authorize a connection
 *
 * Arguments:
 * - afid[4]: fid to use for authorization channel
 * - uname[s]: username associated with this connection
 * - aname[s]: file tree to access
 *
 * Return:
 * - aqid[13]: qid associated with afid
 *
 * Semantics:
 * - if authorization is not required, return Rerror
 * - aqid identifies a file of type QTAUTH
 * - afid is used to exchange (undefined) data to authorize connection
 */
void client_tauth(struct transaction *trans) {
    failif(-1, ENOTSUP);
}

void envoy_tauth(struct transaction *trans) {
    failif(-1, ENOTSUP);
}

/**
 * client_tattach: establish a connection
 *
 * Arguments:
 * - fid[4]: proposed fid for the connection
 * - afid[4]: authorization fid
 * - uname[s]: username associated with this connection
 * - aname[s]: file tree to access
 *
 * Return:
 * - qid[13]: qid associated with fid
 *
 * Semantics:
 * - afid must be properly initialized through auth and read/writes
 * - afid must be NOFID if authorization is not required
 * - uname and aname must be the same as in corresponding auth sequence
 * - afid may be used for multiple attach calls with same uname/aname
 * - error returned if fid is already in use
 */

/* return the results of a request that has been handled by a remote envoy */
void copy_envoy_response(struct transaction *trans) {
    struct message *from;

    assert(trans != NULL);
    assert(!null(trans->children));

    /* walk is a special case */
    if (trans->out->id == RWALK)
        return;
    
    from = ((struct transaction *) car(trans->children))->in;
    assert(from != NULL);

    trans->children = cdr(trans->children);
    assert(null(trans->children));

    /* copy the whole message over */
    memcpy(&trans->out->msg, &from->msg, sizeof(from->msg));
    trans->out->id = from->id;

    send_reply(trans);
}

u32 get_fid(struct message *msg) {
    switch (msg->id) {
        case TATTACH:   return msg->msg.tattach.fid;
        case TWALK:     return msg->msg.twalk.fid;
        case TOPEN:     return msg->msg.topen.fid;
        case TCREATE:   return msg->msg.tcreate.fid;
        case TREAD:     return msg->msg.tread.fid;
        case TWRITE:    return msg->msg.twrite.fid;
        case TCLUNK:    return msg->msg.tclunk.fid;
        case TREMOVE:   return msg->msg.tremove.fid;
        case TSTAT:     return msg->msg.tstat.fid;
        case TWSTAT:    return msg->msg.twstat.fid;
        default:        return NOFID;
    }
}

void set_fid(struct message *msg, u32 fid) {
    switch (msg->id) {
        case TATTACH:   msg->msg.tattach.fid = fid;
                        break;
        case TWALK:     msg->msg.twalk.fid = fid;
                        break;
        case TOPEN:     msg->msg.topen.fid = fid;
                        break;
        case TCREATE:   msg->msg.tcreate.fid = fid;
                        break;
        case TREAD:     msg->msg.tread.fid = fid;
                        break;
        case TWRITE:    msg->msg.twrite.fid = fid;
                        break;
        case TCLUNK:    msg->msg.tclunk.fid = fid;
                        break;
        case TREMOVE:   msg->msg.tremove.fid = fid;
                        break;
        case TSTAT:     msg->msg.tstat.fid = fid;
                        break;
        case TWSTAT:    msg->msg.twstat.fid = fid;
                        break;
        default:        break;
    }
}

int try_forwarding(struct transaction *trans) {
    u32 fid;
    struct forward *forward;

    switch (trans->in->id) {
        case TOPEN:     fid = trans->in->msg.topen.fid;
                        break;
        case TCREATE:   fid = trans->in->msg.tcreate.fid;
                        break;
        case TREAD:     fid = trans->in->msg.tread.fid;
                        break;
        case TWRITE:    fid = trans->in->msg.twrite.fid;
                        break;
        case TCLUNK:    fid = trans->in->msg.tclunk.fid;
                        break;
        case TREMOVE:   fid = trans->in->msg.tremove.fid;
                        break;
        case TSTAT:     fid = trans->in->msg.tstat.fid;
                        break;
        case TWSTAT:    fid = trans->in->msg.twstat.fid;
                        break;
        default:        return 0;
    }

    forward = forward_lookup(trans->conn, fid);
    if (forward == NULL) {}
    return 0;

}

void handle_tattach(struct transaction *trans, int client) {
    struct Tattach *req = &trans->in->msg.tattach;
    struct Rattach *res = &trans->out->msg.rattach;
    struct sockaddr_in *addr;
    
    failif(req->afid != NOFID, EBADF);
    failif(emptystring(req->uname), EINVAL);

    if (!null(trans->children)) {
        /* we have a result back from an envoy */
        copy_envoy_response(trans);
    } else if ((addr = get_envoy_address(req->aname)) == NULL ||
            !addr_cmp(addr, trans->conn->addr))
    {
        /* this request should be handled locally */
        struct stat info;
        char *path;

        if (emptystring(req->aname)) {
            failif((path = resolvePath(rootdir, "", &info)) == NULL, ENOENT);
        } else {
            failif((path = resolvePath(rootdir, req->aname, &info)) == NULL,
                    ENOENT);
        }
        failif(fid_insert_new(trans->conn, req->fid, req->uname, path) < 0,
                EBADF);
        res->qid = stat2qid(&info);

        send_reply(trans);
    } else if (client) {
        /* this request needs to be forwarded */
        struct transaction *rtrans = transaction_new();
        rtrans->conn = conn_lookup_addr(addr);
        if (rtrans->conn == NULL)
            rtrans->conn = conn_new_unopened(CONN_ENVOY_OUT, addr);
        rtrans->handler = NULL;
        rtrans->out = trans->out;
        trans->out = NULL;
        rtrans->out->id = TATTACH;
        rtrans->out->tag = conn_alloc_tag(rtrans->conn, rtrans);
        rtrans->in = NULL;
        rtrans->children = NULL;
        rtrans->parent = trans;
        trans->children = cons(rtrans, trans->children);

        send_request(rtrans);
    } else {
        /* this request was sent to the wrong place */
        assert(0);
    }
}

void client_tattach(struct transaction *trans) {
    handle_tattach(trans, 1);
}

void envoy_tattach(struct transaction *trans) {
    handle_tattach(trans, 0);
}

/**
 * client_tflush: abort a message
 *
 * Arguments:
 * - oldtag[2]: tag of transaction to abort
 *
 * Return:
 *
 * Semantics:
 * - server should respond to Tflush immediately
 * - if oldtag is recognized, abort any pending reponse and discard that tag
 * - always respond with Rflush, never Rerror
 * - multiple Tflushes for a single tag must be answered in order
 * - Rflush for any of multiple Tflushes answers all previous ones
 * - client cannot reuse oldtag until Rflush is received
 * - client must honor regular response received before Rflush, including
 *   server-side state change
 */
void client_tflush(struct transaction *trans) {
    send_reply(trans);
}

void envoy_tflush(struct transaction *trans) {
    failif(-1, ENOTSUP);
}

/**
 * client_twalk: descend a directory hierarchy
 *
 * Arguments:
 * - fid[4]: fid of starting point
 * - newfid[4]: fid to be set to endpoint of walk
 * - nwname[2]: number of elements to walk
 * - nwname * wname[s]: elements to walk
 *
 * Return:
 * - nwqid[2]: number of elements walked
 * - nwqid * qid[13]: elements walked
 *
 * Semantics:
 * - newfid must not be in use unless it is the same as fid
 * - fid must represent a directory unless nwname is zero
 * - fid must be valid and not opened by open or create
 * - if full sequence of nwname elements is walked successfully, newfid will
 *   represent the file that results, else newfid and fid are unaffected
 * - if newfid is in use or otherwise illegal, Rerror will be returned
 * - ".." represents parent directory; "." (as current directory) is not used
 * - nwname can be zero; newfid will represent same file as fid
 * - if newfid == fid then change in newfid happends iff change in fid
 * - for nwname > 0:
 *   - elements are walked in-order, elementwise
 *   - fid must be a directory, user must have permission to search directory
 *   - same restrictions apply to implicit fids created along the walk
 *   - if first element cannot be walked, Rerror is returned
 *   - else normal return with nwqid elements for the successful elements
 *     of the walk: nwqid < nwname implies a failure at index nwqid
 *   - nwqid cannot be zero unless nwname is zero
 *   - newfid only affected if nwqid == nwname (success)
 *   - walk of ".." in root directory is equivalent to walk of no elements
 *   - maximum of MAXWELEM (16) elements in a single Twalk request
 */
void client_twalk(struct transaction *trans) {
    struct Twalk *req = &trans->in->msg.twalk;
    struct Rwalk *res = &trans->out->msg.rwalk;
    struct fid *fid;
    struct stat info;
    char *newpath;
    int i;

    failif(req->nwname > MAXWELEM, EMSGSIZE);
    failif(req->newfid != req->fid && fid_lookup(trans->conn, req->newfid),
            EBADF);
    require_fid_closed(fid);

    /* if no path elements were provided, just clone this fid */
    if (req->nwname == 0) {
        if (req->fid != req->newfid)
            fid_insert_new(trans->conn, req->newfid, fid->uname, fid->path);

        res->nwqid = 0;
        res->wqid = NULL;

        send_reply(trans);
        return;
    }

    guard(lstat(fid->path, &info));
    failif(!S_ISDIR(info.st_mode), ENOTDIR);

    res->nwqid = 0;
    res->wqid = GC_MALLOC_ATOMIC(sizeof(struct qid) * req->nwname);
    assert(res->wqid != NULL);

    newpath = fid->path;
    for (i = 0; i < req->nwname; i++) {
        if (!req->wname[i] || !*req->wname[i] || strchr(req->wname[i], '/')) {
            if (i == 0)
                failif(-1, EINVAL);
            send_reply(trans);
            return;
        }

        /* build the revised name, watching for . and .. */
        if (!strcmp(req->wname[i], "."))
            /* do nothing */;
        else if (!strcmp(req->wname[i], ".."))
            newpath = dirname(newpath);
        else
            newpath = concatname(newpath, req->wname[i]);

        if (lstat(newpath, &info) < 0) {
            if (i == 0)
                guard(-1);
            send_reply(trans);
            return;
        }
        if (i < req->nwname - 1 && !S_ISDIR(info.st_mode)) {
            if (i == 0)
                failif(-1, ENOTDIR);
            send_reply(trans);
            return;
        }
        res->wqid[i] = stat2qid(&info);
        res->nwqid = i + 1;
    }

    if (req->fid == req->newfid)
        fid->path = newpath;
    else
        fid_insert_new(trans->conn, req->newfid, fid->uname, newpath);

    send_reply(trans);
}

void envoy_twalk(struct transaction *trans) {
    failif(-1, ENOTSUP);
}

/**
 * client_topen: prepare a fid for i/o on an existing file
 *
 * Arguments:
 * - fid[4]: fid of file to open
 * - mode[1]: file mode requested
 *
 * Return:
 * - qid[13]: qid representing newly opened file
 * - iounit[4]: maximum number of bytes returnable in single 9P message
 *
 * Semantics:
 * - modes OREAD, OWRITE, ORDWR, OEXEC open file for read-only, write-only,
 *   read and write, and execute respectively, to be checked against
 *   permissions of file
 * - if OTRUNC bit is set, file is to be truncated, requiring write permission
 * - if file is append-only and permission granted for open, OTRUNC is ignored
 * - if mode has ORCLOSE bit set, file is set to be removed on clunk, which
 *   requires permission to remove file from the directory
 * - all other bits in mode must be zero
 * - if file is marked for exclusive use, only one client can have it open
 * - it is illegal to write to, truncate, or remove-on-close a directory
 * - all permissions checked at time of open; subsequent changes to file
 *   permissions do not affect already-open file
 * - fid cannot refer to already-opened or created file
 * - iounit may be zero
 * - if iounit is nonzero, it is max number of bytes guaranteed to be read from
 *   or written to a file without breaking the i/o transfer into multiple 9P
 *   messages
 */
void client_topen(struct transaction *trans) {
    struct Topen *req = &trans->in->msg.topen;
    struct Ropen *res = &trans->out->msg.ropen;
    struct fid *fid;
    struct stat info;

    require_fid_closed(fid);
    guard(lstat(fid->path, &info));

    fid->omode = req->mode;
    fid->offset = 0;
    fid->next_dir_entry = NULL;

    if (S_ISDIR(info.st_mode)) {
        failif(req->mode != OREAD, EPERM);

        failif((fid->dd = opendir(fid->path)) == NULL, errno);

        fid->status = STATUS_OPEN_DIR;
    } else {
        int flags;
        failif((flags = unixflags(req->mode)) == -1, EINVAL);
        
        guard(fid->fd = open(fid->path, flags));

        fid->status = STATUS_OPEN_FILE;
    }

    res->iounit = trans->conn->maxSize - RREAD_HEADER;
    res->qid = stat2qid(&info);

    send_reply(trans);
}

void envoy_topen(struct transaction *trans) {
    failif(-1, ENOTSUP);
}

/**
 * client_tcreate: prepare a fid for i/o on a new file
 *
 * Arguments:
 * - fid[4]: fid of directory in which file is to be created
 * - name[s]: name of new file
 * - perm[4]: permissions for new file
 * - mode[1]: file mode for new file
 *
 * Return:
 * - qid[13]: qid representing newly created file
 * - iounit[4]: maximum number of bytes returnable in single 9P message
 *
 * Semantics:
 * - requires write permission in directory represented by fid
 * - owner of new file is implied user of the request
 * - group of new file is same as directory represented by fid
 * - permissions of new regular file are: perm & (~0666 | (dir.perm & 0666))
 * - permissions of new directory are:    perm & (~0777 | (dir.perm & 0777))
 * - newly opened file is opened according to mode, which is NOT checked
 *   against perm
 * - returned qid is for the new file, and fid is updated to be the new file
 * - directories are created by setting DMDIR in perm
 * - names "." and ".." are special; it is illegal to create these names
 * - attempt to create an existing file will be rejected
 * - all other file open modes and restrictions match Topen; same for iounit
 */
void client_tcreate(struct transaction *trans) {
    struct Tcreate *req = &trans->in->msg.tcreate;
    struct Rcreate *res = &trans->out->msg.rcreate;
    struct fid *fid;
    struct stat dirinfo;
    struct stat info;
    char *newpath;

    require_fid_closed(fid);
    failif(!strcmp(req->name, ".") || !strcmp(req->name, "..") ||
            strchr(req->name, '/'), EINVAL);
    guard(lstat(fid->path, &dirinfo));
    failif(!S_ISDIR(dirinfo.st_mode), ENOTDIR);
    newpath = concatname(fid->path, req->name);

    if ((req->perm & DMDIR)) {
        u32 perm;
        failif(req->mode != OREAD, EINVAL);
        perm = req->perm & (~0777 | (dirinfo.st_mode & 0777));

        guard(mkdir(newpath, perm));
        failif((fid->dd = opendir(newpath)) == NULL, errno);

        fid->status = STATUS_OPEN_DIR;
        guard(lstat(fid->path, &info));
        res->qid = stat2qid(&info);
    } else if ((req->perm & DMSYMLINK)) {
        failif(lstat(newpath, &info) >= 0, EEXIST);
        fid->status = STATUS_OPEN_SYMLINK;
        res->qid.type = QTSLINK;
        res->qid.version = ~0;
        res->qid.path = ~0;
    } else if ((req->perm & DMLINK)) {
        failif(lstat(newpath, &info) >= 0, EEXIST);
        fid->status = STATUS_OPEN_LINK;
        res->qid.type = QTLINK;
        res->qid.version = ~0;
        res->qid.path = ~0;
    } else if ((req->perm & DMDEVICE)) {
        failif(lstat(newpath, &info) >= 0, EEXIST);
        fid->status = STATUS_OPEN_DEVICE;

        /* use QTTMP since there's no QTDEVICE */
        res->qid.type = QTTMP;
        res->qid.version = ~0;
        res->qid.path = ~0;
    } else if (!(req->perm & DMMASK)) {
        u32 perm = req->perm & (~0666 | (dirinfo.st_mode & 0666));
        int flags = unixflags(req->mode);
        failif(flags == -1 || (req->mode & OTRUNC), EINVAL);
        flags |= O_EXCL | O_CREAT /*| O_LARGEFILE*/;

        guard(fid->fd = open(newpath, flags, perm));

        fid->status = STATUS_OPEN_FILE;
        guard(lstat(fid->path, &info));
        res->qid = stat2qid(&info);
    } else {
        failif(-1, EINVAL);
    }

    fid->path = newpath;
    fid->omode = req->mode;
    fid->offset = 0;
    fid->next_dir_entry = NULL;

    res->iounit = trans->conn->maxSize - RREAD_HEADER;

    send_reply(trans);
}

void envoy_tcreate(struct transaction *trans) {
    failif(-1, ENOTSUP);
}

/**
 * client_tread: transfer data from a file
 *
 * Arguments:
 * - fid[4]: file/directory to read from
 * - offset[8]: position (in bytes) from which to start reading
 * - count[4]: number of bytes to read
 *
 * Return:
 * - count[4]: number of bytes read
 * - data[count]: bytes read
 *
 * Semantics:
 * - fid must be open for reading
 * - read starts offset bytes from the beginning
 * - count returned may be equal or less than count requested
 * - if offset is >= number of bytes in file, count of zero is returned
 * - for directories:
 *   - read an integral number of directory entries as in stat
 *   - offset must be zero or offset + (returned) count of last request (no
 *     seek to anywhere but beginning of directory)
 * - more than one message may be produced by a single read call; iounit
 *   from open/create, if non-zero, gives maximum size guaranteed to be
 *   returned atomically
 */
void client_tread(struct transaction *trans) {
    struct Tread *req = &trans->in->msg.tread;
    struct Rread *res = &trans->out->msg.rread;
    struct fid *fid;
    u32 count;

    require_fid(fid);
    failif(req->count > trans->conn->maxSize - RREAD_HEADER, EMSGSIZE);
    count = req->count;

    if (fid->status == STATUS_OPEN_FILE) {
        int len;
        if (req->offset != fid->offset) {
            guard(lseek(fid->fd, req->offset, SEEK_SET));
            fid->offset = req->offset;
        }

        res->data = GC_MALLOC_ATOMIC(count);
        assert(res->data != NULL);

        guard(len = read(fid->fd, res->data, count));

        res->count = len;
        fid->offset += len;
    } else if (fid->status == STATUS_OPEN_DIR) {
        if (req->offset == 0 && fid->offset != 0) {
            rewinddir(fid->dd);
            fid->offset = 0;
            fid->next_dir_entry = NULL;
        }
        failif(req->offset != fid->offset, ESPIPE);

        res->data = GC_MALLOC_ATOMIC(count);
        assert(res->data != NULL);

        /* read directory entries until we run out or the buffer is full */
        res->count = 0;

        do {
            struct dirent *elt;
            struct stat info;

            /* process the next entry, which may be a leftover from earlier */
            if (fid->next_dir_entry != NULL) {
                /* are we going to overflow? */
                if (res->count + statsize(fid->next_dir_entry) > count)
                    break;
                packStat(res->data, &res->count, fid->next_dir_entry);
            }
            fid->next_dir_entry = NULL;

            elt = readdir(fid->dd);

            if (elt != NULL) {
                /* gather the info for the next entry and store it in the fid;
                 * we might not be able to fit it in this time */
                char *path = concatname(fid->path, elt->d_name);
                guard(lstat(path, &info));
                failif(stat2p9stat(&info, &fid->next_dir_entry, path) < 0, EIO);
            }
        } while (fid->next_dir_entry != NULL);

        failif(res->count == sizeof(u16) &&
                fid->next_dir_entry != NULL, ENAMETOOLONG);
        failif(res->count == sizeof(u16), ENOENT);

        /* take note of how many bytes we ended up with */
        fid->offset += res->count;
    } else {
        failif(-1, EPERM);
    }

    send_reply(trans);
}

void envoy_tread(struct transaction *trans) {
    failif(-1, ENOTSUP);
}

/**
 * client_twrite: transfer data to a file
 *
 * Arguments:
 * - fid[4]: file to write to
 * - offset[8]: position (in bytes) at which to start writing
 * - count[4]: number of bytes to write
 * - data[count: bytes to write
 *
 * Return:
 * - count[4]: number of bytes written
 *
 * Semantics:
 * - fid must be open for writing and must not be a directory
 * - write starts at offset bytes from the beginning of the file
 * - if file is append-only, offset is ignored and write happens at end of file
 * - count returned being <= count requested usually indicates an error
 * - more than one message may be produced by a single read call; iounit
 *   from open/create, if non-zero, gives maximum size guaranteed to be
 *   returned atomically
 */
void client_twrite(struct transaction *trans) {
    struct Twrite *req = &trans->in->msg.twrite;
    struct Rwrite *res = &trans->out->msg.rwrite;
    struct fid *fid;

    require_fid(fid);
    failif((fid->omode & OMASK) == OREAD, EACCES);
    failif((fid->omode & OMASK) == OEXEC, EACCES);

    if (fid->status == STATUS_OPEN_FILE) {
        int len;
        if (req->offset != fid->offset) {
            guard(lseek(fid->fd, req->offset, SEEK_SET));
            fid->offset = req->offset;
        }

        guard((len = write(fid->fd, req->data, req->count)));

        res->count = len;
        fid->offset += len;
    } else {
        failif(-1, EPERM);
    }

    send_reply(trans);
}

void envoy_twrite(struct transaction *trans) {
    failif(-1, ENOTSUP);
}

/**
 * client_tclunk: forget about a fid
 *
 * Arguments:
 * - fid[4]: fid to forget about
 *
 * Return:
 *
 * Semantics:
 * - fid is released and can be re-allocated
 * - if ORCLOSE was set at file open, file will be removed
 * - even if an error is returned, the fid is no longer valid
 */
void client_tclunk(struct transaction *trans) {
    struct Tclunk *req = &trans->in->msg.tclunk;
    struct fid *fid;

    require_fid_remove(fid);

    if (fid->status == STATUS_OPEN_FILE) {
        guard(close(fid->fd));
        if ((fid->omode & ORCLOSE))
            guard(unlink(fid->path));
    } else if (fid->status == STATUS_OPEN_DIR) {
        guard(closedir(fid->dd));
    }

    send_reply(trans);
}

void envoy_tclunk(struct transaction *trans) {
    failif(-1, ENOTSUP);
}

/**
 * client_tremove: remove a file
 *
 * Arguments:
 * - fid[4]: fid of file to remove
 *
 * Return:
 *
 * Semantics:
 * - file is removed
 * - fid is clunked, even if the remove fails
 * - client must have write permission in parent directory
 * - if other clients have the file open, file may or may not be removed
 *   immediately: this is implementation dependent
 */
void client_tremove(struct transaction *trans) {
    struct Tremove *req = &trans->in->msg.tremove;
    struct fid *fid;

    require_fid_remove(fid);

    if (fid->status == STATUS_OPEN_FILE) {
        guard(close(fid->fd));
        guard(unlink(fid->path));
    } else if (fid->status == STATUS_OPEN_DIR) {
        guard(closedir(fid->dd));
        guard(rmdir(fid->path));
    } else {
        struct stat info;
        guard(lstat(fid->path, &info));
        if (S_ISDIR(info.st_mode))
            guard(rmdir(fid->path));
        else
            guard(unlink(fid->path));
    }

    send_reply(trans);
}

void envoy_tremove(struct transaction *trans) {
    failif(-1, ENOTSUP);
}

/**
 * client_tstat: inquire about a file's attributes
 *
 * Arguments:
 * - fid[4]: fid of file being queried
 *
 * Return:
 * - stat[n]: file stats
 *
 * Semantics:
 * - requires no special permissions
 */
void client_tstat(struct transaction *trans) {
    struct Tstat *req = &trans->in->msg.tstat;
    struct Rstat *res = &trans->out->msg.rstat;
    struct fid *fid;
    struct stat info;

    require_fid(fid);
    guard(lstat(fid->path, &info));
    failif(stat2p9stat(&info, &res->stat, fid->path) < 0, EIO);

    send_reply(trans);
}

void envoy_tstat(struct transaction *trans) {
    failif(-1, ENOTSUP);
}

/**
 * client_twstat: modify a file's attributes
 *
 * Arguments:
 * - fid[4]: fid of file being modified
 * - stat[n]: requested file stats
 *
 * Return:
 *
 * Semantics:
 * - name can be changed by anyone with write permission in the directory
 * - name cannot be changed to an existing name
 * - length can be changed by anyone with write permission on the file
 * - length of directory can only be set to zero
 * - server may choose to reject length changes for other reasons
 * - mode and mtime can be changed by file's owner and by group leader
 * - directory bit cannot be changed, other mode and perm bits can
 * - gid can be changed by owner if also a member of new group, or by
 *   current group leader if also group leader of new group
 * - no other data can be changed by wstat, and attempts will trigger an error
 * - it is illegal to attempt to change owner of a file
 * - changes are atomic: all success or none happen
 * - request can include "don't touch" values: empty strings for text fields
 *   and ~0 for unsigned values
 * - if all fields are "don't touch", file contents should be committed to
 *   stable storage before the Rwstat is returned
 * - return value has 3 length fiels:
 *   - implicit length taken from message size
 *   - n from stat[n] (number of bytes in stat record)
 *   - size field in the stat record
 *   - note that n = size + 2
 */
void client_twstat(struct transaction *trans) {
    struct Twstat *req = &trans->in->msg.twstat;
    struct fid *fid;
    struct stat info;
    char *name;

    require_fid(fid);
    failif(!strcmp(fid->path, rootdir), EPERM);

    if (fid->status == STATUS_OPEN_SYMLINK) {
        failif(emptystring(req->stat->extension), EINVAL);

        guard(symlink(req->stat->extension, fid->path));

        fid->status = STATUS_CLOSED;
        send_reply(trans);
        return;
    } else if (fid->status == STATUS_OPEN_LINK) {
        u32 targetfid;
        struct fid *target;
        failif(emptystring(req->stat->extension), EINVAL);
        failif(sscanf(req->stat->extension, "hardlink(%u)", &targetfid) != 1,
                EINVAL);
        failif(targetfid == NOFID, EBADF);
        require_alt_fid(target, targetfid);

        guard(link(target->path, fid->path));

        fid->status = STATUS_CLOSED;
        send_reply(trans);
        return;
    } else if (fid->status == STATUS_OPEN_DEVICE) {
        char c;
        int major, minor;
        failif(emptystring(req->stat->extension), EINVAL);
        failif(sscanf(req->stat->extension, "%c %d %d", &c, &major, &minor) !=
                3, EINVAL);
        failif(c != 'c' && c != 'b', EINVAL);

        guard(mknod(fid->path,
                    (fid->omode & 0777) | (c == 'c' ? S_IFCHR : S_IFBLK), 
                    makedev(major, minor)));

        fid->status = STATUS_CLOSED;
        send_reply(trans);
        return;
    }
    
    /* regular file or directory */
    guard(lstat(fid->path, &info));
    name = filename(fid->path);

    /* rename */
    if (!emptystring(req->stat->name) && strcmp(name, req->stat->name)) {
        char *newname = concatname(dirname(fid->path), req->stat->name);
        struct stat newinfo;
        failif(strchr(req->stat->name, '/'), EINVAL);
        failif(lstat(newname, &newinfo) >= 0, EEXIST);

        guard(rename(fid->path, newname));

        fid->path = newname;
    }

    /* mtime */
    if (req->stat->mtime != ~(u32) 0) {
        struct utimbuf buf;
        buf.actime = 0;
        buf.modtime = req->stat->mtime;

        guard(utime(fid->path, &buf));
    }

    /* mode */
    if (req->stat->mode != ~(u32) 0 && req->stat->mode != info.st_mode) {
        int mode = req->stat->mode & 0777;
        if ((req->stat->mode & DMSETUID))
            mode |= S_ISUID;
        if ((req->stat->mode & DMSETGID))
            mode |= S_ISGID;

        guard(chmod(fid->path, mode));
    }

    /* gid */
    if (req->stat->gid != NULL && *req->stat->gid &&
            req->stat->n_gid != ~(u32) 0)
    {
        guard(lchown(fid->path, -1, req->stat->n_gid));
    }

    /* uid */
    if (req->stat->uid != NULL && *req->stat->uid &&
            req->stat->n_uid != ~(u32) 0)
    {
        guard(lchown(fid->path, req->stat->n_uid, -1));
    }

    /* truncate */
    if (req->stat->length != ~(u64) 0 && req->stat->length != info.st_size) {
        guard(truncate(fid->path, req->stat->length));
    }

    send_reply(trans);
}

void envoy_twstat(struct transaction *trans) {
    failif(-1, ENOTSUP);
}
