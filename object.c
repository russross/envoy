#include <assert.h>
#include <pthread.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "transaction.h"
#include "util.h"
#include "config.h"
#include "object.h"
#include "dispatch.h"
#include "worker.h"
#include "lru.h"
#include "oid.h"
#include "lease.h"

/* Operations on storage objects.
 * These functions allow simple calls to the object storage service.  They
 * handle local caching, find storage servers based on OID, and handle
 * replication. */

/* pool of reserved oids */
static u64 object_reserve_next;
static u32 object_reserve_remaining;
static pthread_cond_t *object_reserve_wait;
static Lru *object_cache_status;

void object_cache_validate(u64 oid, Lease *lease) {
    u64 *key = GC_NEW_ATOMIC(u64);
    assert(key != NULL);
    *key = oid;
    lru_add(object_cache_status, key, lease);
}

void object_cache_invalidate(u64 oid) {
    lru_remove(object_cache_status, &oid);
}

void object_cache_invalidate_lease(Lease *lease) {
    lru_remove_value(object_cache_status, lease);
}

int object_cache_isvalid(u64 oid) {
    return lru_get(object_cache_status, &oid) != NULL;
}

static void send_request_to_all(Transaction *trans) {
    List *requests = cons(trans, NULL);
    int i;

    for (i = 1; i < storage_server_count; i++) {
        Transaction *newtrans =
            trans_new(storage_servers[i], NULL, message_new());

        /* copy the whole mess over */
        memcpy(newtrans->out, trans->out, sizeof(Message));

        /* for tswrite, we need to copy the data payload as well */
        if (newtrans->out->raw != NULL) {
            struct Tswrite *req = &newtrans->out->msg.tswrite;

            assert(trans->out->id == TSWRITE);

            newtrans->out->raw = raw_new();
            req->data = newtrans->out->raw + TWRITE_DATA_OFFSET;
            memcpy(req->data, trans->out->raw + TWRITE_DATA_OFFSET, req->count);
        }

        requests = cons(newtrans, requests);
    }

    /* send request to all storage servers and wait for all to respond */
    send_requests(requests);

    /* make sure they all succeeded */
    while (!null(requests)) {
        trans = car(requests);
        assert(trans->in != NULL && trans->in->id == trans->out->id + 1);
        requests = cdr(requests);
    }
}

u64 object_reserve_oid(Worker *worker) {
    assert(storage_server_count > 0);

    /* is someone else in the process of requesting new oids? */
    while (object_reserve_wait != NULL)
        cond_wait(object_reserve_wait);

    /* do we need to request a fresh batch of oids? */
    if (object_reserve_remaining == 0) {
        /* the first storage server is considered the master */
        Transaction *trans = trans_new(storage_servers[0], NULL, message_new());
        struct Rsreserve *res;
        pthread_cond_t *wait;

        trans->out->tag = ALLOCTAG;
        trans->out->id = TSRESERVE;

        wait = object_reserve_wait = cond_new();
        send_request(trans);
        object_reserve_wait = NULL;
        cond_broadcast(wait);

        res = &trans->in->msg.rsreserve;

        object_reserve_next = res->firstoid;
        object_reserve_remaining = res->count;
    }

    object_reserve_remaining--;
    return object_reserve_next++;
}

struct qid object_create(Worker *worker, Lease *lease, u64 oid, u32 mode,
        u32 ctime, char *uid, char *gid, char *extension)
{
    Transaction *trans = trans_new(storage_servers[0], NULL, message_new());
    struct Rscreate *res;
    int len;

    /* create it in the cache */
    len = oid_create(worker, oid, mode, ctime, uid, gid, extension);
    assert(len >= 0);
    object_cache_validate(oid, lease);

    /* create it on the storage servers */
    trans->out->tag = ALLOCTAG;
    trans->out->id = TSCREATE;
    set_tscreate(trans->out, oid, mode, ctime, uid, gid, extension);

    send_request_to_all(trans);

    res = &trans->in->msg.rscreate;
    return res->qid;
}

void object_clone(Worker *worker, Lease *lease, u64 oid, u64 newoid) {
    Transaction *trans = trans_new(storage_servers[0], NULL, message_new());

    /* clone it locally if we have it in the cache */
    if (object_cache_isvalid(oid)) {
        int res = oid_clone(worker, oid, newoid);
        assert(res >= 0);
        object_cache_validate(newoid, lease);
    }

    trans->out->tag = ALLOCTAG;
    trans->out->id = TSCLONE;
    set_tsclone(trans->out, oid, newoid);

    send_request_to_all(trans);
}

void *object_read(Worker *worker, u64 oid, u32 atime, u64 offset, u32 count,
        u32 *bytesread, u8 **data)
{
    int i = randInt(storage_server_count);
    Transaction *trans = trans_new(storage_servers[i], NULL, message_new());
    struct Rsread *res;
    void *result;

    trans->out->tag = ALLOCTAG;
    trans->out->id = TSREAD;
    set_tsread(trans->out, oid, atime, offset, count);

    /* send the request to one randomly chosen storage server */
    send_request(trans);

    assert(trans->in != NULL && trans->in->id == RSREAD);
    res = &trans->in->msg.rsread;

    *bytesread = res->count;
    *data = res->data;
    result = trans->in->raw;
    trans->in->raw = NULL;
    return result;
}

u32 object_write(Worker *worker, u64 oid, u32 mtime, u64 offset,
        u32 count, u8 *data, void *raw)
{
    Transaction *trans = trans_new(storage_servers[0], NULL, message_new());
    struct Rswrite *res;

    /* write to the cache if it exists */
    if (object_cache_isvalid(oid)) {
        int len = oid_write(worker, oid, mtime, offset, count, data);
        assert(len > 0);
    }

    trans->out->raw = raw;
    trans->out->tag = ALLOCTAG;
    trans->out->id = TSWRITE;
    set_tswrite(trans->out, mtime, offset, count, data, oid);

    send_request_to_all(trans);

    res = &trans->in->msg.rswrite;
    return res->count;
}

struct p9stat *object_stat(Worker *worker, Lease *lease, u64 oid,
        char *filename)
{
    int i = randInt(storage_server_count);
    Transaction *trans = trans_new(storage_servers[i], NULL, message_new());
    struct Rsstat *res;
    struct p9stat *info;

    /* handle it from the cache if it exists */
    if (object_cache_isvalid(oid)) {
        info = oid_stat(worker, oid);
        info->name = filename;
        return info;
    }

    trans->out->tag = ALLOCTAG;
    trans->out->id = TSSTAT;
    set_tsstat(trans->out, oid);

    /* send the request to one randomly chosen storage server */
    send_request(trans);

    assert(trans->in != NULL && trans->in->id == RSSTAT);
    res = &trans->in->msg.rsstat;

    /* insert the filename supplied by the caller */
    res->stat->name = filename;

    /* check if we have a cache entry with matching stats */
    if ((info = oid_stat(worker, oid)) != NULL) {
        info->name = filename;

        /* if it's up-to-date, note it as a valid entry */
        if (!p9stat_cmp(info, res->stat))
            object_cache_validate(oid, lease);
    }

    return res->stat;
}

void object_wstat(Worker *worker, u64 oid, struct p9stat *info) {
    Transaction *trans = trans_new(storage_servers[0], NULL, message_new());

    /* update the cache if it exists */
    if (object_cache_isvalid(oid)) {
        int res = oid_wstat(worker, oid, info);
        assert(res >= 0);
    }

    trans->out->tag = ALLOCTAG;
    trans->out->id = TSWSTAT;
    set_tswstat(trans->out, oid, info);

    send_request_to_all(trans);
}

void object_delete(Worker *worker, u64 oid) {
    Transaction *trans = trans_new(storage_servers[0], NULL, message_new());
    int res;

    /* delete the cache entry if it exists */
    res = oid_delete(worker, oid);
    object_cache_invalidate(oid);
    if (res < 0) {
        /* no entry is okay for the cache */
        assert(-res == ENOENT);
    }

    trans->out->tag = ALLOCTAG;
    trans->out->id = TSDELETE;
    set_tsdelete(trans->out, oid);

    send_request_to_all(trans);
}

void object_fetch(Worker *worker, Lease *lease, u64 oid, struct p9stat *info) {
    int res;

    if (object_cache_isvalid(oid))
        return;

    /* delete any existing entry in the cache and fetch this one */
    res = oid_delete(worker, oid);
    assert(res >= 0 || -res == ENOENT);

    /* stripe the reads across the storage servers */

}

void object_state_init(void) {
    object_reserve_next = ~ (u64) 0;
    object_reserve_remaining = 0;
    object_reserve_wait = NULL;
    object_cache_status = lru_new(
            OBJECT_CACHE_STATE_SIZE,
            (Hashfunc) u64_hash,
            (Cmpfunc) u64_cmp,
            NULL,
            NULL);
}
