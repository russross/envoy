#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <utime.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "connection.h"
#include "transaction.h"
#include "fid.h"
#include "util.h"
#include "config.h"
#include "state.h"
#include "fs.h"
#include "dispatch.h"
#include "map.h"
#include "worker.h"
#include "forward.h"

/* convert a u32 to a string, allocating the necessary storage */
static inline char *u32tostr(u32 n) {
    char *res = GC_MALLOC_ATOMIC(11);
    assert(res != NULL);

    sprintf(res, "%u", n);
    return res;
}

/*****************************************************************************/

/* generate an error response with Unix errno errnum */
static void rerror(Message *m, u16 errnum, int line) {
    m->id = RERROR;
    m->msg.rerror.errnum = errnum;
    m->msg.rerror.ename = stringcopy(strerror(errnum));
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

/*
 * forward a request to an envoy by copying trans->in to env->out and
 * changing fids, then send the message, wait for a reply, copy the
 * reply to trans->out, and send it.
 */
#define copy_forward(MESSAGE) do { \
    fwd = forward_lookup(trans->conn, trans->in->msg.MESSAGE.fid); \
    env->out->msg.MESSAGE.fid = fwd->rfid; \
} while(0);

void forward_to_envoy(Worker *worker, Transaction *trans) {
    Transaction *env;
    Forward *fwd = NULL;

    assert(trans != NULL);

    /* copy the whole message over */
    env = trans_new(NULL, NULL, message_new());
    memcpy(&env->out->msg, &trans->in->msg, sizeof(trans->in->msg));
    env->out->id = trans->in->id;

    /* translate the fid */
    switch (trans->in->id) {
        /* look up the fid translation */
        case TATTACH:   copy_forward(tattach);  break;
        case TOPEN:     copy_forward(topen);    break;
        case TCREATE:   copy_forward(tcreate);  break;
        case TREAD:     copy_forward(tread);    break;
        case TWRITE:    copy_forward(twrite);   break;
        case TCLUNK:    copy_forward(tclunk);   break;
        case TREMOVE:   copy_forward(tremove);  break;
        case TSTAT:     copy_forward(tstat);    break;
        case TWSTAT:    copy_forward(twstat);   break;

        /* it's a bug if we got called for the remaining messages */
        default:
            assert(0);
    }

    env->conn = fwd->rconn;
    send_request(env);

    /* now copy the response back into trans */
    memcpy(&trans->out->msg, &env->in->msg, sizeof(env->in->msg));
    trans->out->id = env->in->id;

    send_reply(trans);
}

#undef copy_forward


/*****************************************************************************/

/**
 * tversion: initial handshake
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
void handle_tversion(Worker *worker, Transaction *trans) {
    struct Tversion *req = &trans->in->msg.tversion;
    struct Rversion *res = &trans->out->msg.rversion;

    failif(trans->in->tag != NOTAG, ECONNREFUSED);

    res->msize = max(min(GLOBAL_MAX_SIZE, req->msize), GLOBAL_MIN_SIZE);
    trans->conn->maxSize = res->msize;

    if (state->isstorage) {
        if (!strcmp(req->version, "9P2000.storage")) {
            trans->conn->type = CONN_STORAGE_IN;
            res->version = req->version;
        } else {
            res->version = "unknown";
        }
    } else {
        if (!strcmp(req->version, "9P2000.u")) {
            trans->conn->type = CONN_CLIENT_IN;
            res->version = req->version;
        } else if (!strcmp(req->version, "9P2000.envoy")) {
            trans->conn->type = CONN_ENVOY_IN;
            res->version = req->version;
        } else {
            res->version = "unknown";
        }
    }

    send_reply(trans);
}

/*****************************************************************************/

/**
 * tauth: check credentials to authorize a connection
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
void handle_tauth(Worker *worker, Transaction *trans) {
    failif(-1, ENOTSUP);
}

/**
 * tattach: establish a connection
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
void handle_tattach(Worker *worker, Transaction *trans) {
    struct Tattach *req = &trans->in->msg.tattach;
    struct Rattach *res = &trans->out->msg.rattach;
    struct sockaddr_in *addr;
    List *walk_req, *walk_res;

    failif(req->afid != NOFID, EBADF);
    failif(emptystring(req->uname), EINVAL);

    /* start from the global root and walk to the attach point */
    walk_req = splitpath(req->aname);
    walk_res = walk(get_root_address(), walk_req, req->uname);

    /* was there an error on the walk? */
    failif(length(walk_req) != length(walk_res), ENOENT);

    /* set up a fid here and possibly remotely */

    if ((addr = get_envoy_address(req->aname)) != NULL &&
            !addr_cmp(addr, trans->conn->addr))
    {
        /* this request should be forwarded */
        Connection *rconn;

        if (trans->conn->type == CONN_ENVOY_IN) {
            /* this request shouldn't have been sent here */
            handle_error(worker, trans);
            return;
        }

        rconn = conn_get_from_addr(worker, addr);

        /* did we fail to connect to the remote envoy? */
        if (rconn == NULL) {
            handle_error(worker, trans);
            return;
        }

        if (forward_create_new(trans->conn, req->fid, rconn) == NOFID) {
            handle_error(worker, trans);
            return;
        }

        forward_to_envoy(worker, trans);
    } else {
        /* this request can be handled locally */
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
    }
}

/**
 * tflush: abort a message
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
void handle_tflush(Worker *worker, Transaction *trans) {
    failif(-1, ENOTSUP);
}

/**
 * twalk: descend a directory hierarchy
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
//static List *walk_path_list(int n, int count, char **names, char *path) {
//    char *newpath;
//
//    if (n == count || !names[n] || !*names[n] || strchr(names[n], '/'))
//        return NULL;
//
//    if (!strcmp(names[n], "."))
//        newpath = path;
//    else if (!strcmp(names[n], ".."))
//        newpath = dirname(path);
//    else
//        newpath = concatname(path, names[n]);
//
//    return cons(newpath, walk_path_list(n+1, count, names, newpath));
//}
//
//static List *walk_qid_list(List *paths) {
//    struct stat info;
//
//    if (null(paths) || lstat(car(paths), &info) < 0)
//        return NULL;
//
//    return cons(stat2qid(&info), walk_qid_list(cdr(paths)));
//}

void client_twalk(Worker *worker, Transaction *trans) {
    struct Twalk *req = &trans->in->msg.twalk;
    struct Rwalk *res = &trans->out->msg.rwalk;
    Fid *fid;
    struct stat info;
    char *newpath;
//    List *paths, *qids;
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

    /* make sure we're starting from a directory */
    guard(lstat(fid->path, &info));
    failif(!S_ISDIR(info.st_mode), ENOTDIR);

    res->nwqid = 0;
    res->wqid = GC_MALLOC_ATOMIC(sizeof(struct qid) * req->nwname);
    assert(res->wqid != NULL);

//    /* get the list of paths to check */
//    paths = walk_path_list(0, req->nwname, req->wname, fid->path);
//
//    /* gather the qids for all the paths */
//    qids = walk_qid_list(paths);
//
//    /* now pack them into a reply */
//    for (i = 0; !null(qids); qids = cdr(qids), i++) {
//        if (i < req->nwname - 1 && !(res->wqid[i]->type & QTDIR))
//            break;
//        res->wqid[i] = car(qids);
//        res->nwqid = i + 1;
//    }
//
//    /* if we had qids left, it means we quit on a non-directory */
//    failif(i == 0 && !null(qid), ENOTDIR);
//    failif(i == 0, EINVAL);
//
//    /* there was an error somewhere on the way (after the first element) */
//    if (i != req->nwname) {
//        send_reply(trans);
//        return;
//    }

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

void envoy_twalk(Worker *worker, Transaction *trans) {
    failif(-1, ENOTSUP);
}

/**
 * topen: prepare a fid for i/o on an existing file
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
void handle_topen(Worker *worker, Transaction *trans) {
    struct Topen *req = &trans->in->msg.topen;
    struct Ropen *res = &trans->out->msg.ropen;
    Fid *fid;
    struct p9stat *info;
    u32 perm;

    require_fid_closed(fid);

    /* we don't support remove-on-close */
    failif(req->mode & ORCLOSE, ENOTSUP);

    info = object_stat(worker, fid->claim->oid);

    /* figure out which permissions are required based on the request */
    if (info->type & QTDIR) {
        /* directories can only be opened for reading */
        failif(req->mode != OREAD, EPERM);
        perm = 0444;
    } else {
        /* files have a few basic modes plus optional flags */
        switch (req->mode & OMASK) {
            case OREAD:     perm = 0444; break;
            case OWRITE:    perm = 0222; break;
            case ORDWR:     perm = 0666; break;
            case OEXEC:     perm = 0111; break;
            default:        failif(-1, EINVAL);
        }

        /* truncate is ignored if the file is append-only */
        if ((req->mode & OTRUNC) && !(info->mode & DMAPPEND))
            perm |= 0222;
    }

    /* check against the file's actual permissions */
    if (!strcmp(fid->uname, info->uid))
        failif(info->mode & perm & 0700 != perm & 0700, EPERM);
    else if (groupmember(fid->uname, info->gid))
        failif(info->mode & perm & 0070 != perm & 0070, EPERM);
    else
        failif(info->mode & perm & 0007 != perm & 0007, EPERM);

    /* make sure the file's not in use if it has DMEXCL set */
    if (info->mode & DMEXCL) {
        failif(fid->claim->refcount != 0, EBUSY);
        fid->claim->refcount = -1;
    } else {
        fid->claim->refcount++;
    }

    /* init the file status */
    fid->omode = req->mode;
    fid->readdir_offset = 0LL;
    fid->readdir_cookie = 0;
    fid->status = (info->type & QTDIR) ? STATUS_OPEN_DIR : STATUS_OPEN_FILE;

    /* if it is opened writable, make sure we have a writable object */
    if (req->mode & (OWRITE | ORDWR | OTRUNC))
        require_writeaccess(fid);

    /* send a hint to the cache that we are likely to access this file */
    object_prime_cache(worker, fid->claim->oid);

    res->iounit = trans->conn->maxSize - RREAD_HEADER;
    res->qid = info->qid;

    /* use the original oid to identify the file to the client in case a CoW has happened */
    res->qid.path = fid->client_oid;

    /* do we need to truncate the file? */
    if ((req->mode & OTRUNC) && !(info->mode & DMAPPEND) && info->length > 0LL) {
        info->mode = ~(u32) 0;
        info->atime = info->mtime = ~(u32) 0;
        info->uid = info->gid = NULL;
        info->extension = NULL;
        info->length = 0LL;
        info->name = NULL;

        object_wstat(worker, fid->claim->oid, info);
    }

    send_reply(trans);
}

/**
 * tcreate: prepare a fid for i/o on a new file
 *
 * Arguments:
 * - fid[4]: fid of directory in which file is to be created
 * - name[s]: name of new file
 * - perm[4]: permissions for new file
 * - mode[1]: file mode for new file
 * - extension[s]: special file details
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
void handle_tcreate(Worker *worker, Transaction *trans) {
    struct Tcreate *req = &trans->in->msg.tcreate;
    struct Rcreate *res = &trans->out->msg.rcreate;
    Fid *fid;
    struct p9stat *dirinfo;
    struct p9stat *info;
    char *newpath;
    u32 perm;
    struct qid qid;
    enum fid_status status;

    require_fid_closed(fid);
    failif(!strcmp(req->name, ".") || !strcmp(req->name, "..") ||
            strchr(req->name, '/'), EINVAL);

    /* create can only occur in a directory */
    dirinfo = object_stat(worker, fid->claim->oid);
    failif(!(dirinfo->type & QTDIR), ENOTDIR);

    newpath = concatname(fid->path, req->name);

    /* make sure the mode is valid for opening the new file */
    if ((req->mode & DMDIR)) {
        failif(req->mode != OREAD, EINVAL);
        perm = req->perm & (~0777 | (dirinfo->mode & 0777));
    } else {
        failif(req->mode & OTRUNC, EINVAL);
        perm = req->perm & (~0666 | (dirinfo->mode & 0666));
    }

    /* figure out the status-to-be of the new file */
    if ((req->perm & DMDIR))
        status = STATUS_OPEN_DIR;
    else if ((req->perm & DMSYMLINK))
        status = STATUS_OPEN_SYMLINK;
    else if ((req->perm & DMLINK))
        status = STATUS_OPEN_LINK;
    else if ((req->perm & DMDEVICE))
        status = STATUS_OPEN_DEVICE;
    else if (!(req->perm & DMMASK))
        status = STATUS_OPEN_FILE;
    else
        failif(-1, EINVAL);

    /* TODO: make sure the file doesn't already exist */

    /* create the file */
    qid = object_create(worker, object_reserve_oid(worker), perm, time(), fid->uid,
            dirinfo->gid, req->extension);

    /* TODO: add a directory entry for the new file */

    fid->status = status;
    fid->path = newpath;
    fid->omode = req->mode;
    fid->readdir_offset = 0LL;
    fid->readdir_cookie = 0;

    res->qid = qid;
    res->iounit = trans->conn->maxSize - RREAD_HEADER;

    send_reply(trans);
}

/**
 * tread: transfer data from a file
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
void handle_tread(Worker *worker, Transaction *trans) {
    struct Tread *req = &trans->in->msg.tread;
    struct Rread *res = &trans->out->msg.rread;
    Fid *fid;

    require_fid(fid);
    failif(req->count > trans->conn->maxSize - RREAD_HEADER, EMSGSIZE);

    if (fid->status == STATUS_OPEN_FILE) {
        res->data = object_read(worker, fid->claim->oid, time(), req->offset, req->count,
                &res->count);
    } else if (fid->status == STATUS_OPEN_DIR) {
        /* allow rewinds, but no other offset changes */
        if (req->offset == 0 && fid->readdir_cookie != 0) {
            fid->readdir_cookie = 0;
            fid->readdir_offset = 0;
            fid->readdir_next = NULL;
            fid->readdir_current_block = NULL;
        }
        failif(req->offset != fid->readdir_cookie, ESPIPE);

        res->data = GC_MALLOC_ATOMIC(req->count);
        assert(res->data != NULL);

        /* read directory entries until we run out or the buffer is full */
        res->count = 0;

        do {
            /* is this an attempt to read past the end? */
            if (fid->readdir_next == NULL && fid->readdir_offset > 0)
                break;

            /* pack an entry if we have one */
            if (fid->readdir_next != NULL) {
                /* is it too big to fit in the packet? */
                if (res->count + statsize(fid->readdir_next) > req->count)
                    break;

                packStat(res->data, (int *) &res->count, fid->readdir_next);
                fid->readdir_next = NULL;

                /* advance the offset appropriately */
                fid->readdir_offset &= ~((u64) BLOCK_SIZE - 1);
                if (null(fid->readdir_current_block)) {
                    /* advance to the next block */
                    fid->readdir_offset += BLOCK_SIZE;
                } else {
                    /* advance to the next entry in this block */
                    struct direntry *elt = car(fid->readdir_current_block);
                    fid->readdir_offset += elt->offset;
                }
            }

            /* do we need to read a new directory block? */
            if (null(fid->readdir_current_block)) {
                u32 bytesread;
                void *block;

                block = object_read(worker, fid->claim->oid, time(),
                        fid->readdir_offset, BLOCK_SIZE, &bytesread);

                /* are we at end-of-file? */
                if (bytesread == 0)
                    break;

                fid->readdir_current_block = dir_get_entries(bytesread, block);
            }

            /* find another entry */
            if (!null(fid->readdir_current_block)) {
                struct direntry *elt = car(fid->readdir_current_block);
                char *childpath = concatname(fid->claim->pathname, elt->filename);

                /* can we get stats locally? */
                if (lease_local_read(childpath)) {
                    fid->readdir_next = object_stat(worker, elt->oid);
                    fid->readdir_next->name = elt->filename;
                } else {
                    fid->readdir_next = remote_stat(worker, childpath);
                    fid->readdir_next->name = elt->filename;
                }

                fid->readdir_offset &= ~((u64) BLOCK_SIZE - 1);
                fid->readdir_offset += elt->offset;
                fid->readdir_current_block = cdr(fid->readdir_current_block);
            }
        } while (fid->readdir_next != NULL);

        /* was a single entry too big for the return buffer? */
        failif(res->count <= sizeof(u16) && fid->readdir_next != NULL, ENAMETOOLONG);

        /* end of file? */
        failif(res->count <= sizeof(u16), ENOENT);

        /* take note of how many bytes we ended up with */
        fid->readdir_cookie += res->count;
    } else {
        failif(-1, EPERM);
    }

    send_reply(trans);
}

/**
 * twrite: transfer data to a file
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
void handle_twrite(Worker *worker, Transaction *trans) {
    struct Twrite *req = &trans->in->msg.twrite;
    struct Rwrite *res = &trans->out->msg.rwrite;
    Fid *fid;

    require_fid(fid);
    failif((fid->omode & OMASK) == OREAD, EACCES);
    failif((fid->omode & OMASK) == OEXEC, EACCES);

    if (fid->status == STATUS_OPEN_FILE) {
        guard((len = write(fid->fd, req->data, req->count)));
        res->count = object_write(worker, fid->claim->oid, time(), req->offset, req->count,
                req->data);
    } else {
        failif(-1, EPERM);
    }

    send_reply(trans);
}

/**
 * tclunk: forget about a fid
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
void handle_tclunk(Worker *worker, Transaction *trans) {
    struct Tclunk *req = &trans->in->msg.tclunk;
    Fid *fid;

    require_fid_remove(fid);

    /* we don't support remove-on-close */

    if (fid->claim->refcount == -1 || fid->claim->refcount == 1) {
        /* shut down this claim */
        claim_remove(fid->claim);
    } else {
        fid->claim->refcount--;
    }

    send_reply(trans);
}

/**
 * tremove: remove a file
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
void handle_tremove(Worker *worker, Transaction *trans) {
    struct Tremove *req = &trans->in->msg.tremove;
    Fid *fid;

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

/**
 * tstat: inquire about a file's attributes
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
void handle_tstat(Worker *worker, Transaction *trans) {
    struct Tstat *req = &trans->in->msg.tstat;
    struct Rstat *res = &trans->out->msg.rstat;
    Fid *fid;
    struct stat info;

    require_fid(fid);
    guard(lstat(fid->path, &info));
    failif(stat2p9stat(&info, &res->stat, fid->path) < 0, EIO);

    send_reply(trans);
}

/**
 * twstat: modify a file's attributes
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
void handle_twstat(Worker *worker, Transaction *trans) {
    struct Twstat *req = &trans->in->msg.twstat;
    Fid *fid;
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
        Fid *target;
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
    if (!emptystring(req->stat->gid) && req->stat->n_gid != ~(u32) 0)
        guard(lchown(fid->path, -1, req->stat->n_gid));

    /* uid */
    if (!emptystring(req->stat->uid) && req->stat->n_uid != ~(u32) 0)
        guard(lchown(fid->path, req->stat->n_uid, -1));

    /* truncate */
    if (req->stat->length != ~(u64) 0 && req->stat->length != info.st_size)
        guard(truncate(fid->path, req->stat->length));

    send_reply(trans);
}
