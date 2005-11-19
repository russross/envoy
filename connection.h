#ifndef _CONNECTION_H_
#define _CONNECTION_H_

#include <netinet/in.h>
#include "types.h"
#include "list.h"
#include "vector.h"
#include "transaction.h"

/* connections */
enum conn_type {
    CONN_CLIENT_IN,
    CONN_ENVOY_IN,
    CONN_ENVOY_OUT,
    CONN_STORAGE_IN,
    CONN_STORAGE_OUT,
    CONN_UNKNOWN_IN
};

struct connection {
    int fd;
    enum conn_type type;
    Address *addr;
    int maxSize;
    Vector *fid_vector;
    Vector *forward_vector;
    Vector *tag_vector;
    List *pending_writes;
    Transaction *notag_trans;
};

Connection *conn_insert_new(int fd, enum conn_type type, Address *addr);
Connection *conn_lookup_fd(int fd);
Connection *conn_get_from_addr(Address *addr);
Message *conn_get_pending_write(Connection *conn);
int conn_has_pending_write(Connection *conn);
void conn_queue_write(Connection *conn, Message *msg);
void conn_remove(Connection *conn);

#endif
