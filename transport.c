#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include "types.h"
#include "9p.h"
#include "connection.h"
#include "handles.h"
#include "config.h"
#include "state.h"
#include "transport.h"
#include "worker.h"

static void write_message(int fd) {
    Connection *conn;
    Message *msg;
    int len;

    assert((conn = conn_lookup_fd(fd)) != NULL);
    msg = conn_get_pending_write(conn);

    if (msg != NULL) {
        /* if this was the last message in the queue, stop trying to write */
        if (!conn_has_pending_write(conn))
            handles_remove(state->handles_write, fd);

        packMessage(msg, conn->maxSize);
        printMessage(stderr, msg);

        len = send(conn->fd, msg->raw, msg->size, 0);

        assert(len == msg->size);
    }
}

static Message *read_message(int fd, Connection **from) {
    Message *m = message_new();
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
        handles_remove(state->handles_read, fd);
        if (conn_has_pending_write(*from))
            handles_remove(state->handles_write, fd);
        close(fd);

        return NULL;
    }

    return m;
}

static void accept_connection(int sock) {
    Address *addr;
    socklen_t len;
    int fd;

    addr = GC_NEW_ATOMIC(Address);
    assert(addr != NULL);

    len = sizeof(Address);
    fd = accept(sock, (struct sockaddr *) addr, &len);
    assert(fd >= 0);

    conn_insert_new(fd, CONN_UNKNOWN_IN, addr);

    handles_add(state->handles_read, fd);
    printf("accepted connection from ");
    print_address(addr);
    printf("\n");
}

/* select on all our open sockets and dispatch when one is ready */
static Message *handle_socket_event(Connection **from) {
    int high, num, fd;
    fd_set rset, wset;

    /* only proceed when everyone else is waiting on us */
    worker_wait_for_all();

    assert(state->handles_listen->high >= 0);

    /* prepare and select on all active connections */
    FD_ZERO(&rset);
    high = handles_collect(state->handles_listen, &rset, 0);
    high = handles_collect(state->handles_read, &rset, high);
    FD_ZERO(&wset);
    high = handles_collect(state->handles_write, &wset, high);

    num = select(high + 1, &rset, &wset, NULL, NULL);

    /* writable socket is available--send a queued message */
    /* note: failed connects will show up here first, but they will also
       show up in the readable set. */
    if ((fd = handles_member(state->handles_write, &wset)) > -1) {
        write_message(fd);
        return NULL;
    }

    /* readable socket is available--read a message */
    if ((fd = handles_member(state->handles_read, &rset)) > -1) {
        return read_message(fd, from);
    }

    /* listening socket is available--accept a new incoming connection */
    if ((fd = handles_member(state->handles_listen, &rset)) > -1) {
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

    /* initialize a listening port */
    assert(state->my_address != NULL);

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(fd >= 0);
    assert(bind(fd, (struct sockaddr *) state->my_address,
                sizeof(*state->my_address)) >= 0);
    assert(listen(fd, 5) >= 0);
    
    ling.l_onoff = 1;
    ling.l_linger = 0;
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
    handles_add(state->handles_listen, fd);
}

Message *get_message(Connection **conn) {
    Message *msg = NULL;

    while (msg == NULL)
        msg = handle_socket_event(conn);

    printMessage(stdout, msg);

    return msg;
}

void put_message(Connection *conn, Message *msg) {
    assert(conn != NULL && msg != NULL && conn->fd >= 0);

    if (!conn_has_pending_write(conn))
        handles_add(state->handles_write, conn->fd);

    conn_queue_write(conn, msg);
}

int open_connection(Address *addr) {
    int status, flags;
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (fd < 0)
        return fd;
    if ((flags = fcntl(fd, F_GETFL, 0)) < 0) {
        close(fd);
        return flags;
    }
    if ((status = fcntl(fd, F_SETFL, flags | O_NONBLOCK)) < 0) {
        close(fd);
        return status;
    }

    status = connect(fd, (struct sockaddr *) addr, sizeof(*addr));

    if (status < 0 && errno != EINPROGRESS) {
        close(fd);
        return status;
    }

    handles_add(state->handles_read, fd);

    printf("opened connection to ");
    print_address(addr);
    printf("\n");

    return fd;
}

/* Public API */
/*****************************************************************************/
