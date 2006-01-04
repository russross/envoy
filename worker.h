#ifndef _WORKER_H_
#define _WORKER_H_

#include <assert.h>
#include <pthread.h>
#include <setjmp.h>
#include <gc/gc.h>
#include <stdlib.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "transaction.h"

/* the order here is the order in which locks must be acquired */
enum worker_state_types {
    OBJECT_DIRECTORY,
    OBJECT_FD,
    OBJECT_TOP,
};

enum worker_transaction_states {
    WORKER_ZERO,
    WORKER_RETRY,
};

/* worker threads */
struct worker {
    pthread_cond_t *wait;
    void (*func)(Worker *, Transaction *);
    Transaction *arg;

    jmp_buf jmp;

    u16 errnum;
    List *cleanup;
};

#define worker_reserve(work, kind, obj) do { \
    while (obj->wait != NULL) \
        pthread_cond_wait(obj->wait); \
    obj->wait = GC_NEW_ATOMIC(pthread_cond_t); \
    assert(obj->wait != NULL); \
    pthread_cond_init(obj->wait, NULL); \
    work->cleanup = cons(cons(kind, obj), work->cleanup); \
} while (0)

#define worker_release(work, kind, obj) do { \
    pthread_cond_broadcast(obj->wait); \
    obj->wait = NULL; \
    assert(!null(work->cleanup) && cdar(work->cleanup) == obj); \
    work->cleanup = cdr(work->cleanup); \
} while (0)

void worker_cleanup(Worker *worker);

void worker_init(void);
void worker_lock_acquire(enum worker_state_types type);
void worker_lock_release(enum worker_state_types type);
void worker_create(void (*func)(Worker *, Transaction *), Transaction *arg);
void worker_wait(Transaction *trans);
void worker_wait_multiple(pthread_cond_t *wait);
void worker_wakeup(Transaction *trans);
void worker_wait_for_all(void);


#endif
