#ifndef _OID_H_
#define _OID_H_

#include <utime.h>
#include "types.h"
#include "9p.h"
#include "util.h"
#include "worker.h"

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

struct openfile {
    Worker *lock;

    int fd;
};

struct objectdir {
    Worker *lock;

    u64 start;
    char *dirname;
    char **filenames;
};

u64 oid_find_next_available(void);
Openfile *oid_add_openfile(u64 oid, int fd);
Openfile *oid_get_openfile(Worker *worker, u64 oid);

int oid_reserve_block(u64 *oid, u32 *count);
struct p9stat *oid_stat(Worker *worker, u64 oid);
int oid_wstat(Worker *worker, u64 oid, struct p9stat *info);
int oid_create(Worker *worker, u64 oid, u32 mode, u32 ctime, char *uid,
        char *gid, char *extension);
int oid_set_times(Worker *worker, u64 oid, struct utimbuf *buf);
int oid_clone(Worker *worker, u64 oldoid, u64 newoid);
char *oid_delete(Worker *worker, u64 oid);

void oid_state_init_storage(void);
void oid_state_init_envoy(void);

#endif
