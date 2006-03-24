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
#include "worker.h"
#include "oid.h"

/* TODO: release lock on blocking operations */

/* generate an error response with Unix errno errnum */
static void rerror(Worker *w, Message *m, char *file, int line) {
    m->id = RERROR;
    m->msg.rerror.errnum = w->errnum;
    m->msg.rerror.ename = stringcopy(strerror(w->errnum));
    fprintf(stderr, "error #%u: %s (%s line %d)\n",
            (u32) w->errnum, m->msg.rerror.ename, file, line);
}

#define failif(p,e) do { \
    if (p) { \
        worker->errnum = e; \
        rerror(worker, trans->out, __FILE__, __LINE__); \
            send_reply(trans); \
            return; \
    } \
} while(0)

#define guard(f) do { \
    if ((f) < 0) { \
        worker->errnum = errno; \
        rerror(worker, trans->out, __FILE__, __LINE__); \
            send_reply(trans); \
            return; \
    } \
} while(0)

/*****************************************************************************/

void handle_tsreserve(Worker *worker, Transaction *trans) {
    struct Rsreserve *res = &trans->out->msg.rsreserve;

    failif(oid_reserve_block(&res->firstoid, &res->count), ENOMEM);

    send_reply(trans);
}

void handle_tscreate(Worker *worker, Transaction *trans) {
    struct Tscreate *req = &trans->in->msg.tscreate;
    struct Rscreate *res = &trans->out->msg.rscreate;
    int length;

    length = oid_create(worker, req->oid, req->mode, req->ctime, req->uid, req->gid,
            req->extension);
    failif(length < 0, ENOMEM);

    res->qid = makeqid(req->mode, req->mtime, (u64) length, req->oid);

    send_reply(trans);
}

void handle_tsclone(Worker *worker, Transaction *trans) {
    struct Tsclone *req = &trans->in->msg.tsclone;

    failif(oid_clone(worker, req->oid, req->newoid) < 0, ENOMEM);

    send_reply(trans);
}

void handle_tsread(Worker *worker, Transaction *trans) {
    struct Tsread *req = &trans->in->msg.tsread;
    struct Rsread *res = &trans->out->msg.rsread;
    Openfile *file;
    /* struct utimbuf buf; */

    /* make sure the requested data is small enough to transmit */
    failif(req->count > trans->conn->maxSize - RSREAD_HEADER, EMSGSIZE);

    /* get a handle to the open file */
    failif((file = oid_get_openfile(worker, req->oid)) == NULL, ENOENT);

    if (lseek(file->fd, req->offset, SEEK_SET) != req->offset) {
        guard(-1);
    }

    res->data = GC_MALLOC_ATOMIC(req->count);
    assert(res->data != NULL);

    res->count = read(file->fd, res->data, req->count);

    guard(res->count >= 0);

    /* set the atime */
    /* buf.actime = req->atime;
    buf.modtime = 0;
    guard(oid_set_times(worker, req->oid, &buf)); */

    send_reply(trans);
}

void handle_tswrite(Worker *worker, Transaction *trans) {
    struct Tswrite *req = &trans->in->msg.tswrite;
    struct Rswrite *res = &trans->out->msg.rswrite;
    Openfile *file;
    struct utimbuf buf;

    /* get a handle to the open file */
    failif((file = oid_get_openfile(worker, req->oid)) == NULL, ENOENT);

    if (lseek(file->fd, req->offset, SEEK_SET) != req->offset) {
        guard(-1);
    }

    res->count = write(file->fd, req->data, req->count);

    guard(res->count >= 0);

    /* set the mtime */
    buf.actime = req->mtime;
    buf.modtime = req->mtime;
    guard(oid_set_times(worker, req->oid, &buf));

    send_reply(trans);
}

void handle_tsstat(Worker *worker, Transaction *trans) {
    struct Tsstat *req = &trans->in->msg.tsstat;
    struct Rsstat *res = &trans->out->msg.rsstat;

    res->stat = oid_stat(worker, req->oid);
    failif(res->stat == NULL, ENOENT);

    send_reply(trans);
}

void handle_tswstat(Worker *worker, Transaction *trans) {
    struct Tswstat *req = &trans->in->msg.tswstat;

    failif(oid_wstat(worker, req->oid, req->stat) < 0, ENOENT);

    send_reply(trans);
}
