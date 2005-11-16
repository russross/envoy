#ifndef _WORKER_H_
#define _WORKER_H_

#include <pthread.h>
#include <gc/gc.h>
#include "types.h"
#include "transaction.h"

/* worker threads */
struct worker_thread {
    pthread_cond_t *wait;
    void * (*func)(void *);
    void *arg;
};

void worker_create(void * (*func)(void *), void *arg);
void worker_wait(Transaction *trans);
void worker_wait_multiple(pthread_cond_t *wait);
void worker_wakeup(Transaction *trans);
void worker_wait_for_all(void);

#endif
