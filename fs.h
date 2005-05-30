#ifndef _FS_H_
#define _FS_H_

#include "9p.h"
#include "state.h"

struct transaction *client_tversion(struct transaction *trans);
struct transaction *client_tauth(struct transaction *trans);
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

#endif
