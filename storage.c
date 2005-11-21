#include "types.h"
#include "9p.h"
#include "transaction.h"
#include "storage.h"

void handle_tsreserve(Transaction *trans) {
    struct Tsreserve *req = &trans->in->msg.tsreserve;
    struct Rsreserve *res = &trans->in->msg.rsreserve;

}

void handle_tscreate(Transaction *trans) {
}

void handle_tsclone(Transaction *trans) {
}

void handle_tsread(Transaction *trans) {
}

void handle_tswrite(Transaction *trans) {
}

void handle_tsstat(Transaction *trans) {
}

void handle_tswstat(Transaction *trans) {
}
