#ifndef _TRANSACTION_H_
#define _TRANSACTION_H_

#include <assert.h>
#include <pthread.h>
#include <gc/gc.h>
#include "9p.h"
#include "types.h"

struct transaction {
    pthread_cond_t *wait;

    Connection *conn;
    Message *in;
    Message *out;
};

Transaction *trans_new(Connection *conn, Message *in, Message *out);
void trans_insert(Transaction *trans);
Transaction *trans_lookup_remove(Connection *conn, u16 tag);

#endif
