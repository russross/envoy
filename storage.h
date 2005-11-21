#ifndef _STORAGE_H_
#define _STORAGE_H_

#include "types.h"
#include "transaction.h"

void handle_tsreserve(Transaction *trans);
void handle_tscreate(Transaction *trans);
void handle_tsclone(Transaction *trans);
void handle_tsread(Transaction *trans);
void handle_tswrite(Transaction *trans);
void handle_tsstat(Transaction *trans);
void handle_tswstat(Transaction *trans);

#endif
