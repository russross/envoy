#ifndef _ENVOY_H_
#define _ENVOY_H_

#include "types.h"
#include "9p.h"
#include "connection.h"
#include "transaction.h"
#include "worker.h"

int has_permission(char *uname, struct p9stat *info, u32 required);

void handle_tversion(Worker *worker, Transaction *trans);

void client_twalk(Worker *worker, Transaction *trans);
void envoy_tewalkremote(Worker *worker, Transaction *trans);

void handle_tauth(Worker *worker, Transaction *trans);
void handle_tattach(Worker *worker, Transaction *trans);
void handle_tflush(Worker *worker, Transaction *trans);
void handle_topen(Worker *worker, Transaction *trans);
void handle_tcreate(Worker *worker, Transaction *trans);
void handle_tcreate_admin(Worker *worker, Transaction *trans);
void handle_tread(Worker *worker, Transaction *trans);
void handle_twrite(Worker *worker, Transaction *trans);
void handle_tclunk(Worker *worker, Transaction *trans);
void handle_tremove(Worker *worker, Transaction *trans);
void handle_tstat(Worker *worker, Transaction *trans);
void handle_twstat(Worker *worker, Transaction *trans);

void envoy_teclosefid(Worker *worker, Transaction *trans);
void client_shutdown(Worker *worker, Connection *conn);

#endif
