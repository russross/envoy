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
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "connection.h"
#include "handles.h"
#include "transaction.h"
#include "util.h"
#include "config.h"
#include "state.h"
#include "transport.h"
#include "dispatch.h"
#include "worker.h"

static void write_message(Connection *conn) {
    Message *msg;
    int bytes;

    while (conn_has_pending_write(conn)) {
        /* see if this is the continuation of a partial write */
        if (conn->partial_out == NULL) {
            conn->partial_out = msg = conn_get_pending_write(conn);
            conn->partial_out_bytes = bytes = 0;

            assert(msg != NULL);

            packMessage(msg, conn->maxSize);
            if (state->isstorage || msg->id < TSRESERVE)
                printMessage(stderr, msg);
        } else {
            msg = conn->partial_out;
            bytes = conn->partial_out_bytes;
        }

        /* keep trying to send the message */
        while (bytes < msg->size) {
            int res = send(conn->fd, msg->raw + bytes, msg->size - bytes,
                    MSG_DONTWAIT);

            /* would we block? */
            if (res < 0 && errno == EAGAIN)
                return;

            /* don't know how to handle any other errors... */
            /* TODO: we should handle connect failures here */
            assert(res > 0);

            /* record the bytes we sent */
            conn->partial_out_bytes = (bytes += res);
        }

        /* that message is finished */
        conn->partial_out = NULL;
        conn->partial_out_bytes = 0;
    }

    /* this was the last message in the queue so stop trying to write */
    handles_remove(state->handles_write, conn->fd);
}

static Message *read_message(Connection *conn) {
    Message *msg;
    int bytes, size;

    /* see if this is the continuation of a partial read */
    if (conn->partial_in == NULL) {
        conn->partial_in = msg = message_new();
        conn->partial_in_bytes = bytes = 0;
    } else {
        msg = conn->partial_in;
        bytes = conn->partial_in_bytes;
    }

    /* we start by reading the size field, then the whole message */
    size = (bytes < 4) ? 4 : msg->size;

    while (bytes < size) {
        int res =
            recv(conn->fd, msg->raw + bytes, size - bytes, MSG_DONTWAIT);

        /* did we run out of data? */
        if (res < 0 && errno == EAGAIN)
            return NULL;

        /* an error or connection closed from other side? */
        if (res <= 0)
            break;

        /* record the bytes we read */
        conn->partial_in_bytes = (bytes += res);

        /* read the message length once it's available and check it */
        if (bytes == 4) {
            int index = 0;
            if ((size = unpackU32(msg->raw, 4, &index)) > GLOBAL_MAX_SIZE)
                break;
            else
                msg->size = size;
            /*printf("read_message: size = %d\n", size);*/
        }

        /* have we read the whole message? */
        if (bytes == size) {
            if (unpackMessage(msg) < 0) {
                printf("read_message: unpack failure\n");
                break;
            } else {
                /* success */
                conn->partial_in = NULL;
                conn->partial_in_bytes = 0;
                return msg;
            }
        }
    }

    /* time to shut down this connection */
    printf("closing connection: ");
    print_address(conn->addr);
    printf("\n");

    /* close down the connection */
    conn_remove(conn);
    handles_remove(state->handles_read, conn->fd);
    if (conn_has_pending_write(conn))
        handles_remove(state->handles_write, conn->fd);
    close(conn->fd);

    return NULL;
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

    /* handles are guarded by connection lock */
    assert(state->handles_listen->high >= 0);

    /* prepare and select on all active connections */
    FD_ZERO(&rset);
    high = handles_collect(state->handles_listen, &rset, 0);
    high = handles_collect(state->handles_read, &rset, high);
    FD_ZERO(&wset);
    high = handles_collect(state->handles_write, &wset, high);

    /* give up the lock while we wait */
    unlock();
    num = select(high + 1, &rset, &wset, NULL, NULL);
    lock();

    /* writable socket is available--send a queued message */
    /* note: failed connects will show up here first, but they will also
       show up in the readable set. */
    if ((fd = handles_member(state->handles_write, &wset)) > -1) {
        Connection *conn = conn_lookup_fd(fd);
        assert(conn != NULL);
        write_message(conn);

        return NULL;
    }

    /* readable socket is available--read a message */
    if ((fd = handles_member(state->handles_read, &rset)) > -1) {
        /* was this a refresh request? */
        if (fd == state->refresh_pipe[0]) {
            char buff[16];
            if (read(fd, buff, 16) < 0)
                perror("handle_socket_event failed to read pipe");

            return NULL;
        }

        /* find the connection and store it for our caller */
        *from = conn_lookup_fd(fd);
        assert(*from != NULL);

        return read_message(*from);
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

    /* initialize a pipe that we can use to interrupt select */
    assert(pipe(state->refresh_pipe) == 0);
    handles_add(state->handles_read, state->refresh_pipe[0]);

    /* initialize a listening port */
    assert(state->my_address != NULL);

    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(fd >= 0);
    assert(bind(fd, (struct sockaddr *) state->my_address,
                sizeof(*state->my_address)) == 0);
    assert(listen(fd, 5) == 0);
    printf("listening at ");
    print_address(state->my_address);
    printf("\n");

    ling.l_onoff = 1;
    ling.l_linger = 0;
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
    handles_add(state->handles_listen, fd);
}

void put_message(Connection *conn, Message *msg) {
    assert(conn != NULL && msg != NULL && conn->fd >= 0);

    if (!conn_has_pending_write(conn)) {
        handles_add(state->handles_write, conn->fd);
        transport_refresh();
    }

    conn_queue_write(conn, msg);
}

int open_connection(Address *addr) {
    int flags;
    int fd;

    /* create it, set it to non-blocking, and start it connecting */
    if (    (fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0 ||
            (flags = fcntl(fd, F_GETFL, 0)) < 0 ||
            fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0 ||
            (connect(fd, (struct sockaddr *) addr, sizeof(*addr)) < 0 &&
             errno != EINPROGRESS))
    {
        if (fd >= 0)
            close(fd);
        return -1;
    }

    handles_add(state->handles_read, fd);

    printf("opened connection to ");
    print_address(addr);
    printf("\n");

    return fd;
}

void transport_refresh(void) {
    char *buff = "";
    if (write(state->refresh_pipe[1], buff, 1) < 0)
        perror("transport_refresh failed to write to pipe");
}

void main_loop(void) {
    Transaction *trans;
    Message *msg;
    Connection *conn;

    lock();

    for (;;) {
        /* handle any pending errors */
        if (!null(state->error_queue)) {
            printf("PANIC! Unhandled error\n");
            state->error_queue = cdr(state->error_queue);
            continue;
        }

        /* do a read or write, possibly returning a read message */
        do
            msg = handle_socket_event(&conn);
        while (msg == NULL);

        if (state->isstorage || msg->id < TSRESERVE)
            printMessage(stdout, msg);
        trans = trans_lookup_remove(conn, msg->tag);

        /* what kind of request/response is this? */
        switch (conn->type) {
            case CONN_UNKNOWN_IN:
            case CONN_CLIENT_IN:
            case CONN_ENVOY_IN:
            case CONN_STORAGE_IN:
                /* this is a new transaction */
                assert(trans == NULL);

                trans = trans_new(conn, msg, NULL);

                worker_create(dispatch, trans);
                break;

            case CONN_ENVOY_OUT:
            case CONN_STORAGE_OUT:
                /* this is a reply to a request we made */
                assert(trans != NULL);

                trans->in = msg;

                /* wake up the handler that is waiting for this message */
                cond_signal(trans->wait);
                break;

            default:
                assert(0);
        }
    }
}

Transaction *connect_envoy(Connection *conn) {
    Transaction *trans;
    struct Rversion *res;

    /* prepare a Tversion message and package it in a transaction */
    trans = trans_new(conn, NULL, message_new());
    trans->out->tag = NOTAG;
    trans->out->id = TVERSION;
    if (conn->type == CONN_ENVOY_OUT)
        set_tversion(trans->out, trans->conn->maxSize, "9P2000.envoy");
    else if (conn->type == CONN_STORAGE_OUT)
        set_tversion(trans->out, trans->conn->maxSize, "9P2000.storage");
    else
        assert(0);

    send_request(trans);

    /* check Rversion results and prepare a Tauth message */
    res = &trans->in->msg.rversion;

    /* blow up if the reply wasn't what we were expecting */
    if (trans->in->id != RVERSION ||
            (conn->type == CONN_ENVOY_OUT &&
             strcmp(res->version, "9P2000.envoy")) ||
            (conn->type == CONN_STORAGE_OUT &&
             strcmp(res->version, "9P2000.storage")))
    {
        return trans;
    }

    conn->maxSize = max(min(GLOBAL_MAX_SIZE, res->msize), GLOBAL_MIN_SIZE);

    return NULL;
}

/* Public API */
/*****************************************************************************/
