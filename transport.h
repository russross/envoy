#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include <netinet/in.h>
#include "types.h"
#include "connection.h"
#include "worker.h"

void transport_init(void);
void put_message(Connection *conn, Message *msg);
int open_connection(Address *addr);
void transport_refresh(void);
void main_loop(void);
int connect_envoy(Worker *worker, Connection *conn);

#endif
