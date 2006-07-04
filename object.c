#include <assert.h>
#include <stdlib.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "transaction.h"
#include "util.h"
#include "config.h"
#include "object.h"
#include "dispatch.h"
#include "worker.h"

/* Operations on storage objects.
 * These functions allow simple calls to the object storage service.  They handle
 * local caching, find storage servers based on OID, and handle replication.
 */

/* pool of reserved oids */
static u64 object_reserve_next = ~ (u64) 0;
static u32 object_reserve_remaining = 0;

u64 object_reserve_oid(Worker *worker) {
    assert(storage_server_count > 0);

    /* do we need to request a fresh batch of oids? */
    if (object_reserve_remaining == 0) {
        Transaction *trans;
        struct Rsreserve *res;

        /* the first storage server is considered the master */
        trans = trans_new(storage_servers[0], NULL, message_new());
        trans->out->tag = ALLOCTAG;
        trans->out->id = TSRESERVE;

        send_request(trans);
        res = &trans->in->msg.rsreserve;

        object_reserve_next = res->firstoid;
        object_reserve_remaining = res->count;
    }

    object_reserve_remaining--;
    return object_reserve_next++;
}

struct qid object_create(Worker *worker, u64 oid, u32 mode, u32 ctime,
        char *uid, char *gid, char *extension)
{
    List *requests = NULL;
    Transaction *trans;
    struct Rscreate *res;
    int i;

    for (i = 0; i < storage_server_count; i++) {
        trans = trans_new(storage_servers[i], NULL, message_new());
        trans->out->tag = ALLOCTAG;
        trans->out->id = TSCREATE;
        set_tscreate(trans->out, oid, mode, ctime, uid, gid, extension);
        requests = cons(trans, requests);
    }

    /* send request to all storage servers and wait for all to respond */
    send_requests(requests);

    assert(!null(requests));
    trans = car(requests);
    res = &trans->in->msg.rscreate;

    /* make sure they all succeeded */
    while (!null(requests)) {
        trans = car(requests);
        assert(trans->in != NULL && trans->in->id == RSCREATE);
        requests = cdr(requests);
    }

    return res->qid;
}

void object_clone(Worker *worker, u64 oid, u64 newoid) {
    List *requests = NULL;
    Transaction *trans;
    int i;

    for (i = 0; i < storage_server_count; i++) {
        trans = trans_new(storage_servers[i], NULL, message_new());
        trans->out->tag = ALLOCTAG;
        trans->out->id = TSCLONE;
        set_tsclone(trans->out, oid, newoid);
        requests = cons(trans, requests);
    }

    /* send request to all storage servers and wait for all to respond */
    send_requests(requests);

    /* make sure they all succeeded */
    while (!null(requests)) {
        trans = car(requests);
        assert(trans->in != NULL && trans->in->id == RSCLONE);
        requests = cdr(requests);
    }
}

void *object_read(Worker *worker, u64 oid, u32 atime, u64 offset, u32 count,
        u32 *bytesread, u8 **data)
{
    Transaction *trans;
    struct Rsread *res;

    /* send the request to one randomly chosen storage server */
    trans = trans_new(storage_servers[randInt(storage_server_count)], NULL,
            message_new());
    trans->out->tag = ALLOCTAG;
    trans->out->id = TSREAD;
    set_tsread(trans->out, oid, atime, offset, count);

    send_request(trans);

    assert(trans->in != NULL && trans->in->id == RSREAD);
    res = &trans->in->msg.rsread;

    *bytesread = res->count;
    *data = res->data;
    return trans->in->raw;
}

u32 object_write(Worker *worker, u64 oid, u32 mtime, u64 offset,
        u32 count, u8 *data, void *raw)
{
    List *requests = NULL;
    Transaction *trans;
    struct Rswrite *res;
    int i;

    /* we need new raw buffers for storage_server_count > 1 */
    assert(storage_server_count == 1);
    for (i = 0; i < storage_server_count; i++) {
        trans = trans_new(storage_servers[i], NULL, message_new());
        trans->out->raw = raw;
        trans->out->tag = ALLOCTAG;
        trans->out->id = TSWRITE;
        set_tswrite(trans->out, mtime, offset, count, data, oid);
        requests = cons(trans, requests);
    }

    /* send request to all storage servers and wait for all to respond */
    send_requests(requests);

    assert(!null(requests));
    trans = car(requests);
    res = &trans->in->msg.rswrite;

    /* make sure they all succeeded */
    while (!null(requests)) {
        trans = car(requests);
        assert(trans->in != NULL && trans->in->id == RSWRITE);
        assert(trans->in->msg.rswrite.count == res->count);
        requests = cdr(requests);
    }

    return res->count;
}

struct p9stat *object_stat(Worker *worker, u64 oid, char *filename) {
    Transaction *trans;
    struct Rsstat *res;

    /* send the request to one randomly chosen storage server */
    trans = trans_new(storage_servers[randInt(storage_server_count)], NULL,
            message_new());
    trans->out->tag = ALLOCTAG;
    trans->out->id = TSSTAT;
    set_tsstat(trans->out, oid);

    send_request(trans);

    assert(trans->in != NULL && trans->in->id == RSSTAT);
    res = &trans->in->msg.rsstat;

    /* insert the filename supplied by the caller */
    res->stat->name = filename;

    return res->stat;
}

void object_wstat(Worker *worker, u64 oid, struct p9stat *info) {
    List *requests = NULL;
    Transaction *trans;
    int i;

    for (i = 0; i < storage_server_count; i++) {
        trans = trans_new(storage_servers[i], NULL, message_new());
        trans->out->tag = ALLOCTAG;
        trans->out->id = TSWSTAT;
        set_tswstat(trans->out, oid, info);
        requests = cons(trans, requests);
    }

    /* send request to all storage servers and wait for all to respond */
    send_requests(requests);

    /* make sure they all succeeded */
    while (!null(requests)) {
        trans = car(requests);
        assert(trans->in != NULL && trans->in->id == RSWSTAT);
        requests = cdr(requests);
    }
}
