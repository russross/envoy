#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <gc.h>
#include "9p.h"
#include "config.h"
#include "transport.h"
#include "state.h"

static int connections[32];
static int connection_count = 0;

void start_listening() {
    struct sockaddr_in servaddr;
    int fd;
    struct linger ling;

    assert(connection_count == 0);

    bzero(&servaddr, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(PORT);
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(fd >= 0);
    assert(bind(fd, (struct sockaddr *) &servaddr, sizeof(servaddr)) >= 0);
    assert(listen(fd, 5) >= 0);
    
    ling.l_onoff = 1;
    ling.l_linger = 0;
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &ling, sizeof(ling));
    connections[connection_count++] = fd;
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

    connections[connection_count++] = fd;
    printf("accepted connection from ");
    print_address(addr);
    printf("\n");
}

static struct message *get_message_help(struct connection **conn) {
    int i, high;
    int num;
    fd_set rset;

    assert(connection_count != 0);

    high = 0;
    FD_ZERO(&rset);
    for (i = 0; i < connection_count; i++) {
        FD_SET(connections[i], &rset);
        high = high > connections[i] ? high : connections[i]; 
    }

    num = select(high + 1, &rset, NULL, NULL, NULL);

    if (FD_ISSET(connections[0], &rset)) {
        accept_connection(connections[0]);
        return NULL;
    }

    for (i = 1; i < connection_count; i++) {
        if (FD_ISSET(connections[i], &rset)) {
            struct message *m = message_new();
            int index = 0;
            int count;

            count = recv(connections[i], m->raw, 4, MSG_WAITALL);
            if (count == 0) {
                /* close the socket */
                struct connection *conn = conn_lookup_fd(connections[i]);
                printf("closing connection from  ");
                print_address(conn->addr);
                printf("\n");
                conn_remove(conn);
                close(connections[i]);
                for (index = i; index + 1 < connection_count; index++)
                    connections[index] = connections[index + 1];
                connection_count--;
                exit(0);
                return NULL;
            }

            if (count != 4)
                fprintf(stderr, "count != 4: %d\n", count);
            assert(count == 4);
            m->size = unpackU32(m->raw, 4, &index);
            assert(m->size <= GLOBAL_MAX_SIZE);
            assert(recv(connections[i], m->raw + 4, m->size - 4,
                        MSG_WAITALL) == m->size - 4);
            if (unpackMessage(m) < 0) {
                fprintf(stderr, "failure unpacking message\n");
                return NULL;
            }
            *conn = conn_lookup_fd(connections[i]);
            assert(*conn != NULL);
            return m;
        }
    }
    return NULL;
}

struct transaction *get_transaction(void) {
    struct message *m;
    struct transaction *trans;
    struct connection *conn;

    do {
        m = get_message_help(&conn);
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
    int len;

    assert(trans != NULL);
    assert(trans->conn != NULL);
    assert(trans->out != NULL);

    packMessage(trans->out);

    if (    trans->conn->type == CONN_ENVOY_OUT ||
            trans->conn->type == CONN_STORAGE_OUT)
    {
        /* this is an outgoing request, so get ready for a response */
        assert(trans->handler != NULL);
        assert(trans->in == NULL);

        trans_insert(trans);
    } else {
        assert(trans->in != NULL);
    }

    printMessage(stderr, trans->out);

    len = send(trans->conn->fd, trans->out->raw, trans->out->size, 0);

    assert(len == trans->out->size);
}
