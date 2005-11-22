#ifndef _OID_H_
#define _OID_H_

#include "types.h"
#include "9p.h"
#include "lru.h"

/* filenames:
 *  18 d0755 username groupnam
 *  19 f0644 username groupnam
 */

#define OBJECT_DIR_MODE 0755

enum oid_type {
    OID_FILE,
    OID_DIR,
    OID_LINK,
};

struct oid {
    u64 oid;
    int fd;
    u64 offset;
    enum oid_type type;
    u32 mode;
    char *username;
    char *groupname;
};

struct objectdir {
    u64 start;
    struct oid **oids;
};

Lru *oid_init_dir_lru(void);
Lru *oid_init_fd_lru(void);
u64 oid_find_next_available(void);
int oid_reserve_block(u64 *oid, int *count);

#endif
