#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <utime.h>
#include <sys/socket.h>
#include <errno.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "connection.h"
#include "transaction.h"
#include "util.h"
#include "config.h"
#include "storage.h"
#include "dispatch.h"
#include "worker.h"
#include "oid.h"

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

    length = oid_create(worker, req->oid, req->mode, req->time, req->uid,
            req->gid, req->extension);
    failif(length < 0, ENOMEM);

    res->qid = makeqid(req->mode, req->time, (u64) length, req->oid);

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
    int len;
    /* struct utimbuf buf; */

    /* make sure the requested data is small enough to transmit */
    failif(req->count > trans->conn->maxSize - RSREAD_HEADER, EMSGSIZE);

    /* get a handle to the open file */
    failif((file = oid_get_openfile(worker, req->oid)) == NULL, ENOENT);

    guard(lseek(file->fd, req->offset, SEEK_SET));

    /* use the raw message buffer */
    trans->out->raw = raw_new();
    res->data = trans->out->raw + RSREAD_DATA_OFFSET;

    unlock();
    len = read(file->fd, res->data, req->count);
    lock();

    if (len < 0) {
        raw_delete(trans->out->raw);
        trans->out->raw = NULL;
    }
    guard(len);
    res->count = (u32) len;

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
    int len;

    /* get a handle to the open file */
    failif((file = oid_get_openfile(worker, req->oid)) == NULL, ENOENT);

    guard(lseek(file->fd, req->offset, SEEK_SET));

    unlock();
    len = write(file->fd, req->data, req->count);
    lock();

    raw_delete(trans->in->raw);

    trans->in->raw = NULL;
    guard(len);
    res->count = (u32) len;

    /* set the mtime */
    buf.actime = req->time;
    buf.modtime = req->time;
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

void storage_server_connection_init(void) {
    int i;

    storage_servers = GC_MALLOC(sizeof(Connection *) * storage_server_count);
    assert(storage_servers != NULL);

    for (i = 0; i < storage_server_count; i++) {
        Connection *conn;

        if (DEBUG_VERBOSE) {
            printf("storage server %d: %s\n", i,
                    address_to_string(storage_addresses[i]));
        }

        conn = conn_connect_to_storage(storage_addresses[i]);
        if (conn == NULL) {
            fprintf(stderr, "Failed to connect to storage server %d\n", i);
            assert(0);
        }

        storage_servers[i] = conn;
    }
}
