#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <gc.h>
#include "9p.h"
#include "config.h"
#include "state.h"
#include "util.h"

/*
 * Static state
 */

static struct hashtable *fd_2_conn = NULL;
static struct hashtable *addr_2_conn = NULL;
static struct hashtable *name_2_trans = NULL;

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

static u32 trans_name_hash(const struct transaction_name *elt) {
    u32 hash = 0;
    hash = generic_hash(&elt->conn, sizeof(elt->conn), hash);
    hash = generic_hash(&elt->tag, sizeof(elt->tag), hash);
    return hash;
}

static int trans_name_cmp(const struct transaction_name *a,
                    const struct transaction_name *b)
{
    if (a->conn != b->conn)
        return (int) a->conn - (int) b->conn;
    return (int) a->tag - (int) b->tag;
}

/*
 * Connection pool state
 */

void conn_insert_new(int fd, enum conn_type type, struct sockaddr_in *addr,
        int maxSize)
{
    struct connection *conn;

    assert(fd >= 0);
    assert(addr != NULL);
    assert(hash_get(fd_2_conn, &fd) == NULL);
    assert(hash_get(addr_2_conn, addr) == NULL);

    conn = GC_NEW(struct connection);
    assert(conn != NULL);

    conn->fd = fd;
    conn->type = type;
    conn->addr = addr;
    conn->maxSize = maxSize;
    conn->fids = hash_create(
            FID_HASHTABLE_SIZE,
            (u32 (*)(const void *)) u32_hash,
            (int (*)(const void *, const void *)) u32_cmp);

    hash_set(fd_2_conn, &conn->fd, conn);
    hash_set(addr_2_conn, conn->addr, conn);
}

struct connection *conn_lookup_fd(int fd) {
    return hash_get(fd_2_conn, &fd);
}

struct connection *conn_lookup_addr(struct sockaddr_in *addr) {
    return hash_get(addr_2_conn, addr);
}

void conn_remove(struct connection *conn) {
    assert(conn != NULL);
    assert(hash_get(fd_2_conn, &conn->fd) != NULL);
    assert(hash_get(addr_2_conn, conn->addr) != NULL);

    hash_remove(fd_2_conn, &conn->fd);
    hash_remove(addr_2_conn, conn->addr);
}

/*
 * Transaction pool state
 * Active transactions that are waiting for a response are indexed by name.
 */

void trans_insert(struct transaction *trans) {
    struct transaction_name *name;

    assert(trans != NULL);
    assert(trans->in == NULL);
    assert(trans->out != NULL);

    name = GC_NEW(struct transaction_name);
    assert(name != NULL);

    name->conn = trans->conn;
    name->tag = trans->out->tag;
    hash_set(name_2_trans, name, trans);
}

struct transaction *trans_lookup_remove(struct connection *conn, u16 tag) {
    struct transaction_name name;
    struct transaction *trans;

    assert(conn != NULL);

    if (tag == NOTAG)
        return NULL;

    name.conn = conn;
    name.tag = tag;

    trans = hash_get(name_2_trans, &name);
    hash_remove(name_2_trans, &name);

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

    /* when multithreaded, this needs to be atomic with the hash_set call */
    if (hash_get(conn->fids, &fid) != NULL)
        return -1;

    res = GC_NEW(struct fid);
    assert(res != NULL);

    res->fid = fid;
    res->uname = uname;
    res->path = path;
    res->fd = -1;
    res->status = STATUS_CLOSED;

    hash_set(conn->fids, &res->fid, res);

    return 0;
}

struct fid *fid_lookup(struct connection *conn, u32 fid) {
    assert(conn != NULL);
    assert(fid != NOFID);

    return hash_get(conn->fids, &fid);
}

struct fid *fid_lookup_remove(struct connection *conn, u32 fid) {
    struct fid *res;

    assert(conn != NULL);
    assert(fid != NOFID);

    res = hash_get(conn->fids, &fid);

    if (res != NULL)
        hash_remove(conn->fids, &fid);

    return res;
}

/*
 * Debugging functions
 */

void print_address(struct sockaddr_in *addr) {
    fprintf(stderr, "{%d.%d.%d.%d:%d}",
            (addr->sin_addr.s_addr >> 24) & 0xff,
            (addr->sin_addr.s_addr >> 16) & 0xff,
            (addr->sin_addr.s_addr >>  8) & 0xff,
             addr->sin_addr.s_addr        & 0xff,
            addr->sin_port);
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
    printf("fd:%d ", conn->fd);
    print_connection_type(conn->type);
    printf(" ");
    print_address(conn->addr);
}

static void print_fd_conn(int *fd, struct connection *conn) {
    printf("  [%d -> ", *fd);
    print_connection(conn);
    printf("]\n");
}

static void print_addr_conn(struct sockaddr_in *addr, struct connection *conn) {
    printf("  [");
    print_address(addr);
    printf(" -> ");
    print_connection(conn);
    printf("]\n");
}

static void print_transaction(struct transaction *trans) {
    printf("\n  ");
    print_connection(trans->conn);
    if (trans->in != NULL) {
        printf("\n  Request:\n   ");
        printMessage(stdout, trans->in);
    }
    if (trans->out != NULL) {
        printf("\n  Response:\n   ");
        printMessage(stdout, trans->out);
    }
}

static void print_name_trans(struct transaction_name *name,
        struct transaction *trans)
{
    printf("  tag[%d] (", (int) name->tag);
    print_connection(name->conn);
    printf(") -> ");
    print_transaction(trans);
    printf("\n");
}

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

void state_dump(void) {
    printf("Hashtable: fd -> connection:\n");
    hash_apply(fd_2_conn, (void (*)(void *, void *)) print_fd_conn);
    printf("Hashtable: address -> connection\n");
    hash_apply(addr_2_conn, (void (*)(void *, void *)) print_addr_conn);
    printf("Hashtable: name -> transaction\n");
    hash_apply(name_2_trans, (void (*)(void *, void *)) print_name_trans);
}

/*
 * State initializers
 */

static void conn_init(void) {
    assert(fd_2_conn == NULL);
    assert(addr_2_conn == NULL);

    fd_2_conn = hash_create(
            CONN_HASHTABLE_SIZE,
            (u32 (*)(const void *)) u32_hash,
            (int (*)(const void *, const void *)) u32_cmp);

    addr_2_conn = hash_create(
            CONN_HASHTABLE_SIZE,
            (u32 (*)(const void *)) addr_hash,
            (int (*)(const void *, const void *)) addr_cmp);
}

static void trans_init(void) {
    assert(name_2_trans == NULL);

    name_2_trans = hash_create(
            TRANS_HASHTABLE_SIZE,
            (u32 (*)(const void *)) trans_name_hash,
            (int (*)(const void *, const void *)) trans_name_cmp);
}

void state_init(void) {
    conn_init();
    trans_init();
}
