#ifndef _DIR_H_
#define _DIR_H_

#include "types.h"
#include "9p.h"
#include "list.h"
#include "util.h"

#define DIR_COW_OFFSET 8

struct direntry {
    int offset;
    u64 oid;
    u8 cow;
    char *filename;
};

List *dir_get_entries(u32 count, u8 *data);
void dir_clone(u32 count, u8 *data);

#endif
