#include <assert.h>
#include <pthread.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "vector.h"
#include "hashtable.h"
#include "connection.h"
#include "handles.h"
#include "transaction.h"
#include "fid.h"
#include "util.h"
#include "config.h"
#include "state.h"
#include "envoy.h"
#include "worker.h"
#include "oid.h"
#include "lease.h"
#include "walk.h"

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

struct p9stat *p9stat_new(void) {
    struct p9stat *info = GC_NEW(struct p9stat);
    assert(info != NULL);

    /* set all fields to empty values */
    info->type = ~(u16) 0;
    info->dev = ~(u32) 0;
    info->qid.type = ~(u8) 0;
    info->qid.version = ~(u32) 0;
    info->qid.path = ~(u64) 0;
    info->mode = ~(u32) 0;
    info->atime = ~(u32) 0;
    info->mtime = ~(u32) 0;
    info->length = ~(u64) 0;
    info->name = NULL;
    info->uid = NULL;
    info->gid = NULL;
    info->muid = NULL;
    info->extension = NULL;
    info->n_uid = ~(u32) 0;
    info->n_gid = ~(u32) 0;
    info->n_muid = ~(u32) 0;

    return info;
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
        u32 address = addr_get_address(addr);
        printf("{%d.%d.%d.%d:%d}",
                (address >> 24) & 0xff,
                (address >> 16) & 0xff,
                (address >>  8) & 0xff,
                address         & 0xff,
                addr_get_port(addr));
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

    worker_state_init();

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
    walk_state_init();

    /* namespace management state */
    /*state->lease_owned = hash_create(
            LEASE_HASHTABLE_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp);
    state->lease_shared = hash_create(
            LEASE_HASHTABLE_SIZE,
            (Hashfunc) string_hash,
            (Cmpfunc) strcmp);

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

void storage_server_connection_init(void) {
    char *in = getenv("ENVOY_STORAGE_SERVERS");
    char *ptr;
    List *servers = NULL;
    int i;

    storage_server_count = 0;
    storage_servers = NULL;

    if (in == NULL) {
        fprintf(stderr, "ENVOY_STORAGE_SERVERS must be defined\n");
        exit(-1);
    }
    printf("ENVOY_STORAGE_SERVERS: [%s]\n", in);

    while (in != NULL && *in != 0) {
        char *machine;
        int port = 0;

        /* extract a single address from the list */
        if ((ptr = strchr(in, ',')) != NULL) {
            machine = substring(in, 0, ptr - in);
            in = ptr + 1;
        } else {
            machine = in;
            in = NULL;
        }

        /* now convert it into an Address */
        if ((ptr = strchr(machine, ':')) != NULL) {
            port = atoi(ptr + 1);
            if (port < 1)
                port = STORAGE_PORT;
            *ptr = 0;
        } else {
            port = STORAGE_PORT;
        }
        servers = cons(make_address(machine, port), servers);
    }

    /* put the servers in the original order */
    servers = reverse(servers);

    storage_server_count = length(servers);
    storage_servers = GC_MALLOC(sizeof(Connection *) * storage_server_count);
    assert(storage_servers != NULL);

    for (i = 0; i < storage_server_count; i++) {
        Address *addr = car(servers);
        Connection *conn;

        printf("storage server %d: ", i);
        print_address(addr);
        printf("\n");

        conn = conn_connect_to_storage(addr);
        if (conn == NULL) {
            printf("Failed to connect to storage server %d\n", i);
            assert(0);
        }

        storage_servers[i] = conn;
        servers = cdr(servers);
    }

    assert(null(servers));
}
