#ifndef _OID_H_
#define _OID_H_

#include "types.h"
#include "9p.h"
#include "list.h"
#include "lru.h"

/* filenames:
 *  18 d0755 username groupnam
 *  19 f0644 username groupnam
 */

enum oid_type {
    OID_FILE,
    OID_DIR,
    OID_LINK,
};

struct oid {
    u64 oid;
    int fd;
    enum oid_type type;
    u32 mode;
    char *username;
    char *groupname;
};

struct objectdir {
    u64 start;
    struct oid **oids;
};

Lru *oid_init_lru(void);
List *oid_to_path(u64 oid);

#endif
