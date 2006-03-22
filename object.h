#ifndef _OBJECT_H_
#define _OBJECT_H_

#include "foo.h"

u64 object_reserve_oid(Worker *worker);
void object_create(Worker *worker, u64 oid, struct p9stat *info);
void object_clone(Worker *worker, u64 oid, u64 newoid);
void *object_read(Worker *worker, u64 oid, u32 atime, u64 offset, u32 count, u32 *bytesread);
u32 object_write(Worker *worker, u64 oid, u32 mtime, u64 offset, u32 count, void *data);
struct p9stat *object_stat(Worker *worker, u64 oid);
void object_wstat(Worker *worker, u64 oid, struct p9stat *info);

#endif
