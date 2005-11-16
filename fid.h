#ifndef _FID_H_
#define _FID_H_

#include <dirent.h>
#include "9p.h"
#include "types.h"

/* active files */
enum fd_status {
    STATUS_CLOSED,
    STATUS_OPEN_FILE,
    STATUS_OPEN_DIR,
    STATUS_OPEN_SYMLINK,
    STATUS_OPEN_LINK,
    STATUS_OPEN_DEVICE,
};

struct fid {
    u32 fid;
    char *uname;
    char *path;
    int fd;
    DIR *dd;
    enum fd_status status;
    int omode;
    u64 offset;
    struct p9stat *next_dir_entry;
};

int fid_insert_new(Connection *conn, u32 fid, char *uname, char *path);
Fid *fid_lookup(Connection *conn, u32 fid);
Fid *fid_lookup_remove(Connection *conn, u32 fid);

#endif
