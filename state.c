#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <gc.h>
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
    return m;
}

struct transaction *transaction_new(void) {
    struct transaction *t = GC_NEW(struct transaction);
    assert(t != NULL);
    return t;
}

struct handles *handles_new(void) {
    struct handles *set = GC_NEW(struct handles);
    assert(set != NULL);

    set->handles = GC_MALLOC_ATOMIC(sizeof(int) * HANDLES_INITIAL_SIZE);
    assert(set->handles != NULL);
    set->count = 0;
    set->max = HANDLES_INITIAL_SIZE;

    return set;
}

/*
 * Hash and comparison functions
 */

static u32 generic_hash(const void *elt, int len, u32 hash) {
    int i;
    u8 *bytes = (char *) elt;

    for (i = 0; i < len; i++)
        hash = hash * 157 + *(bytes++);

    return hash;
}

static u32 u32_hash(const u32 *key) {
    return *key;
}

static int u32_cmp(const u32 *a, const u32 *b) {
    return (int) *a - *b;
}

static u32 u16_hash(const u16 *key) {
    return (u32) *key;
}

static int u16_cmp(const u16 *a, const u16 *b) {
    return (int) *a - *b;
}

static u32 addr_hash(const struct sockaddr_in *addr) {
    u32 hash = 0;
    hash = generic_hash(&addr->sin_family, sizeof(addr->sin_family), hash);
    hash = generic_hash(&addr->sin_port, sizeof(addr->sin_port), hash);
    hash = generic_hash(&addr->sin_addr, sizeof(addr->sin_addr), hash);
    return hash;
}

static int addr_cmp(const struct sockaddr_in *a, const struct sockaddr_in *b) {
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
    /* do we need to resize the array? */
    if (set->count >= set->max) {
        int *old = set->handles;
        set->max *= 2;
        set->handles = GC_MALLOC_ATOMIC(sizeof(int) * set->max);
        assert(set->handles != NULL);
        memcpy(set->handles, old, sizeof(int) * set->count);
    }

    set->handles[set->count++] = handle;
}

void handles_remove(struct handles *set, int handle) {
    int i;

    /* do we need to resize the array? */
    if ((set->count - 1) * 4 < set->max && set->max > HANDLES_INITIAL_SIZE) {
        int *old = set->handles;
        set->max /= 2;
        set->handles = GC_MALLOC_ATOMIC(sizeof(int) * set->max);
        assert(set->handles != NULL);
        memcpy(set->handles, old, sizeof(int) * set->count);
    }

    /* find the handle ... */
    for (i = 0; i < set->count && set->handles[i] != handle; i++)
        ;
    assert(i != set->count);

    /* ... and remove it */
    for (i++ ; i < set->count; i++)
        set->handles[i-1] = set->handles[i];
    set->count--;
}

int handles_collect(struct handles *set, fd_set *rset, int high) {
    int i;

    for (i = 0; i < set->count; i++) {
        FD_SET(set->handles[i], rset);
        high = high > set->handles[i] ? high : set->handles[i]; 
    }

    return high;
}

int handles_member(struct handles *set, fd_set *rset) {
    int i;

    for (i = 0; i < set->count; i++)
        if (FD_ISSET(set->handles[i], rset))
            return set->handles[i];

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
    assert(hash_get(state->fd_2_conn, &fd) == NULL);
    assert(hash_get(state->addr_2_conn, addr) == NULL);

    conn = GC_NEW(struct connection);
    assert(conn != NULL);

    conn->fd = fd;
    conn->type = type;
    conn->addr = addr;
    conn->maxSize = maxSize;
    conn->fid_2_fid = hash_create(
            FID_HASHTABLE_SIZE,
            (u32 (*)(const void *)) u32_hash,
            (int (*)(const void *, const void *)) u32_cmp);
    conn->fid_2_forward = hash_create(
            FORWARD_HASHTABLE_SIZE,
            (u32 (*)(const void *)) u32_hash,
            (int (*)(const void *, const void *)) u32_cmp);
    conn->tag_2_trans = hash_create(
            TRANS_HASHTABLE_SIZE,
            (u32 (*)(const void *)) u16_hash,
            (int (*)(const void *, const void *)) u16_cmp);
    conn->pending_writes = NULL;

    hash_set(state->fd_2_conn, &conn->fd, conn);
    hash_set(state->addr_2_conn, conn->addr, conn);

    return conn;
}

struct connection *conn_new_unopened(enum conn_type type,
        struct sockaddr_in *addr, int maxSize)
{
    struct connection *conn;

    assert(addr != NULL);
    assert(hash_get(state->addr_2_conn, addr) == NULL);

    conn = GC_NEW(struct connection);
    assert(conn != NULL);

    conn->fd = -1;
    conn->type = type;
    conn->addr = addr;
    conn->maxSize = maxSize;
    conn->fid_2_fid = NULL;
    conn->fid_2_forward = NULL;
    conn->tag_2_trans = NULL;
    conn->pending_writes = NULL;

    return conn;
}

struct connection *conn_lookup_fd(int fd) {
    return hash_get(state->fd_2_conn, &fd);
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
    assert(hash_get(state->fd_2_conn, &conn->fd) != NULL);
    assert(hash_get(state->addr_2_conn, conn->addr) != NULL);

    hash_remove(state->fd_2_conn, &conn->fd);
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

    hash_set(trans->conn->tag_2_trans, &trans->out->tag, trans);
}

struct transaction *trans_lookup_remove(struct connection *conn, u16 tag) {
    struct transaction *trans;

    assert(conn != NULL);

    trans = hash_get(conn->tag_2_trans, &tag);
    hash_remove(conn->tag_2_trans, &tag);

    return trans;
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

    if (hash_get(conn->fid_2_fid, &fid) != NULL)
        return -1;

    res = GC_NEW(struct fid);
    assert(res != NULL);

    res->fid = fid;
    res->uname = uname;
    res->path = path;
    res->fd = -1;
    res->status = STATUS_CLOSED;

    hash_set(conn->fid_2_fid, &res->fid, res);

    return 0;
}

struct fid *fid_lookup(struct connection *conn, u32 fid) {
    assert(conn != NULL);
    assert(fid != NOFID);

    return hash_get(conn->fid_2_fid, &fid);
}

struct fid *fid_lookup_remove(struct connection *conn, u32 fid) {
    struct fid *res;

    assert(conn != NULL);
    assert(fid != NOFID);

    res = hash_get(conn->fid_2_fid, &fid);

    if (res != NULL)
        hash_remove(conn->fid_2_fid, &fid);

    return res;
}

/*
 * Forwarded fid state.
 */

int forward_insert_new(struct connection *conn, u32 fid,
        struct connection *rconn, u32 rfid)
{
    struct forward *res;

    assert(conn != NULL);
    assert(fid != NOFID);
    assert(rconn != NULL);
    assert(rfid != NOFID);

    if (hash_get(conn->fid_2_forward, &fid) != NULL)
        return -1;

    res = GC_NEW(struct forward);
    assert(res != NULL);

    res->fid = fid;
    res->rconn = rconn;
    res->rfid = rfid;

    hash_set(conn->fid_2_forward, &res->fid, res);

    return 0;
}

struct forward *forward_lookup(struct connection *conn, u32 fid) {
    assert(conn != NULL);
    assert(fid != NOFID);

    return hash_get(conn->fid_2_forward, &fid);
}

struct forward *forward_lookup_remove(struct connection *conn, u32 fid) {
    struct forward *res;

    assert(conn != NULL);
    assert(fid != NOFID);

    res = hash_get(conn->fid_2_forward, &fid);

    if (res != NULL)
        hash_remove(conn->fid_2_forward, &fid);

    return res;
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

static void print_fid_fid(u32 *n, struct fid *fid) {
    printf("   %-2u:\n", *n);
    print_fid(fid);
}

void print_address(struct sockaddr_in *addr) {
    printf("{%d.%d.%d.%d:%d}",
            (addr->sin_addr.s_addr >> 24) & 0xff,
            (addr->sin_addr.s_addr >> 16) & 0xff,
            (addr->sin_addr.s_addr >>  8) & 0xff,
             addr->sin_addr.s_addr        & 0xff,
            addr->sin_port);
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

static void print_tag_trans(u16 *tag, struct transaction *trans) {
    printf("   %-2u:\n", (u32) *tag);
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
    hash_apply(conn->fid_2_fid, (void (*)(void *, void *)) print_fid_fid);
    printf("  transactions:\n");
    hash_apply(conn->tag_2_trans, (void (*)(void *, void *)) print_tag_trans);
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

    state->fd_2_conn = hash_create(
            CONN_HASHTABLE_SIZE,
            (u32 (*)(const void *)) u32_hash,
            (int (*)(const void *, const void *)) u32_cmp);

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

    state->transaction_queue = NULL;
    state->error_queue = NULL;
}
