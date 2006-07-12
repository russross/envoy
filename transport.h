#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include <stdlib.h>
#include <netinet/in.h>
#include "types.h"
#include "connection.h"
#include "transaction.h"

void transport_init(void);
void put_message(Connection *conn, Message *msg);
int open_connection(struct sockaddr_in *netaddr);
void transport_refresh(void);
void main_loop(void);
/* returns NULL on success, failed transaction on failure */
Transaction *connect_envoy(Connection *conn);

#endif
