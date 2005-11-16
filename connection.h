#ifndef _CONNECTION_H_
#define _CONNECTION_H_

#include "9p.h"
#include "types.h"
#include "vector.h"
#include "list.h"
#include "transaction.h"

/* connections */
enum conn_type {
    CONN_CLIENT_IN,
    CONN_ENVOY_IN,
    CONN_ENVOY_OUT,
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

Connection *conn_insert_new(
        int fd, enum conn_type type, Address *addr, int maxSize);
Connection *conn_new_unopened(enum conn_type type, Address *addr);
Connection *conn_lookup_fd(int fd);
Connection *conn_lookup_addr(Address *addr);
Transaction *conn_get_pending_write(Connection *conn);
int conn_has_pending_write(Connection *conn);
void conn_queue_write(Transaction *trans);
void conn_remove(Connection *conn);

#endif
