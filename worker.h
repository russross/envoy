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
enum lock_types {
    LOCK_DIRECTORY,
    LOCK_OPENFILE,
    LOCK_FID,
    LOCK_CLAIM,
    LOCK_LEASE,
    LOCK_WALK,
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

#define reserve(work, kind, obj) do { \
    while (obj->wait != NULL) \
        cond_wait(obj->wait); \
    obj->wait = cond_new(); \
    work->cleanup = cons(cons((void *) kind, obj), work->cleanup); \
} while (0)

#define release(work, kind, obj) do { \
    cond_broadcast(obj->wait); \
    obj->wait = NULL; \
    assert(!null(work->cleanup) && cdar(work->cleanup) == obj); \
    work->cleanup = cdr(work->cleanup); \
} while (0)

#define lock_lease(work, obj) do { \
    if (obj->wait_for_update != NULL) { \
        cond_wait(obj->wait_for_update); \
        worker_retry(work); \
    } \
    obj->inflight++; \
    work->cleanup = cons(cons((void *) LOCK_LEASE, obj), work->cleanup); \
} while(0)

void worker_create(void (*func)(Worker *, Transaction *), Transaction *arg);
void worker_cleanup(Worker *worker);
void worker_retry(Worker *worker);

void lock(void);
void unlock(void);
void cond_signal(pthread_cond_t *var);
void cond_broadcast(pthread_cond_t *var);
void cond_wait(pthread_cond_t *var);
pthread_cond_t *cond_new(void);

#endif
