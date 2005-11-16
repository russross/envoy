#ifndef _FS_H_
#define _FS_H_

#include "transaction.h"

void forward_to_envoy(struct transaction *trans);

void handle_tversion(struct transaction *trans);

void client_twalk(struct transaction *trans);
void envoy_twalk(struct transaction *trans);

void handle_tauth(struct transaction *trans);
void handle_tattach(struct transaction *trans);
void handle_tflush(struct transaction *trans);
void handle_topen(struct transaction *trans);
void handle_tcreate(struct transaction *trans);
void handle_tread(struct transaction *trans);
void handle_twrite(struct transaction *trans);
void handle_tclunk(struct transaction *trans);
void handle_tremove(struct transaction *trans);
void handle_tstat(struct transaction *trans);
void handle_twstat(struct transaction *trans);

#endif
