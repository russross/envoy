#ifndef _DISPATCH_H_
#define _DISPATCH_H_

#include "state.h"
#include "map.h"
#include "types.h"

void send_request(Transaction *trans);
void send_requests(List *list);
void send_reply(Transaction *trans);
void queue_transaction(Transaction *trans);
void handle_error(Transaction *trans);

void main_loop(void);

Connection *connect_envoy(Address *addr);

#endif
