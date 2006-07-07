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
};

/* high-level functions */

void dir_clone(u32 count, u8 *data);
u32 dir_read(Worker *worker, Fid *fid, u32 size, u8 *data);
/* returns 0 on success, -1 if the file already exists */
int dir_create_entry(Worker *worker, Claim *dir, char *name, u64 oid, int cow);
/* returns 0 on success, -1 if not found */
int dir_remove_entry(Worker *worker, Claim *dir, char *name);
/* scan an entire directory and create claim for a specific target file */
Claim *dir_find_claim(Worker *worker, Claim *dir, char *name);
/* check if a directory is empty */
int dir_is_empty(Worker *worker, Claim *dir);
int dir_rename(Worker *worker, Claim *dir, char *oldname, char *newname);
u64 dir_change_oid(Worker *worker, Claim *dir, char *name,
        u64 oid, int cow);

#endif
