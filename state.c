#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <sys/select.h>
#include <assert.h>
#include <pthread.h>
#include <gc/gc.h>
#include "9p.h"
#include "config.h"
#include "state.h"
#include "list.h"
#include "util.h"
#include "map.h"

/*
 * Static state
 */

struct state *state = NULL;

/*
 * Constructors
 */

struct message *message_new(void) {
    struct message *m = GC_NEW(struct message);
    assert(m != NULL);
    m->raw = GC_MALLOC_ATOMIC(GLOBAL_MAX_SIZE);
    assert(m->raw != NULL);
    /* make sure tag doesn't default to NOTAG */
    m->tag = 0;
    return m;
}

struct transaction *transaction_new(struct connection *conn,
        struct message *in, struct message *out)
{
    struct transaction *t = GC_NEW(struct transaction);
    assert(t != NULL);
    t->wait = GC_NEW(pthread_cond_t);
    assert(t->wait != NULL);
    pthread_cond_init(t->wait, NULL);
    t->conn = conn;
    t->in = in;
    t->out = out;
    return t;
}

struct handles *handles_new(void) {
    struct handles *set = GC_NEW(struct handles);
    assert(set != NULL);
    set->set = GC_NEW_ATOMIC(fd_set);
    assert(set != NULL);
    FD_ZERO(set->set);
    set->high = -1;

    return set;
}

/*
 * Hash and comparison functions
 */

static u32 generic_hash(const void *elt, int len, u32 hash) {
    int i;
    u8 *bytes = (u8 *) elt;

    for (i = 0; i < len; i++)
        hash = hash * 157 + *(bytes++);

    return hash;
}

static u32 addr_hash(const struct sockaddr_in *addr) {
    u32 hash = 0;
    hash = generic_hash(&addr->sin_family, sizeof(addr->sin_family), hash);
    hash = generic_hash(&addr->sin_port, sizeof(addr->sin_port), hash);
    hash = generic_hash(&addr->sin_addr, sizeof(addr->sin_addr), hash);
    return hash;
}

int addr_cmp(const struct sockaddr_in *a, const struct sockaddr_in *b) {
    int x;
    if ((x = memcmp(&a->sin_family, &b->sin_family, sizeof(a->sin_family))))
        return x;
    if ((x = memcmp(&a->sin_port, &b->sin_port, sizeof(a->sin_port))))
        return x;
    return memcmp(&a->sin_addr, &b->sin_addr, sizeof(a->sin_addr));
}

/*
 * Handle sets
 */

void handles_add(struct handles *set, int handle) {
    FD_SET(handle, set->set);
    set->high = handle > set->high ? handle : set->high;
}

void handles_remove(struct handles *set, int handle) {
    FD_CLR(handle, set->set);
    if (handle >= set->high)
        while (set->high >= 0 && FD_ISSET(set->high, set->set))
            set->high--;
}

int handles_collect(struct handles *set, fd_set *rset, int high) {
    int i;
    fd_mask *a = (fd_mask *) rset, *b = (fd_mask *) set->set;

    for (i = 0; i * NFDBITS <= high; i++)
        a[i] |= b[i];

    return high > set->high ? high : set->high;
}

int handles_member(struct handles *set, fd_set *rset) {
    int i;

    for (i = 0; i <= set->high; i++)
        if (FD_ISSET(i, rset) && FD_ISSET(i, set->set))
            return i;

    return -1;
}

/*
 * Connection pool state
 */

struct connection *conn_insert_new(int fd, enum conn_type type,
        struct sockaddr_in *addr, int maxSize)
{
    struct connection *conn;

    assert(fd >= 0);
    assert(addr != NULL);
    assert(!vector_test(state->conn_vector, fd));
    assert(hash_get(state->addr_2_conn, addr) == NULL);

    conn = GC_NEW(struct connection);
    assert(conn != NULL);

    conn->fd = fd;
    conn->type = type;
    conn->addr = addr;
    conn->maxSize = maxSize;
    conn->fid_vector = vector_create(FID_VECTOR_SIZE);
    conn->forward_vector = vector_create(FORWARD_VECTOR_SIZE);
    conn->tag_vector = vector_create(TAG_VECTOR_SIZE);
    conn->pending_writes = NULL;
    conn->notag_trans = NULL;

    vector_set(state->conn_vector, conn->fd, conn);
    hash_set(state->addr_2_conn, conn->addr, conn);

    return conn;
}

struct connection *conn_new_unopened(enum conn_type type,
        struct sockaddr_in *addr)
{
    struct connection *conn;

    assert(addr != NULL);
    assert(hash_get(state->addr_2_conn, addr) == NULL);

    conn = GC_NEW(struct connection);
    assert(conn != NULL);

    conn->fd = -1;
    conn->type = type;
    conn->addr = addr;
    conn->maxSize = GLOBAL_MAX_SIZE;
    conn->fid_vector = vector_create(FID_VECTOR_SIZE);
    conn->forward_vector = vector_create(FORWARD_VECTOR_SIZE);
    conn->tag_vector = vector_create(TAG_VECTOR_SIZE);
    conn->pending_writes = NULL;
    conn->notag_trans = NULL;

    return conn;
}

struct connection *conn_lookup_fd(int fd) {
    return vector_get(state->conn_vector, fd);
}

struct connection *conn_lookup_addr(struct sockaddr_in *addr) {
    return hash_get(state->addr_2_conn, addr);
}

struct transaction *conn_get_pending_write(struct connection *conn) {
    struct transaction *res = NULL;

    assert(conn != NULL);

    if (!null(conn->pending_writes)) {
        res = car(conn->pending_writes);
        conn->pending_writes = cdr(conn->pending_writes);
    }

    return res;
}

int conn_has_pending_write(struct connection *conn) {
    return conn->pending_writes != NULL;
}

void conn_queue_write(struct transaction *trans) {
    assert(trans != NULL);

    trans->conn->pending_writes =
        append_elt(trans->conn->pending_writes, trans);
}

void conn_remove(struct connection *conn) {
    assert(conn != NULL);
    assert(vector_test(state->conn_vector, conn->fd));
    assert(hash_get(state->addr_2_conn, conn->addr) != NULL);

    vector_remove(state->conn_vector, conn->fd);
    hash_remove(state->addr_2_conn, conn->addr);
}

/*
 * Transaction pool state
 * Active transactions that are waiting for a response are indexed by tag.
 */

void trans_insert(struct transaction *trans) {
    assert(trans != NULL);
    assert(trans->conn != NULL);
    assert(trans->in == NULL);
    assert(trans->out != NULL);

    /* NOTAG is a special case */
    if (trans->out->tag == NOTAG)
        trans->conn->notag_trans = trans;
    else
        trans->out->tag = vector_alloc(trans->conn->tag_vector, trans);
}

struct transaction *trans_lookup_remove(struct connection *conn, u16 tag) {
    assert(conn != NULL);

    if (tag == NOTAG) {
        struct transaction *res = conn->notag_trans;
        conn->notag_trans = NULL;
        return res;
    }

    return (struct transaction *) vector_get_remove(conn->tag_vector, tag);
}

/*
 * Fid pool state
 */

int fid_insert_new(struct connection *conn, u32 fid, char *uname, char *path) {
    struct fid *res;

    assert(conn != NULL);
    assert(fid != NOFID);
    assert(uname != NULL && *uname);
    assert(path != NULL && *path);

    if (vector_test(conn->fid_vector, fid))
        return -1;

    res = GC_NEW(struct fid);
    assert(res != NULL);

    res->fid = fid;
    res->uname = uname;
    res->path = path;
    res->fd = -1;
    res->status = STATUS_CLOSED;

    vector_set(conn->fid_vector, res->fid, res);

    return 0;
}

struct fid *fid_lookup(struct connection *conn, u32 fid) {
    assert(conn != NULL);
    assert(fid != NOFID);

    return vector_get(conn->fid_vector, fid);
}

struct fid *fid_lookup_remove(struct connection *conn, u32 fid) {
    assert(conn != NULL);
    assert(fid != NOFID);

    return (struct fid *) vector_get_remove(conn->fid_vector, fid);
}

/*
 * Forwarded fid state.
 */

u32 forward_create_new(struct connection *conn, u32 fid,
        struct connection *rconn)
{
    struct forward *fwd;
    u32 rfid;

    assert(conn != NULL);
    assert(fid != NOFID);
    assert(rconn != NULL);

    if (vector_test(conn->forward_vector, fid))
        return NOFID;

    fwd = GC_NEW(struct forward);
    assert(fwd != NULL);

    rfid = vector_alloc(state->forward_fids, fwd);

    fwd->fid = fid;
    fwd->rconn = rconn;
    fwd->rfid = rfid;

    vector_set(conn->forward_vector, fid, fwd);

    return rfid;
}

struct forward *forward_lookup(struct connection *conn, u32 fid) {
    assert(conn != NULL);
    assert(fid != NOFID);

    return vector_get(conn->forward_vector, fid);
}

struct forward *forward_lookup_remove(struct connection *conn, u32 fid) {
    struct forward *fwd;

    assert(conn != NULL);
    assert(fid != NOFID);

    fwd = vector_get_remove(conn->forward_vector, fid);

    if (fwd != NULL)
        vector_remove(state->forward_fids, fwd->rfid);

    return fwd;
}

/*
 * Debugging functions
 */

static void print_status(enum fd_status status) {
    printf("%s",
            status == STATUS_CLOSED ? "STATUS_CLOSED" :
            status == STATUS_OPEN_FILE ? "STATUS_OPEN_FILE" :
            status == STATUS_OPEN_DIR ? "STATUS_OPEN_DIR" :
            status == STATUS_OPEN_SYMLINK ? "STATUS_OPEN_SYMLINK" :
            status == STATUS_OPEN_LINK ? "STATUS_OPEN_LINK" :
            status == STATUS_OPEN_DEVICE ? "STATUS_OPEN_DEVICE" :
            "(unknown status)");
}

static void print_fid(struct fid *fid) {
    printf("    fid:%u path:%s uname:%s\n    ",
            fid->fid, fid->path, fid->uname);
    print_status(fid->status);
    if (fid->fd > 0)
        printf(" fd[%d]", fid->fd);
    if (fid->dd != NULL)
        printf(" dd[%u]", (u32) fid->dd);
    printf(" offset[%llu] omode[$%x]\n", fid->offset, fid->omode);
    if (fid->next_dir_entry != NULL) {
        printf("    next_dir_entry:\n");
        dumpStat(stdout, "      ", fid->next_dir_entry);
    }
}

static void print_fid_fid(u32 n, struct fid *fid) {
    printf("   %-2u:\n", n);
    print_fid(fid);
}

void print_address(struct sockaddr_in *addr) {
    struct hostent *host = gethostbyaddr(&addr->sin_addr,
            sizeof(addr->sin_addr), addr->sin_family);
    if (host == NULL || host->h_name == NULL) {
        printf("{%d.%d.%d.%d:%d}",
                (addr->sin_addr.s_addr >> 24) & 0xff,
                (addr->sin_addr.s_addr >> 16) & 0xff,
                (addr->sin_addr.s_addr >>  8) & 0xff,
                addr->sin_addr.s_addr        & 0xff,
                addr->sin_port);
    } else {
        printf("{%s:%d}", host->h_name, addr->sin_port);
    }
}

static void print_transaction(struct transaction *trans) {
    if (trans->in != NULL) {
        printf("    Req: ");
        printMessage(stdout, trans->in);
    }
    if (trans->out != NULL) {
        printf("    Res: ");
        printMessage(stdout, trans->out);
    }
}

static void print_tag_trans(u32 tag, struct transaction *trans) {
    printf("   %-2u:\n", tag);
    print_transaction(trans);
}

static void print_connection_type(enum conn_type type) {
    printf("%s",
            type == CONN_CLIENT_IN ? "CONN_CLIENT_IN" :
            type == CONN_ENVOY_IN ? "CONN_ENVOY_IN" :
            type == CONN_ENVOY_OUT ? "CONN_ENVOY_OUT" :
            type == CONN_STORAGE_OUT ? "CONN_STORAGE_OUT" :
            "(unknown state)");
}

static void print_connection(struct connection *conn) {
    printf(" ");
    print_address(conn->addr);
    printf(" fd:%d ", conn->fd);
    print_connection_type(conn->type);
    printf("\n  fids:\n");
    vector_apply(conn->fid_vector, (void (*)(u32, void *)) print_fid_fid);
    printf("  transactions:\n");
    vector_apply(conn->tag_vector, (void (*)(u32, void *)) print_tag_trans);
}

static void print_addr_conn(struct sockaddr_in *addr, struct connection *conn) {
    print_connection(conn);
}

void state_dump(void) {
    printf("Hashtable: address -> connection\n");
    hash_apply(state->addr_2_conn, (void (*)(void *, void *)) print_addr_conn);
}

/*
 * State initializer
 */

void state_init(void) {
    char *hostname;

    assert(state == NULL);
    state = GC_NEW(struct state);
    assert(state != NULL);

    state->handles_listen = handles_new();
    state->handles_read = handles_new();
    state->handles_write = handles_new();

    state->conn_vector = vector_create(CONN_VECTOR_SIZE);
    state->forward_fids = vector_create(GLOBAL_FORWARD_VECTOR_SIZE);
    state->addr_2_conn = hash_create(
            CONN_HASHTABLE_SIZE,
            (u32 (*)(const void *)) addr_hash,
            (int (*)(const void *, const void *)) addr_cmp);

    assert((hostname = getenv("HOSTNAME")) != NULL);
    state->my_address = make_address(hostname, PORT);

    state->map = GC_NEW(struct map);
    assert(state->map != NULL);
    state->map->prefix = NULL;
    state->map->addr = state->my_address;
    state->map->nchildren = 0;
    state->map->children = NULL;

    state->error_queue = NULL;

    state->biglock = GC_NEW(pthread_mutex_t);
    assert(state->biglock != NULL);
    pthread_mutex_init(state->biglock, NULL);
    pthread_mutex_lock(state->biglock);

    state->wait_workers = GC_NEW(pthread_cond_t);
    assert(state->wait_workers != NULL);
    pthread_cond_init(state->wait_workers, NULL);

    state->active_worker_count = 0;
    state->thread_pool = NULL;
}

/*
 * Worker threads
 */

static void *worker_thread_loop(struct worker_thread *t) {
    state->active_worker_count++;
    pthread_mutex_lock(state->biglock);

    int i;
    for (i = 0; i < THREAD_LIFETIME; i++) {
        if (i > 0) {
            /* wait in the pool for a request */
            state->thread_pool = append_elt(state->thread_pool, t);
            pthread_cond_wait(t->wait, state->biglock);
        }

        /* service the request */
        if (t->func != NULL)
            t->func(t->arg);
        t->func = NULL;
        state->active_worker_count--;
        pthread_cond_signal(state->wait_workers);
    }

    pthread_mutex_unlock(state->biglock);
    return NULL;
}

void worker_create(void * (*func)(void *), void *arg) {
    if (null(state->thread_pool)) {
        struct worker_thread *t = GC_NEW(struct worker_thread);
        assert(t != NULL);
        t->wait = GC_NEW(pthread_cond_t);
        assert(t->wait != NULL);
        pthread_cond_init(t->wait, NULL);

        t->func = func;
        t->arg = arg;

        pthread_t newthread;
        pthread_create(&newthread, NULL,
                (void *(*)(void *)) worker_thread_loop, (void *) t);
    } else {
        struct worker_thread *t = car(state->thread_pool);
        state->thread_pool = cdr(state->thread_pool);

        t->func = func;
        t->arg = arg;

        state->active_worker_count++;
        pthread_cond_signal(t->wait);
    }
}

void worker_wait(struct transaction *trans) {
    state->active_worker_count--;
    pthread_cond_signal(state->wait_workers);
    pthread_cond_wait(trans->wait, state->biglock);
}

void worker_wakeup(struct transaction *trans) {
    state->active_worker_count++;
    pthread_cond_signal(trans->wait);
}

void worker_wait_for_all(void) {
    while (state->active_worker_count > 0)
        pthread_cond_wait(state->wait_workers, state->biglock);
}
