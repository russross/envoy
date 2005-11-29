#ifndef _OID_H_
#define _OID_H_

#include "types.h"
#include "9p.h"
#include "util.h"
#include "lru.h"

/* filenames:
 *  18 d0755 username groupnam
 *  19 f0644 username groupnam
 */

#define OBJECT_DIR_MODE 0755

struct fd_wrapper {
    int refcount;
    int fd;
};

enum oid_type {
    OID_FILE,
    OID_DIR,
    OID_LINK,
};

struct objectdir {
    u64 start;
    char *dirname;
    char **filenames;
};

Lru *oid_init_dir_lru(void);
Lru *oid_init_fd_lru(void);
u64 oid_find_next_available(void);
int oid_reserve_block(u64 *oid, int *count);
struct fd_wrapper *oid_add_fd_wrapper(u64 oid, int fd);
struct fd_wrapper *oid_get_open_fd_wrapper(u64 oid);
struct objectdir *oid_read_dir(u64 start);

struct p9stat *oid_stat(u64 oid);
int oid_wstat(u64 oid, struct p9stat *info);
int oid_create(u64 oid, struct p9stat *info);

#endif
