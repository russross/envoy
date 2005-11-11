#ifndef _STATE_H_
#define _STATE_H_

#include <netinet/in.h>
#include <dirent.h>
#include <sys/select.h>
#include <pthread.h>
#include <gc/gc.h>
#include "9p.h"
#include "util.h"

#define CONN_HASHTABLE_SIZE 64
#define CONN_VECTOR_SIZE 64
#define TAG_VECTOR_SIZE 64
#define FID_VECTOR_SIZE 64
#define FORWARD_VECTOR_SIZE 64
#define GLOBAL_FORWARD_VECTOR_SIZE 64
#define HANDLES_INITIAL_SIZE 32
#define THREAD_LIFETIME 1024

/* handle sets */
struct handles {
    fd_set *set;
    int high;
};

void handles_add(struct handles *set, int handle);
void handles_remove(struct handles *set, int handle);
int handles_collect(struct handles *set, fd_set *rset, int high);
int handles_member(struct handles *set, fd_set *rset);

/* connections */
enum conn_type {
    CONN_CLIENT_IN,
    CONN_ENVOY_IN,
    CONN_ENVOY_OUT,
    CONN_STORAGE_OUT,
    CONN_UNKNOWN_IN
};

struct connection {
    int fd;
    enum conn_type type;
    struct sockaddr_in *addr;
    int maxSize;
    struct vector *fid_vector;
    struct vector *forward_vector;
    struct vector *tag_vector;
    struct cons *pending_writes;
    struct transaction *notag_trans;
};

struct connection *     conn_insert_new(int fd,
                                        enum conn_type type,
                                        struct sockaddr_in *addr,
                                        int maxSize);
struct connection *     conn_new_unopened(enum conn_type type,
                                          struct sockaddr_in *addr);
struct connection *     conn_lookup_fd(int fd);
struct connection *     conn_lookup_addr(struct sockaddr_in *addr);
struct transaction *    conn_get_pending_write(struct connection *conn);
int                     conn_has_pending_write(struct connection *conn);
void                    conn_queue_write(struct transaction *trans);
void                    conn_remove(struct connection *conn);

/* transactions */
struct transaction {
    pthread_cond_t *wait;

    struct connection *conn;
    struct message *in;
    struct message *out;
};

void                    trans_insert(struct transaction *trans);
struct transaction *    trans_lookup_remove(struct connection *conn, u16 tag);

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

int                     fid_insert_new(struct connection *conn,
                                       u32 fid, char *uname,
                                       char *path);
struct fid *            fid_lookup(struct connection *conn, u32 fid);
struct fid *            fid_lookup_remove(struct connection *conn, u32 fid);

struct forward {
    u32 fid;
    struct connection *rconn;
    u32 rfid;
};

u32 forward_create_new(struct connection *conn, u32 fid,
        struct connection *rconn);
struct forward *forward_lookup(struct connection *conn, u32 fid);
struct forward *forward_lookup_remove(struct connection *conn, u32 fid);

void print_address(struct sockaddr_in *addr);
int addr_cmp(const struct sockaddr_in *a, const struct sockaddr_in *b);
void state_dump(void);

/* persistent state */
struct state {
    pthread_mutex_t *biglock;
    struct vector *conn_vector;
    struct vector *forward_fids;
    struct hashtable *addr_2_conn;
    struct handles *handles_listen;
    struct handles *handles_read;
    struct handles *handles_write;
    struct sockaddr_in *my_address;
    struct map *map;
    struct cons *error_queue;
    struct cons *thread_pool;
    pthread_cond_t *wait_workers;
    int active_worker_count;
};

extern struct state *state;
void state_init(void);

/* worker threads */
struct worker_thread {
    pthread_cond_t *wait;
    void * (*func)(void *);
    void *arg;
};

void worker_create(void * (*func)(void *), void *arg);
void worker_wait(struct transaction *trans);
void worker_wakeup(struct transaction *trans);
void worker_wait_for_all(void);

struct message *message_new(void);
struct transaction *transaction_new(struct connection *conn,
        struct message *in, struct message *out);
struct handles *handles_new(void);

#endif
