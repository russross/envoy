#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <gc.h>
#include "9p.h"
#include "config.h"
#include "transport.h"
#include "state.h"

#define HANDLES_INITIAL_SIZE 32

/*****************************************************************************/
/* Connection sets */

struct connection_set {
    int *handles;
    int count;
    int max;
};

static struct connection_set connections_listen;
static struct connection_set connections_read;
static struct connection_set connections_write;

static void connection_set_init(struct connection_set *elt) {
    elt->handles = GC_MALLOC_ATOMIC(sizeof(int) * HANDLES_INITIAL_SIZE);
    assert(elt->handles != NULL);
    elt->count = 0;
    elt->max = HANDLES_INITIAL_SIZE;
}

static void connection_set_add(struct connection_set *elt, int handle) {
    /* do we need to resize the array? */
    if (elt->count >= elt->max) {
        int *old = elt->handles;
        elt->max *= 2;
        elt->handles = GC_MALLOC_ATOMIC(sizeof(int) * elt->max);
        assert(elt->handles != NULL);
        memcpy(elt->handles, old, sizeof(int) * elt->count);
    }

    elt->handles[elt->count++] = handle;
}

static void connection_set_remove(struct connection_set *elt, int handle)
{
    int i;

    /* do we need to resize the array? */
    if ((elt->count - 1) * 4 < elt->max && elt->max > HANDLES_INITIAL_SIZE) {
        int *old = elt->handles;
        elt->max /= 2;
        elt->handles = GC_MALLOC_ATOMIC(sizeof(int) * elt->max);
        assert(elt->handles != NULL);
        memcpy(elt->handles, old, sizeof(int) * elt->count);
    }

    /* find the handle ... */
    for (i = 0; i < elt->count && elt->handles[i] != handle; i++)
        ;
    assert(i != elt->count);

    /* ... and remove it */
    for (i++ ; i < elt->count; i++)
        elt->handles[i-1] = elt->handles[i];
    elt->count--;
}

static int connection_set_collect(struct connection_set *elt, fd_set *rset,
        int high)
{
    int i;

    for (i = 0; i < elt->count; i++) {
        FD_SET(elt->handles[i], rset);
        high = high > elt->handles[i] ? high : elt->handles[i]; 
    }

    return high;
}

static int connection_set_member(struct connection_set *elt, fd_set *rset) {
    int i;

    for (i = 0; i < elt->count; i++)
        if (FD_ISSET(elt->handles[i], rset))
            return elt->handles[i];

    return -1;
}

/* Connection sets */
/*****************************************************************************/

static void write_message(int fd) {
    struct connection *conn;
    struct transaction *trans;
    int len;

    assert((conn = conn_lookup_fd(fd)) != NULL);
    trans = conn_get_pending_write(conn);


    if (trans != NULL) {
        assert(trans != NULL);
        assert(trans->conn != NULL);
        assert(trans->out != NULL);

        /* if this was the last message in the queue, stop trying to write */
        if (!conn_has_pending_write(conn)) {
            connection_set_remove(&connections_write, fd);
        }

        packMessage(trans->out);

        if (    trans->conn->type == CONN_ENVOY_OUT ||
                trans->conn->type == CONN_STORAGE_OUT)
        {
            /* this is an outgoing request, so we should be prepared
               for a response */
            assert(trans->handler != NULL);
            assert(trans->in == NULL);

            trans_insert(trans);
        } else {
            /* this is a reply, so make sure there was a matching request */
            assert(trans->in != NULL);
        }

        printMessage(stderr, trans->out);

        len = send(trans->conn->fd, trans->out->raw, trans->out->size, 0);

        assert(len == trans->out->size);
    }
}

static struct message *read_message(int fd, struct connection **from) {
    struct message *m = message_new();
    int index = 0;
    int count;

    *from = conn_lookup_fd(fd);
    assert(*from != NULL);

    count = recv(fd, m->raw, 4, MSG_WAITALL);

    /* has the connection been closed? */
    if (count == 0)
        printf("socket closed from other end\n");

    if (    count != 4 ||
            (m->size = unpackU32(m->raw, 4, &index)) > GLOBAL_MAX_SIZE ||
            recv(fd, m->raw + 4, m->size - 4, MSG_WAITALL) != m->size - 4 ||
            unpackMessage(m) < 0)
    {
        /* something went wrong */
        printf("closing connection: ");
        print_address((*from)->addr);
        printf("\n");

        /* close down the connection */
        conn_remove(*from);
        connection_set_remove(&connections_read, fd);
        if (!conn_has_pending_write(*from))
            connection_set_remove(&connections_write, fd);
        close(fd);

        return NULL;
    }

    return m;
}

static void accept_connection(int sock) {
    struct sockaddr_in *addr;
    socklen_t len;
    int fd;

    addr = GC_NEW_ATOMIC(struct sockaddr_in);
    assert(addr != NULL);

    len = sizeof(struct sockaddr_in);
    fd = accept(sock, (struct sockaddr *) addr, &len);
    assert(fd >= 0);

    conn_insert_new(fd, CONN_UNKNOWN_IN, addr, GLOBAL_MAX_SIZE);

    connection_set_add(&connections_read, fd);
    printf("accepted connection from ");
    print_address(addr);
    printf("\n");
}

static struct message *handle_socket_event(struct connection **from) {
    int high, num, fd;
    fd_set rset, wset;

    assert(connections_listen.count != 0);

    /* prepare and select on all active connections */
    FD_ZERO(&rset);
    high = connection_set_collect(&connections_listen, &rset, 0);
    high = connection_set_collect(&connections_read, &rset, high);
    FD_ZERO(&wset);
    high = connection_set_collect(&connections_write, &wset, high);

    num = select(high + 1, &rset, &wset, NULL, NULL);

    /* writable socket is available--send a queued message */
    /* note: failed connects will show up here first, but they will also
       show up in the readable set. */
    if ((fd = connection_set_member(&connections_write, &wset)) > -1) {
        write_message(fd);
        return NULL;
    }

    /* readable socket is available--read a message */
    if ((fd = connection_set_member(&connections_read, &rset)) > -1) {
        return read_message(fd, from);
    }

    /* listening socket is available--accept a new incoming connection */
    if ((fd = connection_set_member(&connections_listen, &rset)) > -1) {
        accept_connection(fd);
        return NULL;
    }

    return NULL;
}

/*****************************************************************************/
/* Public API */

/* initialize data structures and start listening on a port */
void transport_init() {
    int fd;
    struct linger ling;

    connection_set_init(&connections_listen);
    connection_set_init(&connections_read);
    connection_set_init(&connections_write);

    /* initialize a listening port */
    assert(my_address != NULL);

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(fd >= 0);
    assert(bind(fd, (struct sockaddr *) my_address, sizeof(*my_address)) >= 0);
    assert(listen(fd, 5) >= 0);
    
    ling.l_onoff = 1;
    ling.l_linger = 0;
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
    connection_set_add(&connections_listen, fd);
}

struct transaction *get_transaction(void) {
    struct message *m;
    struct transaction *trans;
    struct connection *conn;

    do {
        m = handle_socket_event(&conn);
    } while (m == NULL);

    printMessage(stdout, m);

    trans = trans_lookup_remove(conn, m->tag);

    if (    conn->type == CONN_UNKNOWN_IN ||
            conn->type == CONN_CLIENT_IN ||
            conn->type == CONN_ENVOY_IN)
    {
        /* new, incoming request */
        assert(trans == NULL);

        trans = transaction_new();
    } else {
        /* response to an old, outgoing request */
        assert(trans != NULL);
    }

    trans->conn = conn;
    trans->in = m;

    return trans;
}

void put_transaction(struct transaction *trans) {
    /* do we need to open a socket? */
    if (trans->conn->fd < 0) {
        int res;
        int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        assert(fd >= 0);
        assert(fcntl(fd, O_NONBLOCK) >= 0);
        res = connect(fd, (struct sockaddr *) trans->conn->addr,
                sizeof(*trans->conn->addr));
        assert(res >= 0 || res == EINPROGRESS);
        trans->conn->fd = fd;
    }

    if (!conn_has_pending_write(trans->conn))
        connection_set_add(&connections_write, trans->conn->fd);

    conn_queue_write(trans);
}

/* Public API */
/*****************************************************************************/
