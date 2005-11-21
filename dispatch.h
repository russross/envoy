#ifndef _DISPATCH_H_
#define _DISPATCH_H_

#include "types.h"
#include "list.h"
#include "connection.h"
#include "transaction.h"

void send_request(Transaction *trans);
void send_requests(List *list);
void send_reply(Transaction *trans);
void handle_error(Transaction *trans);

void main_loop(void);

int connect_envoy(Connection *conn);

#endif
