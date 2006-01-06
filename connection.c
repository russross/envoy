#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <netinet/in.h>
#include "types.h"
#include "list.h"
#include "vector.h"
#include "hashtable.h"
#include "connection.h"
#include "config.h"
#include "state.h"
#include "transport.h"
#include "dispatch.h"
#include "worker.h"

/*
 * Connection pool state
 */

Connection *conn_insert_new(int fd, enum conn_type type, Address *addr) {
    Connection *conn;

    assert(fd >= 0);
    assert(addr != NULL);
    assert(!vector_test(state->conn_vector, fd));
    assert(hash_get(state->addr_2_conn, addr) == NULL);

    conn = GC_NEW(Connection);
    assert(conn != NULL);

    conn->fd = fd;
    conn->type = type;
    conn->addr = addr;
    conn->maxSize = GLOBAL_MAX_SIZE;
    conn->fid_vector = vector_create(FID_VECTOR_SIZE);
    conn->forward_vector = vector_create(FORWARD_VECTOR_SIZE);
    conn->tag_vector = vector_create(TAG_VECTOR_SIZE);
    conn->pending_writes = NULL;
    conn->notag_trans = NULL;
    conn->partial_in = NULL;
    conn->partial_in_bytes = 0;
    conn->partial_out = NULL;
    conn->partial_out_bytes = 0;

    vector_set(state->conn_vector, conn->fd, conn);
    hash_set(state->addr_2_conn, conn->addr, conn);

    return conn;
}

Connection *conn_lookup_fd(int fd) {
    return vector_get(state->conn_vector, fd);
}

static Connection *get_from_addr(Worker *worker, Address *addr) {
    Connection *conn;

    /* note: caller must hold LOCK_CONNECTION */

    assert(addr != NULL);

    if ((conn = hash_get(state->addr_2_conn, addr)) == NULL) {
        int fd;
        if ((fd = open_connection(addr)) < 0)
            return NULL;

        if (addr->sin_port == ENVOY_PORT)
            conn = conn_insert_new(fd, CONN_ENVOY_OUT, addr);
        else if (addr->sin_port == STORAGE_PORT)
            conn = conn_insert_new(fd, CONN_STORAGE_OUT, addr);
        else
            assert(0);

        if (connect_envoy(worker, conn) < 0) {
            conn_remove(conn);
            return NULL;
        }
    }

    return conn;
}

Connection *conn_get_from_addr(Worker *worker, Address *addr) {
    Connection *conn;
    worker_lock_acquire(LOCK_CONNECTION);
    conn = get_from_addr(worker, addr);
    worker_lock_release(LOCK_CONNECTION);
    return conn;
}

Message *conn_get_pending_write(Connection *conn) {
    Message *msg = NULL;

    assert(conn != NULL);

    if (!null(conn->pending_writes)) {
        List *elt = conn->pending_writes;
        msg = car(elt);
        conn->pending_writes = cdr(elt);
        GC_free(elt);
    }

    return msg;
}

int conn_has_pending_write(Connection *conn) {
    return conn->partial_out != NULL || !null(conn->pending_writes);
}

void conn_queue_write(Connection *conn, Message *msg) {
    assert(conn != NULL && msg != NULL);

    conn->pending_writes = append_elt(conn->pending_writes, msg);
}

void conn_remove(Connection *conn) {
    assert(conn != NULL);
    assert(vector_test(state->conn_vector, conn->fd));
    assert(hash_get(state->addr_2_conn, conn->addr) != NULL);

    vector_remove(state->conn_vector, conn->fd);
    hash_remove(state->addr_2_conn, conn->addr);
}
