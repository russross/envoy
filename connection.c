#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <unistd.h>
#include <netinet/in.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "vector.h"
#include "hashtable.h"
#include "connection.h"
#include "transaction.h"
#include "util.h"
#include "config.h"
#include "transport.h"
#include "dispatch.h"
#include "worker.h"

/*
 * Connection pool state
 */

Vector *conn_vector;
Hashtable *addr_2_envoy_out;
Hashtable *addr_2_in;
int envoycount;

Connection *conn_insert_new(int fd, enum conn_type type,
        struct sockaddr_in *netaddr)
{
    Connection *conn;

    assert(fd >= 0);
    assert(netaddr != NULL);
    assert(!vector_test(conn_vector, fd));

    conn = GC_NEW(Connection);
    assert(conn != NULL);

    conn->fd = fd;
    if (type == CONN_ENVOY_IN)
        conn->envoyindex = envoycount++;
    else
        conn->envoyindex = 0;
    conn->type = type;
    conn->netaddr = netaddr;
    conn->addr = netaddr_to_addr(netaddr);
    conn->maxSize = GLOBAL_MAX_SIZE;
    conn->fid_vector = vector_create(FID_VECTOR_SIZE);
    conn->tag_vector = vector_create(TAG_VECTOR_SIZE);
    conn->pending_writes = NULL;
    conn->notag_trans = NULL;
    conn->partial_in = NULL;
    conn->partial_in_bytes = 0;
    conn->partial_out = NULL;
    conn->partial_out_bytes = 0;
    conn->totalbytesin = 0;
    conn->totalbytesout = 0;
    conn->totalmessagesin = 0;
    conn->totalmessagesout = 0;
    conn->prevbytesin = 0;
    conn->prevbytesout = 0;
    conn->prevmessagesin = 0;
    conn->prevmessagesout = 0;

    vector_set(conn_vector, conn->fd, conn);
    if (type == CONN_ENVOY_OUT) {
        assert(hash_get(addr_2_envoy_out, conn->addr) == NULL);
        hash_set(addr_2_envoy_out, conn->addr, conn);
    } else if (type == CONN_ENVOY_IN || type == CONN_CLIENT_IN ||
            type == CONN_UNKNOWN_IN)
    {
        assert(hash_get(addr_2_in, conn->addr) == NULL);
        hash_set(addr_2_in, conn->addr, conn);
    }

    return conn;
}

Connection *conn_insert_new_stub(Address *addr) {
    Connection *conn = GC_NEW(Connection);
    assert(conn != NULL);

    conn->fd = -1;
    conn->envoyindex = envoycount++;
    conn->type = CONN_ENVOY_IN;
    conn->netaddr = NULL;
    conn->addr = addr;
    conn->maxSize = GLOBAL_MAX_SIZE;
    conn->fid_vector = vector_create(FID_VECTOR_SIZE);
    conn->tag_vector = NULL;
    conn->pending_writes = NULL;
    conn->notag_trans = NULL;
    conn->partial_in = NULL;
    conn->partial_in_bytes = 0;
    conn->partial_out = NULL;
    conn->partial_out_bytes = 0;
    conn->totalbytesin = 0;
    conn->totalbytesout = 0;
    conn->totalmessagesin = 0;
    conn->totalmessagesout = 0;
    conn->prevbytesin = 0;
    conn->prevbytesout = 0;
    conn->prevmessagesin = 0;
    conn->prevmessagesout = 0;

    hash_set(addr_2_in, addr, conn);

    return conn;
}

void conn_set_addr_envoy_in(Connection *conn, Address *addr) {
    Connection *merge = hash_get(addr_2_in, addr);

    assert(conn->addr != NULL);
    assert(addr != NULL);
    assert(conn->type == CONN_ENVOY_IN);

    hash_remove(addr_2_in, conn->addr);

    if (merge != NULL) {
        hash_remove(addr_2_in, addr);
        assert(vector_isempty(conn->fid_vector));
        conn->fid_vector = merge->fid_vector;
    }

    conn->addr = addr;
    hash_set(addr_2_in, addr, conn);
}

Connection *conn_lookup_fd(int fd) {
    return vector_get(conn_vector, fd);
}

Connection *conn_get_envoy_out(Worker *worker, Address *addr) {
    Transaction *trans;
    Connection *conn;

    assert(addr != NULL);
    assert(addr_cmp(addr, my_address));

    if ((conn = hash_get(addr_2_envoy_out, addr)) == NULL) {
        int fd;
        struct sockaddr_in *netaddr = addr_to_netaddr(addr);
        if ((fd = open_connection(netaddr)) < 0)
            return NULL;

        conn = conn_insert_new(fd, CONN_ENVOY_OUT, netaddr);

        if ((trans = connect_envoy(conn)) != NULL) {
            handle_error(worker, trans);
            conn_remove(conn);
            return NULL;
        }
    }

    return conn;
}

Connection *conn_get_incoming(Address *addr) {
    Connection *conn = hash_get(addr_2_in, addr);
    return conn;
}

Connection *conn_connect_to_storage(Address *addr) {
    struct sockaddr_in *netaddr = addr_to_netaddr(addr);
    Connection *conn;
    int fd;

    if ((fd = open_connection(netaddr)) < 0)
        return NULL;

    conn = conn_insert_new(fd, CONN_STORAGE_OUT, netaddr);

    if (connect_envoy(conn) != NULL) {
        conn_remove(conn);
        return NULL;
    }

    return conn;
}

Message *conn_get_pending_write(Connection *conn) {
    Message *msg = NULL;

    assert(conn != NULL);

    if (!null(conn->pending_writes)) {
        List *elt = conn->pending_writes;
        msg = car(elt);
        conn->pending_writes = cdr(elt);
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
    assert(vector_test(conn_vector, conn->fd));
    if (conn->type == CONN_ENVOY_OUT) {
        assert(hash_get(addr_2_envoy_out, conn->addr) != NULL);
        hash_remove(addr_2_envoy_out, conn->addr);
    } else if (conn->type == CONN_ENVOY_IN || conn->type == CONN_CLIENT_IN ||
            conn->type == CONN_UNKNOWN_IN)
    {
        assert(hash_get(addr_2_in, conn->addr) != NULL);
        hash_remove(addr_2_in, conn->addr);
    }

    vector_remove(conn_vector, conn->fd);
}

/*
static int conn_resurrect(Connection *conn) {
    return
        conn->type == CONN_CLIENT_IN ||
        conn->type == CONN_ENVOY_IN ||
        !null(conn->pending_writes) ||
        conn->partial_in_bytes > 0 ||
        conn->partial_out_bytes > 0 ||
        conn->fid_vector != NULL ||
        conn->tag_vector != NULL;
}
*/

void conn_close(Connection *conn) {
    close(conn->fd);
    conn->fd = -1;
}

void conn_init(void) {
    conn_vector = vector_create(CONN_VECTOR_SIZE);
    addr_2_envoy_out = hash_create(
            CONN_HASHTABLE_SIZE,
            (u32 (*)(const void *)) addr_hash,
            (int (*)(const void *, const void *)) addr_cmp);
    addr_2_in = hash_create(
            CONN_HASHTABLE_SIZE,
            (u32 (*)(const void *)) addr_hash,
            (int (*)(const void *, const void *)) addr_cmp);
    envoycount = 1;
}
