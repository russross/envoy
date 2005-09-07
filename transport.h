#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include "9p.h"
#include "state.h"

void transport_init();
struct message *        get_message(struct connection **conn);
void                    put_message(struct transaction *trans);

#endif
