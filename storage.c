#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
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
#include "disk.h"

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

    failif(disk_reserve_block(&res->firstoid, &res->count), ENOMEM);

    send_reply(trans);
}

void handle_tscreate(Worker *worker, Transaction *trans) {
    struct Tscreate *req = &trans->in->msg.tscreate;
    struct Rscreate *res = &trans->out->msg.rscreate;
    int length;

    length = disk_create(worker, req->oid, req->mode, req->time, req->uid,
            req->gid, req->extension);
    failif(length < 0, ENOMEM);

    res->qid = makeqid(req->mode, req->time, (u64) length, req->oid);

    send_reply(trans);
}

void handle_tsclone(Worker *worker, Transaction *trans) {
    struct Tsclone *req = &trans->in->msg.tsclone;

    failif(disk_clone(worker, req->oid, req->newoid) < 0, ENOENT);

    send_reply(trans);
}

void handle_tsread(Worker *worker, Transaction *trans) {
    struct Tsread *req = &trans->in->msg.tsread;
    struct Rsread *res = &trans->out->msg.rsread;
    int len;

    /* make sure the requested data is small enough to transmit */
    failif(req->count > trans->conn->maxSize - RSREAD_HEADER, EMSGSIZE);

    /* use the raw message buffer */
    trans->out->raw = raw_new();
    res->data = trans->out->raw + RSREAD_DATA_OFFSET;

    worker_cleanup_add(worker, LOCK_RAW, trans->out->raw);
    len = disk_read(worker, req->oid, req->time, req->offset, req->count,
            res->data);
    worker_cleanup_remove(worker, LOCK_RAW, trans->out->raw);

    if (len < 0) {
        raw_delete(trans->out->raw);
        trans->out->raw = NULL;
    }

    failif(len < 0, -len);

    res->count = (u32) len;

    send_reply(trans);
}

void handle_tswrite(Worker *worker, Transaction *trans) {
    struct Tswrite *req = &trans->in->msg.tswrite;
    struct Rswrite *res = &trans->out->msg.rswrite;
    int len;

    len = disk_write(worker, req->oid, req->time, req->offset, req->count,
            req->data);

    raw_delete(trans->in->raw);
    trans->in->raw = NULL;

    failif(len < 0, -len);

    res->count = (u32) len;

    send_reply(trans);
}

void handle_tsstat(Worker *worker, Transaction *trans) {
    struct Tsstat *req = &trans->in->msg.tsstat;
    struct Rsstat *res = &trans->out->msg.rsstat;

    res->stat = disk_stat(worker, req->oid);
    failif(res->stat == NULL, ENOENT);

    send_reply(trans);
}

void handle_tswstat(Worker *worker, Transaction *trans) {
    struct Tswstat *req = &trans->in->msg.tswstat;

    failif(disk_wstat(worker, req->oid, req->stat) < 0, ENOENT);

    send_reply(trans);
}

void handle_tsdelete(Worker *worker, Transaction *trans) {
    struct Tsdelete *req = &trans->in->msg.tsdelete;
    int res = disk_delete(worker, req->oid);

    failif(res < 0, -res);

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
                    addr_to_string(storage_addresses[i]));
        }

        conn = conn_connect_to_storage(storage_addresses[i]);
        if (conn == NULL) {
            fprintf(stderr, "Failed to connect to storage server %d\n", i);
            assert(0);
        }

        storage_servers[i] = conn;
    }
}
