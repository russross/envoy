#ifndef _DIR_H_
#define _DIR_H_

#include "types.h"
#include "9p.h"
#include "list.h"
#include "util.h"
#include "worker.h"

#define DIR_COW_OFFSET 8

struct direntry {
    u32 offset;
    u64 oid;
    u8 cow;
    char *filename;
};

List *dir_get_entries(u32 count, u8 *data);
void dir_clone(u32 count, u8 *data);
int dir_will_fit(u32 count, u8 *data, char *filename);
u8 *dir_add_entry(u32 count, u8 *data, u64 oid, char *filename, u8 cow);
u8 *dir_new_block(u32 *count, u64 oid, char *filename, u8 cow);

/* high-level functions */

/* scan an entire directory and return a list of direnty structs */
List *dir_scan(Worker *worker, u64 oid);


#endif
