#ifndef _DISK_H_
#define _DISK_H_

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

u64 disk_find_next_available(void);
Openfile *disk_add_openfile(u64 oid, int fd);
Openfile *disk_get_openfile(Worker *worker, u64 oid);

int disk_reserve_block(u64 *oid, u32 *count);
struct p9stat *disk_stat(Worker *worker, u64 oid);
int disk_wstat(Worker *worker, u64 oid, struct p9stat *info);
int disk_create(Worker *worker, u64 oid, u32 mode, u32 ctime, char *uid,
        char *gid, char *extension);
int disk_set_times(Worker *worker, u64 oid, struct utimbuf *buf);
int disk_clone(Worker *worker, u64 oldoid, u64 newoid);
int disk_delete(Worker *worker, u64 oid);
int disk_write(Worker *worker, u64 oid, u32 time, u64 offset, u32 count,
        u8 *data);
int disk_read(Worker *worker, u64 oid, u32 time, u64 offset, u32 count,
        u8 *data);

void disk_state_init_storage(void);
void disk_state_init_envoy(void);

#endif
