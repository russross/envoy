#ifndef _STORAGE_H_
#define _STORAGE_H_

#include "types.h"
#include "transaction.h"
#include "worker.h"

void handle_tsreserve(Worker *worker, Transaction *trans);
void handle_tscreate(Worker *worker, Transaction *trans);
void handle_tsclone(Worker *worker, Transaction *trans);
void handle_tsread(Worker *worker, Transaction *trans);
void handle_tswrite(Worker *worker, Transaction *trans);
void handle_tsstat(Worker *worker, Transaction *trans);
void handle_tswstat(Worker *worker, Transaction *trans);

#endif
