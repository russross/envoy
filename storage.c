#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <utime.h>
#include <errno.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "transaction.h"
#include "util.h"
#include "storage.h"
#include "dispatch.h"
#include "oid.h"

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

/*****************************************************************************/

void handle_tsreserve(Transaction *trans) {
    struct Rsreserve *res = &trans->out->msg.rsreserve;

    failif(oid_reserve_block(&res->firstoid, &res->count), ENOMEM);

    send_reply(trans);
}

void handle_tscreate(Transaction *trans) {
    struct Tscreate *req = &trans->in->msg.tscreate;
    int status;

    status = oid_create(req->oid, req->stat);
    failif(status != 0, status);

    send_reply(trans);
}

void handle_tsclone(Transaction *trans) {
    //struct Tsclone *req = &trans->in->msg.tsclone;
    //struct Rsclone *res = &trans->out->msg.rsclone;
}

void handle_tsread(Transaction *trans) {
    struct Tsread *req = &trans->in->msg.tsread;
    struct Rsread *res = &trans->out->msg.rsread;
    struct fd_wrapper *wrapper;
    struct utimbuf buf;

    /* make sure the requested data is small enough to transmit */
    failif(req->count > trans->conn->maxSize - RSREAD_HEADER, EMSGSIZE);

    /* get a handle to the open file */
    failif((wrapper = oid_get_open_fd_wrapper(req->oid)) == NULL, ENOENT);

    if (lseek(wrapper->fd, req->offset, SEEK_SET) != req->offset) {
        wrapper->refcount--;
        guard(-1);
    }

    res->data = GC_MALLOC_ATOMIC(req->count);
    assert(res->data != NULL);

    res->count = read(wrapper->fd, res->data, req->count);

    wrapper->refcount--;
    guard(res->count >= 0);

    /* set the atime */
    buf.actime = req->atime;
    buf.modtime = 0;
    guard(oid_set_times(req->oid, &buf));

    send_reply(trans);
}

void handle_tswrite(Transaction *trans) {
    struct Tswrite *req = &trans->in->msg.tswrite;
    struct Rswrite *res = &trans->out->msg.rswrite;
    struct fd_wrapper *wrapper;
    struct utimbuf buf;

    /* get a handle to the open file */
    failif((wrapper = oid_get_open_fd_wrapper(req->oid)) == NULL, ENOENT);

    if (lseek(wrapper->fd, req->offset, SEEK_SET) != req->offset) {
        wrapper->refcount--;
        guard(-1);
    }

    res->count = write(wrapper->fd, req->data, req->count);

    wrapper->refcount--;
    guard(res->count >= 0);

    /* set the mtime */
    buf.actime = 0;
    buf.modtime = req->mtime;
    guard(oid_set_times(req->oid, &buf));

    send_reply(trans);
}

void handle_tsstat(Transaction *trans) {
    struct Tsstat *req = &trans->in->msg.tsstat;
    struct Rsstat *res = &trans->out->msg.rsstat;

    res->stat = oid_stat(req->oid);
    failif(res->stat == NULL, ENOENT);

    send_reply(trans);
}

void handle_tswstat(Transaction *trans) {
    struct Tswstat *req = &trans->in->msg.tswstat;

    failif(oid_wstat(req->oid, req->stat) < 0, ENOENT);

    send_reply(trans);
}
