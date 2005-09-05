#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include "9p.h"

#define PORT 9922

void transport_init();
struct transaction *    get_transaction(void);
void                    put_transaction(struct transaction *trans);

#endif
