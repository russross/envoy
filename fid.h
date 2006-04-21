#ifndef _FID_H_
#define _FID_H_

#include <pthread.h>
#include <gc/gc.h>
#include <unistd.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "connection.h"
#include "claim.h"

/* active files */
enum fid_status {
    STATUS_CLOSED,
    STATUS_OPEN_FILE,
    STATUS_OPEN_DIR,
    STATUS_OPEN_SYMLINK,
    STATUS_OPEN_LINK,
    STATUS_OPEN_DEVICE,
};

struct fid {
    /* for operations on this file through this fid */
    pthread_cond_t *wait;

    Claim *claim;

    /* the client-visible fid */
    u32 fid;
    /* the username of the client */
    char *user;
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

int fid_insert_new(Connection *conn, u32 fid, char *user, Claim *claim);
Fid *fid_lookup(Connection *conn, u32 fid);
Fid *fid_lookup_remove(Connection *conn, u32 fid);

u32 fid_hash(const Fid *fid);
int fid_cmp(const Fid *a, const Fid *b);
enum claim_access fid_access_child(enum claim_access access, int cowlink);

#endif
