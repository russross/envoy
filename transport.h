#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include <netinet/in.h>
#include "types.h"
#include "connection.h"

void transport_init(void);
Message *get_message(Connection **conn);
void put_message(Connection *conn, Message *msg);
int open_connection(Address *addr);
void transport_refresh(void);

#endif
