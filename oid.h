#ifndef _OID_H_
#define _OID_H_

#include <pthread.h>
#include <gc/gc.h>
#include <utime.h>
#include "types.h"
#include "9p.h"
#include "util.h"
#include "lru.h"

/* filenames:
 *  18 d0755 username groupnam
 *  19 f0644 username groupnam
 */

#define OBJECT_DIR_MODE 0755
#define OBJECT_MODE 0644
#define OBJECT_FILENAME_LENGTH 29
#define MAX_UID_LENGTH 8
#define MAX_GID_LENGTH 8
#define CLONE_BUFFER_SIZE 8192

struct oid_fd {
    pthread_cond_t *wait;
    
    int fd;
};

enum oid_type {
    OID_FILE,
    OID_DIR,
    OID_LINK,
};

struct oid_dir {
    pthread_cond_t *wait;

    u64 start;
    char *dirname;
    char **filenames;
};

Lru *oid_init_dir_lru(void);
Lru *oid_init_fd_lru(void);

u64 oid_find_next_available(void);
struct oid_fd *oid_add_fd(u64 oid, int fd);
struct oid_fd *oid_get_open_fd(u64 oid);
struct objectdir *oid_read_dir(u64 start);

int oid_reserve_block(u64 *oid, u32 *count);
struct p9stat *oid_stat(u64 oid);
int oid_wstat(u64 oid, struct p9stat *info);
int oid_create(u64 oid, struct p9stat *info);
int oid_set_times(u64 oid, struct utimbuf *buf);
int oid_clone(u64 oldoid, u64 newoid);

#endif
