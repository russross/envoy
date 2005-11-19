#include <assert.h>
#include <pthread.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <netdb.h>
#include <netinet/in.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "vector.h"
#include "hashtable.h"
#include "connection.h"
#include "handles.h"
#include "transaction.h"
#include "fid.h"
#include "util.h"
#include "config.h"
#include "state.h"
#include "map.h"

/*
 * Static state
 */

struct state *state = NULL;

/*
 * Constructors
 */

Message *message_new(void) {
    Message *msg = GC_NEW(Message);
    assert(msg != NULL);
    msg->raw = GC_MALLOC_ATOMIC(GLOBAL_MAX_SIZE);
    assert(msg->raw != NULL);
    msg->tag = ALLOCTAG;
    return msg;
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

static u32 addr_hash(const Address *addr) {
    u32 hash = 0;
    hash = generic_hash(&addr->sin_family, sizeof(addr->sin_family), hash);
    hash = generic_hash(&addr->sin_port, sizeof(addr->sin_port), hash);
    hash = generic_hash(&addr->sin_addr, sizeof(addr->sin_addr), hash);
    return hash;
}

int addr_cmp(const Address *a, const Address *b) {
    int x;
    if ((x = memcmp(&a->sin_family, &b->sin_family, sizeof(a->sin_family))))
        return x;
    if ((x = memcmp(&a->sin_port, &b->sin_port, sizeof(a->sin_port))))
        return x;
    return memcmp(&a->sin_addr, &b->sin_addr, sizeof(a->sin_addr));
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

static void print_fid(Fid *fid) {
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

static void print_fid_fid(u32 n, Fid *fid) {
    printf("   %-2u:\n", n);
    print_fid(fid);
}

void print_address(Address *addr) {
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

static void print_transaction(Transaction *trans) {
    if (trans->in != NULL) {
        printf("    Req: ");
        printMessage(stdout, trans->in);
    }
    if (trans->out != NULL) {
        printf("    Res: ");
        printMessage(stdout, trans->out);
    }
}

static void print_tag_trans(u32 tag, Transaction *trans) {
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

static void print_connection(Connection *conn) {
    printf(" ");
    print_address(conn->addr);
    printf(" fd:%d ", conn->fd);
    print_connection_type(conn->type);
    printf("\n  fids:\n");
    vector_apply(conn->fid_vector, (void (*)(u32, void *)) print_fid_fid);
    printf("  transactions:\n");
    vector_apply(conn->tag_vector, (void (*)(u32, void *)) print_tag_trans);
}

static void print_addr_conn(Address *addr, Connection *conn) {
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
    state->my_address = make_address(hostname, ENVOY_PORT);

    state->map = GC_NEW(Map);
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
