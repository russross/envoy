#ifndef _DISPATCH_H_
#define _DISPATCH_H_

#include "types.h"
#include "list.h"
#include "transaction.h"
#include "worker.h"

void send_request(Transaction *trans);
void send_requests(List *list);
void send_reply(Transaction *trans);
void handle_error(Worker *worker, Transaction *trans);
void dispatch(Worker *worker, Transaction *trans);

extern List *dispatch_error_queue;

#endif
