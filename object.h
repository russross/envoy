#ifndef _OBJECT_H_
#define _OBJECT_H_

#include "types.h"
#include "9p.h"
#include "util.h"
#include "worker.h"

/* stubs for storage calls */

u64 object_reserve_oid(Worker *worker);
struct qid object_create(Worker *worker, u64 oid, u32 mode,
        u32 ctime, char *uid, char *gid, char *extension);
void object_clone(Worker *worker, u64 oid, u64 newoid);
void *object_read(Worker *worker, u64 oid, u32 atime, u64 offset, u32 count,
        u32 *bytesread, u8 **data);
u32 object_write(Worker *worker, u64 oid, u32 mtime,
        u64 offset, u32 count, u8 *data, void *raw);
struct p9stat *object_stat(Worker *worker, u64 oid,
        char *filename);
void object_wstat(Worker *worker, u64 oid, struct p9stat *info);
void object_delete(Worker *worker, u64 oid);
void object_fetch(Worker *worker, u64 oid, struct p9stat *info);

void object_cache_validate(u64 oid);
void object_cache_invalidate(u64 oid);
void object_cache_invalidate_all(void);
int object_cache_isvalid(u64 oid);

void object_state_init(void);

#endif
