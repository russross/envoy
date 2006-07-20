#ifndef _FID_H_
#define _FID_H_

#include <unistd.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "vector.h"
#include "connection.h"
#include "worker.h"
#include "dir.h"
#include "claim.h"

/* active files */
enum fid_status {
    STATUS_UNOPENNED,
    STATUS_OPEN_FILE,
    STATUS_OPEN_DIR,
};

struct fid {
    /* for in-flight operations on this file through this fid */
    Worker *lock;

    /* the client-visible fid */
    u32 fid;
    /* the full pathname */
    char *pathname;
    /* the username of the client */
    char *user;
    /* the file status as seen by the client */
    enum fid_status status;
    /* the mode used to open this file */
    int omode;
    /* the number of bytes returned so far in the current directory read */
    u64 readdir_cookie;

    Address *addr;
    int isremote;

    /* local fields */

    /* handle to the object */
    Claim *claim;
    /* state for readdir operations */
    struct dir_read_env *readdir_env;

    /* remote fields */

    /* address of the remote envoy */
    Address *raddr;
    /* fid seen by the remote envoy */
    u32 rfid;
};

void fid_insert_local(Connection *conn, u32 fid, char *user, Claim *claim);
void fid_insert_remote(Connection *conn, u32 fid, char *pathname, char *user,
        Address *raddr, u32 rfid);
void fid_update_remote(Fid *fid, char *pathname, Address *raddr, u32 rfid);
void fid_update_local(Fid *fid, Claim *claim);
int fid_cmp(const Fid *a, const Fid *b);
u32 fid_hash(const Fid *fid);
Fid *fid_lookup(Connection *conn, u32 fid);
void fid_remove(Connection *conn, u32 fid);
u32 fid_reserve_remote(Worker *worker);
void fid_release_remote(u32 fid);
void fid_set_remote(u32 rfid, Fid *fid);
Fid *fid_get_remote(u32 rfid);

enum claim_access fid_access_child(enum claim_access access, int cowlink);

void fid_state_init(void);

extern Vector *fid_remote_vector;
extern List *fid_deleted_list;

#endif
