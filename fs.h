#ifndef _FS_H_
#define _FS_H_

#include "9p.h"
#include "state.h"

/* handshake messages (connection type unknown) */
struct transaction *unknown_tversion(struct transaction *trans);
struct transaction *unknown_tauth(struct transaction *trans);
struct transaction *unknown_tread(struct transaction *trans);
struct transaction *unknown_twrite(struct transaction *trans);

/* messages from a client */
struct transaction *client_tattach(struct transaction *trans);
struct transaction *client_tflush(struct transaction *trans);
struct transaction *client_twalk(struct transaction *trans);
struct transaction *client_topen(struct transaction *trans);
struct transaction *client_tcreate(struct transaction *trans);
struct transaction *client_tread(struct transaction *trans);
struct transaction *client_twrite(struct transaction *trans);
struct transaction *client_tclunk(struct transaction *trans);
struct transaction *client_tremove(struct transaction *trans);
struct transaction *client_tstat(struct transaction *trans);
struct transaction *client_twstat(struct transaction *trans);

/* messages from another envoy */
struct transaction *envoy_tattach(struct transaction *trans);
struct transaction *envoy_tflush(struct transaction *trans);
struct transaction *envoy_twalk(struct transaction *trans);
struct transaction *envoy_topen(struct transaction *trans);
struct transaction *envoy_tcreate(struct transaction *trans);
struct transaction *envoy_tread(struct transaction *trans);
struct transaction *envoy_twrite(struct transaction *trans);
struct transaction *envoy_tclunk(struct transaction *trans);
struct transaction *envoy_tremove(struct transaction *trans);
struct transaction *envoy_tstat(struct transaction *trans);
struct transaction *envoy_twstat(struct transaction *trans);

#endif
