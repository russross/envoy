#ifndef _CONNECTION_H_
#define _CONNECTION_H_

#include <netinet/in.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "vector.h"
#include "transaction.h"
#include "worker.h"

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
    int envoyindex;
    enum conn_type type;
    struct sockaddr_in *netaddr;
    Address *addr;
    int maxSize;
    Vector *fid_vector;
    Vector *tag_vector;
    List *pending_writes;
    Transaction *notag_trans;
    Message *partial_in;
    int partial_in_bytes;
    Message *partial_out;
    int partial_out_bytes;
};

struct address {
    u32 ip;
    u16 port;
};

extern Vector *conn_vector;
extern int envoycount;

Connection *conn_insert_new(int fd, enum conn_type type,
        struct sockaddr_in *netaddr);
Connection *conn_insert_new_stub(Address *addr);
void conn_set_addr_envoy_in(Connection *conn, Address *addr);
Connection *conn_lookup_fd(int fd);
Connection *conn_get_envoy_out(Worker *worker, Address *addr);
Connection *conn_get_incoming(Address *addr);
Connection *conn_connect_to_storage(Address *addr);
Message *conn_get_pending_write(Connection *conn);
int conn_has_pending_write(Connection *conn);
void conn_queue_write(Connection *conn, Message *msg);
void conn_remove(Connection *conn);
void conn_init(void);

#endif
