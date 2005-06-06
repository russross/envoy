#ifndef _STATE_H_
#define _STATE_H_

#include <netinet/in.h>
#include <dirent.h>
#include "9p.h"
#include "util.h"

#define CONN_HASHTABLE_SIZE 64
#define TRANS_HASHTABLE_SIZE 64
#define FID_HASHTABLE_SIZE 64

void state_init(void);

struct message *message_new(void);
struct transaction *transaction_new(void);

enum conn_type {
    CONN_CLIENT_IN,
    CONN_ENVOY_IN,
    CONN_ENVOY_OUT,
    CONN_STORAGE_OUT
};

struct connection {
    int fd;
    enum conn_type type;
    struct sockaddr_in *addr;
    int maxSize;
    struct hashtable *fid_2_fid;
    struct hashtable *tag_2_trans;
};

void                    conn_insert_new(int fd,
                                        enum conn_type type,
                                        struct sockaddr_in *addr,
                                        int maxSize);
struct connection *     conn_lookup_fd(int fd);
struct connection *     conn_lookup_addr(struct sockaddr_in *addr);
void                    conn_remove(struct connection *conn);

struct transaction {
    struct transaction * (*handler)(struct transaction *trans);

    struct connection *conn;
    struct message *in;
    struct message *out;

    struct transaction *prev;
};

void                    trans_insert(struct transaction *trans);
struct transaction *    trans_lookup_remove(struct connection *conn, u16 tag);

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

int                     fid_insert_new(struct connection *conn,
                                       u32 fid, char *uname,
                                       char *path);
struct fid *            fid_lookup(struct connection *conn, u32 fid);
struct fid *            fid_lookup_remove(struct connection *conn, u32 fid);

void print_address(struct sockaddr_in *addr);
void state_dump(void);

#endif
