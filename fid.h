#ifndef _FID_H_
#define _FID_H_

#include <pthread.h>
#include <gc/gc.h>
#include <dirent.h>
#include "types.h"
#include "9p.h"
#include "connection.h"

/* active files */
enum fid_status {
    STATUS_CLOSED,
    STATUS_OPEN_FILE,
    STATUS_OPEN_DIR,
    STATUS_OPEN_SYMLINK,
    STATUS_OPEN_LINK,
    STATUS_OPEN_DEVICE,
};

enum fid_access {
    ACCESS_WRITEABLE,
    ACCESS_READONLY,
    ACCESS_COW,
};

struct fid {
    pthread_cond_t *wait;

    Claim *claim;

    /* the client-visible fid */
    u32 fid;
    /* the client-visible oid--we keep the old one in the case of CoW */
    u64 client_oid;
    /* the username of the client */
    char *uname;
    /* the file status as seen by the client */
    enum fid_status status;
    /* the mode used to open this file */
    int omode;

    /* the number of bytes returned so far in the current directory read */
    u64 readdir_cookie;
    /* the actual offset into the directory */
    u64 readdir_offset;
    /* the current block when in a readdir sequence */
    List *readdir_current_block;
    /* next dir entry */
    struct p9stat *readdir_next;
};

int fid_insert_new(Connection *conn, u32 fid, char *uname, char *path);
Fid *fid_lookup(Connection *conn, u32 fid);
Fid *fid_lookup_remove(Connection *conn, u32 fid);

u32 fid_hash(const Fid *fid);
int fid_cmp(const Fid *a, const Fid *b);

#endif
