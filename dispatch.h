#ifndef _DISPATCH_H_
#define _DISPATCH_H_

#include "state.h"
#include "map.h"

void send_request(struct transaction *trans);
void send_reply(struct transaction *trans);
void queue_transaction(struct transaction *trans);
void handle_error(struct transaction *trans);

void main_loop(void);

struct connection *connect_envoy(struct sockaddr_in *addr);

#endif
