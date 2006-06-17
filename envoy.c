#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
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
#include "object.h"
#include "envoy.h"
#include "dispatch.h"
#include "worker.h"
#include "dir.h"
#include "claim.h"
#include "lease.h"
#include "walk.h"

/* convert a u32 to a string, allocating the necessary storage */
static inline char *u32tostr(u32 n) {
    char *res = GC_MALLOC_ATOMIC(11);
    assert(res != NULL);

    sprintf(res, "%u", n);
    return res;
}

u32 now(void) {
    return (u32) time(NULL);
}

/* TODO */
int isgroupmember(char *user, char *group) {
    return 1;
}
int isgroupleader(char *user, char *group) {
    return 1;
}

int has_permission(char *uname, struct p9stat *info, u32 required) {
    if (!strcmp(uname, info->uid)) {
        return (info->mode & required & 0700) == (required & 0700) &&
            (required & 0700) != 0;
    } else if (isgroupmember(uname, info->gid)) {
        return (info->mode & required & 0070) == (required & 0070) &&
            (required & 0070) != 0;
    } else {
        return (info->mode & required & 0007) == (required & 0007) &&
            (required & 0007) != 0;
    }
}

static void walklist_to_qids(u16 length, List *from, struct qid **to) {
    int i;

    *to = GC_MALLOC_ATOMIC(sizeof(struct qid) * length);
    assert(*to != NULL);

    for (i = length - 1; i >= 0; i--) {
        Walk *walk = car(from);
        from = cdr(from);
        *to[i] = *walk->qid;
    }
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

#define require_fid_unopenned(ptr) do { \
    (ptr) = fid_lookup(trans->conn, req->fid); \
    if ((ptr) == NULL) { \
        rerror(trans->out, EBADF, __LINE__); \
        send_reply(trans); \
        return; \
    } else if ((ptr)->status != STATUS_UNOPENNED) { \
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
    env->out->msg.MESSAGE.fid = fid->rfid; \
} while(0);

void forward_to_envoy(Worker *worker, Transaction *trans, Fid *fid) {
    Transaction *env;

    assert(fid->isremote);

    /* copy the whole message over */
    env = trans_new(NULL, NULL, message_new());
    memcpy(&env->out->msg, &trans->in->msg, sizeof(trans->in->msg));
    env->out->id = trans->in->id;

    /* translate the fid */
    switch (trans->in->id) {
        /* look up the fid translation */
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

    env->conn = conn_get_from_addr(worker, fid->raddr);
    send_request(env);

    /* now copy the response back into trans */
    memcpy(&trans->out->msg, &env->in->msg, sizeof(env->in->msg));
    trans->out->id = env->in->id;
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
    int maxsize;

    failif(trans->in->tag != NOTAG, ECONNREFUSED);

    if (state->isstorage) {
        maxsize = GLOBAL_MAX_SIZE;
        if (!strcmp(req->version, "9P2000.storage")) {
            trans->conn->type = CONN_STORAGE_IN;
            res->version = req->version;
        } else {
            res->version = "unknown";
        }
    } else {
        maxsize = GLOBAL_MAX_SIZE - STORAGE_SLUSH;
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

    res->msize = max(min(maxsize, req->msize), GLOBAL_MIN_SIZE);
    trans->conn->maxSize = res->msize;

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
    struct walk_response *walkres;
    List *names;
    enum walk_request_type;
    Walk *walk;

    failif(req->afid != NOFID, EBADF);
    failif(emptystring(req->uname), EINVAL);

    /* make sure walk knows how to find the remote address */
    if (!addr_cmp(state->root_address, state->my_address))
        walk_prime("/", req->uname, state->root_address);

    /* treat this like a walk from the global root */
    if (emptystring(req->aname))
        names = cons(NULL, NULL);
    else
        names = append_elt(splitpath(req->aname), NULL);
    walkres = common_twalk(worker, trans, 1, req->fid, names, "/", req->uname);

    failif(walkres->type == WALK_ERROR, walkres->errnum);
    failif(length(walkres->walks) != length(names), ENOENT);

    /* get the last qid, which is the first in the list (we know it's !NULL) */
    walk = car(walkres->walks);
    res->qid = *walk->qid;

    send_reply(trans);
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

/* Handle walk requests from a client.  Do a few basic checks and format a
 * request for the more general function. */
void client_twalk(Worker *worker, Transaction *trans) {
    struct Twalk *req = &trans->in->msg.twalk;
    struct Rwalk *res = &trans->out->msg.rwalk;
    int i;
    List *names;
    struct walk_response *walkres;
    Fid *fid;

    /* verify the limit on how many names can be walked in a single request */
    failif(req->nwname > MAXWELEM, EMSGSIZE);

    /* make sure the new fid isn't already in use (if it's actually new) */
    failif(req->newfid != req->fid &&
            fid_lookup(trans->conn, req->newfid) != NULL,
            EBADF);

    /* copy the array of names into a list with a blank at the end */
    names = cons(NULL, NULL);
    for (i = req->nwname - 1; i >= 0; i--) {
        failif(strchr(req->wname[i], '/') != NULL, EINVAL);
        names = cons(req->wname[i], names);
    }

    /* does this start with a remote fid? */
    failif((fid = fid_lookup(trans->conn, req->fid)) == NULL, EBADF);
    if (fid->isremote) {
        /* make sure there's a cache hint about where to find the start */
        walk_prime(fid->pathname, fid->user, fid->raddr);
        walkres = common_twalk(worker, trans, 1, req->newfid,
                names, fid->pathname, fid->user);
    } else {
        failif(fid->status != STATUS_UNOPENNED, ETXTBSY);
        walkres = common_twalk(worker, trans, 1, req->newfid,
                names, fid->claim->pathname, fid->user);
    }

    /* did we fail on the first step? */
    failif(walkres->type == WALK_ERROR && length(walkres->walks) < 2,
            walkres->errnum);

    /* the last walk is the starting point, so throw it away */
    res->nwqid = length(walkres->walks) - 1;
    walklist_to_qids(res->nwqid, walkres->walks, &res->wqid);

    /* TODO: update the forward pathname, fid if necessary */
    if (walkres->type == WALK_SUCCESS &&
            length(walkres->walks) == length(names))
    {
    }
    /* TODO: remove the starting fid if necessary */

    send_reply(trans);
}

void envoy_twalk(Worker *worker, Transaction *trans) {
    struct Tewalkremote *req = &trans->in->msg.tewalkremote;
    struct Rewalkremote *res = &trans->out->msg.rewalkremote;
    int i;
    char *pathname;
    char *user;
    List *names;
    struct walk_response *walkres;
    Walk *walk;

    /* copy the array of names into a list */
    names = NULL;
    for (i = req->nwname - 1; i >= 0; i--)
        names = cons(req->wname[i], names);

    /* is this the start of the walk? */
    if (emptystring(req->user) || emptystring(req->path)) {
        /* this must be from an existing, closed fid */
        Fid *fid = fid_lookup(trans->conn, req->fid);
        failif(fid == NULL, EBADF);
        failif(fid->status != STATUS_UNOPENNED, ETXTBSY);
        pathname = fid->claim->pathname;
        user = fid->user;
    } else {
        /* this is coming here after crossing a lease boundary */
        pathname = req->path;
        user = req->user;
    }

    /* we can only handle local requests */
    failif(lease_find_root(pathname) == NULL, EBADF);
    walk_prime(pathname, user, NULL);

    walkres = common_twalk(worker, trans, 0, req->newfid, names,
            pathname, user);

    /* did we fail on the first step? */
    failif(walkres->type == WALK_ERROR && null(walkres->walks),
            walkres->errnum);

    res->nwqid = length(walkres->walks);
    walklist_to_qids(res->nwqid, walkres->walks, &res->wqid);

    /* fix the pathname to match what was walked */
    while (names != walkres->names) {
        pathname = walk_pathname(pathname, car(names));
        names = cdr(names);
    }

    walk = walk_lookup(pathname, user);
    if (walk != NULL) {
        res->address =
            ((walk->addr->sin_addr.s_addr >> 24) & 0xff) |
            ((walk->addr->sin_addr.s_addr >> 16) & 0xff) |
            ((walk->addr->sin_addr.s_addr >>  8) & 0xff) |
            ( walk->addr->sin_addr.s_addr        & 0xff);
        res->port =
            ((walk->addr->sin_port >>  8) & 0xff) |
            ( walk->addr->sin_port        & 0xff);
    } else {
        res->address = ~(u32) 0;
        res->port = ~(u16) 0;
    }
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

    require_fid_unopenned(fid);

    /* we don't support remove-on-close */
    failif(req->mode & ORCLOSE, ENOTSUP);

    /* handle forwarding */
    if (fid->isremote) {
        forward_to_envoy(worker, trans, fid);

        /* update local state based on the reply */
        if (trans->out->tag == ROPEN) {
            fid->status = (res->qid.type & QTDIR) ?
                STATUS_OPEN_DIR : STATUS_OPEN_FILE;
            fid->omode = req->mode;
            fid->readdir_cookie = 0LL;
        }

        goto send_reply;
    }

    if (fid->claim->info == NULL) {
        fid->claim->info =
            object_stat(worker, fid->claim->oid,
                    filename(fid->claim->pathname));
    }
    info = fid->claim->info;

    /* figure out which permissions are required based on the request */
    if (info->mode & DMDIR) {
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
    failif(!has_permission(fid->user, info, perm), EPERM);

    /* make sure the file's not in use if it has DMEXCL set */
    if (info->mode & DMEXCL) {
        failif(fid->claim->refcount != 0, EBUSY);
        fid->claim->refcount = -1;
    } else {
        fid->claim->refcount++;
    }

    /* init the file status */
    fid->omode = req->mode;
    fid->readdir_cookie = 0;
    fid->readdir_env = NULL;
    fid->status = (info->mode & DMDIR) ? STATUS_OPEN_DIR : STATUS_OPEN_FILE;

    res->iounit = trans->conn->maxSize - RREAD_HEADER;
    res->qid = info->qid;

    /* do we need to truncate the file? */
    if ((req->mode & OTRUNC) && !(info->mode & DMAPPEND) && info->length > 0LL)
    {
        struct p9stat *changes = p9stat_new();
        changes->length = 0LL;

        object_wstat(worker, fid->claim->oid, changes);

        /* the mtime wrong now */
        fid->claim->info = NULL;
    }

    send_reply:
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
    u32 perm;
    struct qid qid;
    enum fid_status status;
    u64 newoid;

    failif(!strcmp(req->name, ".") || !strcmp(req->name, "..") ||
            strchr(req->name, '/'), EINVAL);

    require_fid_unopenned(fid);

    /* figure out the status-to-be of the new file */
    if ((req->perm & DMDIR))
        status = STATUS_OPEN_DIR;
    else if (!(req->perm & DMMASK))
        status = STATUS_OPEN_FILE;
    else
        status = STATUS_UNOPENNED;

    /* handle forwarding */
    if (fid->isremote) {
        forward_to_envoy(worker, trans, fid);

        /* update local state based on the reply */
        if (trans->out->tag == RCREATE) {
            fid->pathname = concatname(fid->pathname, req->name);
            fid->status = status;
            fid->omode = req->mode;
            fid->readdir_cookie = 0LL;
        }

        goto send_reply;
    }

    /* create can only occur in a directory */
    dirinfo =
        object_stat(worker, fid->claim->oid, filename(fid->claim->pathname));
    failif(!(dirinfo->mode & DMDIR), ENOTDIR);

    /* make sure the mode is valid for opening the new file */
    if ((req->perm & DMDIR)) {
        failif(req->mode != OREAD, EINVAL);
        perm = req->perm & (~0777 | (dirinfo->mode & 0777));
    } else {
        failif(req->mode & OTRUNC, EINVAL);
        perm = req->perm & (~0666 | (dirinfo->mode & 0666));
    }

    /* create the file */
    newoid = object_reserve_oid(worker);

    qid = object_create(worker, newoid, perm, now(), fid->user,
            dirinfo->gid, req->extension);

    /* note: the client normally checks to make sure this doesn't exist before
     * trying to create it, but a race with another client could still happen */
    failif(dir_create_entry(worker, fid, dirinfo, req->name, newoid) < 0,
            EEXIST);
    /* object_delete(worker, newoid); */

    /* move this fid to the new file */
    fid->claim = claim_new(fid->claim, req->name, ACCESS_WRITEABLE, newoid);
    fid->status = status;
    fid->omode = req->mode;

    fid->readdir_cookie = 0;
    fid->readdir_env = NULL;

    /* prepare and send the reply */
    res->qid = qid;
    res->iounit = trans->conn->maxSize - RREAD_HEADER;

    send_reply:
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
    u32 count = (req->count > trans->conn->maxSize - RREAD_HEADER) ?
        trans->conn->maxSize - RREAD_HEADER : req->count;

    require_fid(fid);

    /* handle forwarding */
    if (fid->isremote) {
        forward_to_envoy(worker, trans, fid);

        /* update local state based on the reply */
        if (trans->out->tag == RREAD && fid->status == STATUS_OPEN_DIR) {
            if (req->offset == 0)
                fid->readdir_cookie = 0;
            fid->readdir_cookie += res->count;
        }

        goto send_reply;
    }

    if (fid->status == STATUS_OPEN_FILE) {
        res->data = object_read(worker, fid->claim->oid, now(), req->offset,
                count, &res->count);
    } else if (fid->status == STATUS_OPEN_DIR) {
        /* allow rewinds, but no other offset changes */
        if (req->offset == 0 && fid->readdir_cookie != 0) {
            fid->readdir_cookie = 0;
            fid->readdir_env = NULL;
        }
        failif(req->offset != fid->readdir_cookie, ESPIPE);

        res->data = GC_MALLOC_ATOMIC(count);
        assert(res->data != NULL);

        /* read directory entries until we run out or the buffer is full */
        res->count = dir_read(worker, fid, count, res->data);

        /* was a single entry too big for the return buffer? */
        failif(res->count == 0 && fid->readdir_env->next != NULL,
                ENAMETOOLONG);

        /* take note of how many bytes we ended up with */
        /* note: eof is signaled by return 0 bytes (dir_read caches this) */
        fid->readdir_cookie += res->count;
    } else {
        failif(-1, EPERM);
    }

send_reply:
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
    failif(fid->status != STATUS_OPEN_FILE, EPERM);

    /* handle forwarding */
    if (fid->isremote) {
        forward_to_envoy(worker, trans, fid);
        goto send_reply;
    }

    res->count = object_write(worker, fid->claim->oid, now(), req->offset,
            req->count, req->data);
    fid->claim->info = NULL;

    send_reply:
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

    /* handle forwarding */
    if (fid->isremote) {
        forward_to_envoy(worker, trans, fid);
        goto send_reply;
    }

    /* we don't support remove-on-close */

    send_reply:
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
    struct p9stat *info;
    int res;

    require_fid_remove(fid);

    /* handle forwarding */
    if (fid->isremote) {
        forward_to_envoy(worker, trans, fid);
        goto send_reply;
    }

    /* first make sure it's not a non-empty directory */
    if (fid->claim->info == NULL) {
        fid->claim->info =
            object_stat(worker, fid->claim->oid,
                    filename(fid->claim->pathname));
    }
    info = fid->claim->info;
    failif((info->mode & DMDIR) && !dir_is_empty(worker, fid, info), ENOTEMPTY);

    /* TODO: implement this */
    /* do we have local control of the parent directory? */
    /* if not, give up ownership of this file and repeat the request remotely */

    /* check permission in the parent directory */

    /* remove it */
    /*res = dir_remove_entry(worker, fid, dirinfo, filename(fid->pathname));*/
    res = -1;

    /* delete the storage object? */

    failif(res < 0, ENOENT);

    send_reply:
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

    require_fid(fid);

    /* handle forwarding */
    if (fid->isremote) {
        forward_to_envoy(worker, trans, fid);
        goto send_reply;
    }

    if (fid->claim->info == NULL) {
        fid->claim->info =
            object_stat(worker, fid->claim->oid,
                    filename(fid->claim->pathname));
    }
    res->stat = fid->claim->info;

    send_reply:
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
    struct p9stat *info = NULL;
    struct p9stat *change = p9stat_new();

    require_fid(fid);

    if (fid->claim->info == NULL) {
        fid->claim->info =
            object_stat(worker, fid->claim->oid,
                    filename(fid->claim->pathname));
    }
    info = fid->claim->info;

    /* mode */
    if (req->stat->mode != ~(u32) 0) {
        failif(strcmp(fid->user, info->uid) &&
                !isgroupleader(fid->user, info->gid), EPERM);
        change->mode = req->stat->mode;
    }

    /* mtime */
    if (req->stat->mtime != ~(u32) 0) {
        failif(strcmp(fid->user, info->uid) &&
                !isgroupleader(fid->user, info->gid), EPERM);
        change->mtime = req->stat->mtime;
    }

    /* length */
    if (req->stat->length != ~(u64) 0) {
        failif(!has_permission(fid->user, info, 0222), EPERM);
        failif(info->mode & DMDIR, EACCES);
        change->length = req->stat->length;
    }

    /* extension */
    if (!emptystring(req->stat->extension)) {
        /* no extension changes allowed? */
        failif(1, EACCES);
    }

    /* rename */
    if (!emptystring(req->stat->name) &&
            strcmp(filename(fid->pathname), req->stat->name))
    {
        failif(strchr(req->stat->name, '/'), EINVAL);
        /* TODO: implement this */
        failif(1, ENOTSUP);

        fid->pathname = concatname(dirname(fid->pathname), req->stat->name);
    }

    object_wstat(worker, fid->claim->oid, change);
    fid->claim->info = NULL;

    send_reply(trans);
}
