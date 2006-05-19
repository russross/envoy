#include <assert.h>
#include <stdlib.h>
#include "types.h"
#include "9p.h"
#include "transaction.h"
#include "util.h"
#include "config.h"
#include "state.h"
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
    struct qid qid;
    return qid;
}

void object_clone(Worker *worker, u64 oid, u64 newoid) {
}

void *object_read(Worker *worker, u64 oid, u32 atime, u64 offset, u32 count,
        u32 *bytesread)
{
    return NULL;
}

u32 object_write(Worker *worker, u64 oid, u32 mtime, u64 offset,
        u32 count, void *data)
{
    return 0;
}

struct p9stat *object_stat(Worker *worker, u64 oid, char *filename) {
    return NULL;
}

void object_wstat(Worker *worker, u64 oid, struct p9stat *info) {
}

/* a hint to load the given object into the local cache */
void object_prime_cache(Worker *worker, u64 oid) {
}
