#include <assert.h>
#include <pthread.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
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
#include "worker.h"
#include "oid.h"
#include "lease.h"

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
 * Debugging functions
 */

static void print_status(enum fid_status status) {
    printf("%s",
            status == STATUS_UNOPENNED ? "STATUS_UNOPENNED" :
            status == STATUS_OPEN_FILE ? "STATUS_OPEN_FILE" :
            status == STATUS_OPEN_DIR ? "STATUS_OPEN_DIR" :
            "(unknown status)");
}

static void print_fid(Fid *fid) {
    printf("    fid:%u path:%s uname:%s\n    ",
            fid->fid, fid->claim->pathname, fid->user);
    print_status(fid->status);
    printf(" cookie[%llu] omode[$%x]\n", fid->readdir_cookie, fid->omode);
    if (fid->readdir_env != NULL)
        printf("    readdir in progress:\n");
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
                ntohs(addr->sin_port));
    } else {
        printf("{%s:%d}", host->h_name, ntohs(addr->sin_port));
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
    hash_apply(addr_2_conn, (void (*)(void *, void *)) print_addr_conn);
}

/*
 * State initializer
 */

static void state_init_common(int port) {
    char *hostname;

    assert(state == NULL);
    state = GC_NEW(struct state);
    assert(state != NULL);

    /* find out who we are */
    hostname = GC_MALLOC_ATOMIC(MAX_HOSTNAME + 1);

    assert(hostname != NULL);
    hostname[MAX_HOSTNAME] = 0;
    if (gethostname(hostname, MAX_HOSTNAME) < 0) {
        perror("can't find local hostname");
        exit(-1);
    }

    hostname = stringcopy(hostname);
    state->my_address = make_address(hostname, port);

    printf("starting up on host %s\n", hostname);

    /* set up the transport state */
    state->handles_listen = handles_new();
    state->handles_read = handles_new();
    state->handles_write = handles_new();
    state->refresh_pipe = GC_MALLOC_ATOMIC(sizeof(int) * 2);
    assert(state->refresh_pipe != NULL);

    conn_init();

    /* error handling */
    state->error_queue = NULL;

    /* the lock */
    state->biglock = GC_NEW(pthread_mutex_t);
    assert(state->biglock != NULL);
    pthread_mutex_init(state->biglock, NULL);

    state->thread_pool = NULL;
}

void state_init_envoy(void) {
    state_init_common(ENVOY_PORT);
    state->isstorage = 0;

    lease_state_init();
    /* namespace management state */
    /*state->lease_owned = hash_create(
            LEASE_HASHTABLE_SIZE,
            (u32 (*)(const void *)) string_hash,
            (int (*)(const void *, const void *)) strcmp);
    state->lease_shared = hash_create(
            LEASE_HASHTABLE_SIZE,
            (u32 (*)(const void *)) string_hash,
            (int (*)(const void *, const void *)) strcmp);

    state->map = GC_NEW(Map);
    assert(state->map != NULL);
    state->map->prefix = NULL;
    state->map->addr = state->my_address;
    state->map->nchildren = 0;
    state->map->children = NULL; */
}

void state_init_storage(void) {
    state_init_common(STORAGE_PORT);
    state->isstorage = 1;

    /* object cache state */
    state->objectdir_lru = init_objectdir_lru();
    state->openfile_lru = init_openfile_lru();
    state->oid_next_available = oid_find_next_available();
}
