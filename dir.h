#ifndef _DIR_H_
#define _DIR_H_

#include "types.h"
#include "9p.h"
#include "list.h"
#include "fid.h"
#include "util.h"
#include "worker.h"
#include "claim.h"

#define DIR_COW_OFFSET 8
#define DIR_END_OFFSET 11

struct direntry {
    u32 offset;
    u64 oid;
    u8 cow;
    char *filename;
};

struct dir_read_env {
    struct p9stat *next;
    u64 offset;
    List *entries;
    int eof;
};

/* high-level functions */

void dir_clone(u32 count, u8 *data);
u32 dir_read(Worker *worker, Fid *fid, u32 size, u8 *data);
/* returns new OID, or NOOID if the file already exists */
u64 dir_create_entry(Worker *worker, Fid *fid, struct p9stat *dirinfo,
        char *name);
/* returns 0 on success, -1 if not found */
int dir_remove_entry(Worker *worker, Fid *fid, struct p9stat *dirinfo,
        char *name);
/* scan an entire directory and create claim for a specific target file */
Claim *dir_find_claim(Worker *worker, Claim *dir, char *name);
/* check if a directory is empty */
int dir_is_empty(Worker *worker, Fid *fid, struct p9stat *dirinfo);

#endif
