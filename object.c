#include <stdlib.h>
#include "types.h"
#include "9p.h"
#include "util.h"
#include "object.h"
#include "worker.h"

/* Operations on storage objects.
 * These functions allow simple calls to the object storage service.  They handle
 * local caching, find storage servers based on OID, and handle replication.
 */

u64 object_reserve_oid(Worker *worker) {
    return 0;
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
