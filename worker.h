#ifndef _WORKER_H_
#define _WORKER_H_

#include <pthread.h>
#include <setjmp.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <unistd.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "transaction.h"
#include "lease.h"

#define WORKER_PRIORITY_THRESHOLD 0x80000000L

enum lock_types {
    LOCK_DIRECTORY,
    LOCK_OPENFILE,
    LOCK_FID,
    LOCK_CLAIM,
    LOCK_LEASE,
    LOCK_LEASE_EXCLUSIVE,
    LOCK_WALK,
    LOCK_REMOTE_FID,
};

enum worker_transaction_states {
    WORKER_ZERO,
    WORKER_BLOCKED,
    WORKER_RETRY,
    WORKER_MULTISTEP,
};

/* worker threads */
struct worker {
    pthread_cond_t *sleep;
    void (*func)(Worker *, void *);
    Transaction *arg;

    jmp_buf jmp;
    u16 errnum;
    List *cleanup;

    u32 priority;
    List *blocking;
};

#define reserve(work, kind, obj) do { \
    if (work != obj->lock) { \
        obj->lock = worker_attempt_to_acquire(work, obj->lock); \
        worker_cleanup_add(work, kind, obj); \
    } \
} while (0)

#define release(work, kind, obj) do { \
    obj->lock = NULL; \
    worker_cleanup_remove(work, kind, obj); \
} while (0)

void worker_create(void (*func)(Worker *, void *), void *arg);
void worker_cleanup(Worker *worker);
void worker_retry(Worker *worker);

void lock(void);
void unlock(void);
void lock_lease(Worker *worker, Lease *lease);
void lock_lease_exclusive(Worker *worker, Lease *lease);
void lock_lease_extend_multistep(Worker *worker, Lease *lease);

void cond_signal(pthread_cond_t *var);
void cond_broadcast(pthread_cond_t *var);
void cond_wait(pthread_cond_t *var);
pthread_cond_t *cond_new(void);

Worker *worker_attempt_to_acquire(Worker *worker, Worker *other);
void worker_cleanup_add(Worker *worker, enum lock_types type, void *object);
void worker_cleanup_remove(Worker *worker, enum lock_types type, void *object);

void worker_state_init(void);
void worker_commit(Worker *worker);
int worker_active_count(void);

void worker_multistep_transfer_request(Worker *worker,
        void (*func)(Worker *, void *), void *arg);
void worker_multistep_wait(Worker *worker);

#endif
