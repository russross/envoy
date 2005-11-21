#ifndef _FS_H_
#define _FS_H_

#include "types.h"
#include "transaction.h"

void forward_to_envoy(Transaction *trans);

void handle_tversion(Transaction *trans);

void client_twalk(Transaction *trans);
void envoy_twalk(Transaction *trans);

void handle_tauth(Transaction *trans);
void handle_tattach(Transaction *trans);
void handle_tflush(Transaction *trans);
void handle_topen(Transaction *trans);
void handle_tcreate(Transaction *trans);
void handle_tread(Transaction *trans);
void handle_twrite(Transaction *trans);
void handle_tclunk(Transaction *trans);
void handle_tremove(Transaction *trans);
void handle_tstat(Transaction *trans);
void handle_twstat(Transaction *trans);

#endif
