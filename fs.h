#ifndef _FS_H_
#define _FS_H_

#include "9p.h"
#include "state.h"

/* handshake messages (connection type unknown) */
void unknown_tversion(struct transaction *trans);
void unknown_tauth(struct transaction *trans);
void unknown_tread(struct transaction *trans);
void unknown_twrite(struct transaction *trans);

/* messages from a client */
void client_tattach(struct transaction *trans);
void client_tflush(struct transaction *trans);
void client_twalk(struct transaction *trans);
void client_topen(struct transaction *trans);
void client_tcreate(struct transaction *trans);
void client_tread(struct transaction *trans);
void client_twrite(struct transaction *trans);
void client_tclunk(struct transaction *trans);
void client_tremove(struct transaction *trans);
void client_tstat(struct transaction *trans);
void client_twstat(struct transaction *trans);

/* messages from another envoy */
void envoy_tattach(struct transaction *trans);
void envoy_tflush(struct transaction *trans);
void envoy_twalk(struct transaction *trans);
void envoy_topen(struct transaction *trans);
void envoy_tcreate(struct transaction *trans);
void envoy_tread(struct transaction *trans);
void envoy_twrite(struct transaction *trans);
void envoy_tclunk(struct transaction *trans);
void envoy_tremove(struct transaction *trans);
void envoy_tstat(struct transaction *trans);
void envoy_twstat(struct transaction *trans);

#endif
