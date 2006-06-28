#include <assert.h>
#include <pthread.h>
#include <setjmp.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "fid.h"
#include "config.h"
#include "worker.h"
#include "heap.h"
#include "oid.h"
#include "claim.h"
#include "lease.h"
#include "walk.h"

/* Static data */
u32 worker_next_priority;
Heap *worker_ready_to_run;
List *worker_thread_pool;
pthread_mutex_t *worker_biglock;

int worker_priority_cmp(const Worker *a, const Worker *b) {
    if (a->priority == b->priority)
        return 0;
    if (a->priority < b->priority &&
            b->priority - a->priority < WORKER_PRIORITY_THRESHOLD)
    {
        return -1;
    }
    return 1;
}

void worker_state_init(void) {
    worker_next_priority = 0;
    worker_ready_to_run =
        heap_new(WORKER_READY_QUEUE_SIZE, (Cmpfunc) worker_priority_cmp);
    worker_thread_pool = NULL;
    worker_biglock = GC_NEW(pthread_mutex_t);
    assert(worker_biglock != NULL);
    pthread_mutex_init(worker_biglock, NULL);
}

void worker_wake_up_next(void) {
    Worker *next = heap_remove(worker_ready_to_run);
    if (next == NULL)
        return;
    cond_signal(next->sleep);
}

/*
 * Worker threads
 */

static void *worker_loop(Worker *t) {
    int i;
    enum worker_transaction_states state;

    lock();

    for (i = 0; i < THREAD_LIFETIME; i++) {
        if (i > 0) {
            /* wait in the pool for a request */
            worker_thread_pool = append_elt(worker_thread_pool, t);
            cond_wait(t->sleep);
        }
        t->priority = worker_next_priority++;
        if (!heap_isempty(worker_ready_to_run)) {
            /* wait for older threads that are ready to run */
            heap_add(worker_ready_to_run, t);
            cond_wait(t->sleep);
        }

        /* initialize the exception handler and catch exceptions */
        do {
            state = setjmp(t->jmp);

            if (state == WORKER_BLOCKED) {
                /* wait for the blocking transaction to finish,
                 * then start over */
                printf("WORKER_BLOCKED sleeping\n");
                worker_cleanup(t);
                cond_wait(t->sleep);
                printf("WORKER_BLOCKED waking up\n");
                worker_wake_up_next();
            } else if (state == WORKER_RETRY) {
                worker_cleanup(t);
            }
        } while (state != WORKER_ZERO);

        /* service the request */
        if (t->func != NULL)
            t->func(t, t->arg);
        worker_cleanup(t);
        t->func = NULL;

        /* make anyone waiting on this transaction ready to run, then run one */
        if (!null(t->blocking)) {
            while (!null(t->blocking)) {
                heap_add(worker_ready_to_run, car(t->blocking));
                t->blocking = cdr(t->blocking);
            }
            worker_wake_up_next();
        }
    }

    unlock();

    return NULL;
}

void worker_create(void (*func)(Worker *, void *), void *arg) {
    if (null(worker_thread_pool)) {
        Worker *t = GC_NEW(Worker);
        assert(t != NULL);
        t->sleep = cond_new();

        t->func = func;
        t->arg = arg;
        t->priority = ~(u32) 0;
        t->blocking = NULL;

        pthread_t newthread;
        pthread_create(&newthread, NULL,
                (void *(*)(void *)) worker_loop, (void *) t);
    } else {
        Worker *t = car(worker_thread_pool);
        worker_thread_pool = cdr(worker_thread_pool);

        t->func = func;
        t->arg = arg;
        t->priority = ~(u32) 0;
        t->blocking = NULL;

        cond_signal(t->sleep);
    }
}

static void unlock_lease_cleanup(Lease *lease) {
    if (--lease->inflight == 0 && lease->wait_for_update != NULL)
        heap_add(worker_ready_to_run, lease->wait_for_update);
    /* note: we leave wait_for_update set to keep the lease locked */
}

#define cleanup(var) do { \
    var = obj; \
    var->lock = NULL; \
} while (0)

void worker_cleanup(Worker *worker) {
    while (!null(worker->cleanup)) {
        struct objectdir *dir;
        struct openfile *file;
        Fid *fid;
        Claim *claim;
        enum lock_types type = (enum lock_types) caar(worker->cleanup);
        void *obj = cdar(worker->cleanup);
        worker->cleanup = cdr(worker->cleanup);

        switch (type) {
            case LOCK_DIRECTORY:        cleanup(dir);   break;
            case LOCK_OPENFILE:         cleanup(file);  break;
            case LOCK_FID:              cleanup(fid);   break;
            case LOCK_CLAIM:
                cleanup(claim);
                claim_release((Claim *) obj);
                break;
            case LOCK_LEASE:
                unlock_lease_cleanup((Lease *) obj);
                break;
            case LOCK_WALK:
                walk_release((Walk *) obj);
                break;
            default:
                assert(0);
        }
    }
}
#undef cleanup

Worker *worker_attempt_to_acquire(Worker *worker, Worker *other) {
    assert(worker != NULL);
    if (other == NULL || other == worker)
        return worker;

    /* we lose */
    other->blocking = cons(worker, other->blocking);
    longjmp(worker->jmp, WORKER_BLOCKED);
    return NULL;
}

void worker_retry(Worker *worker) {
    longjmp(worker->jmp, WORKER_RETRY);
}

void lock(void) {
    pthread_mutex_lock(worker_biglock);
}

void unlock(void) {
    pthread_mutex_unlock(worker_biglock);
}

void lock_lease(Worker *worker, Lease *lease) {
    /* FIXME: think this through--also optimize repeat lock calls */
    worker_attempt_to_acquire(worker, lease->wait_for_update);
    lease->inflight++;
    worker_cleanup_add(worker, LOCK_LEASE, lease);
}

void unlock_lease(Worker *worker, Lease *lease) {
    worker_cleanup_remove(worker, LOCK_LEASE, lease);
    unlock_lease_cleanup(lease);
}

void cond_signal(pthread_cond_t *cond) {
    pthread_cond_signal(cond);
}

void cond_broadcast(pthread_cond_t *cond) {
    pthread_cond_broadcast(cond);
}

void cond_wait(pthread_cond_t *cond) {
    pthread_cond_wait(cond, worker_biglock);
}

pthread_cond_t *cond_new(void) {
    pthread_cond_t *cond = GC_NEW(pthread_cond_t);
    assert(cond != NULL);
    pthread_cond_init(cond, NULL);
    return cond;
}

void worker_cleanup_add(Worker *worker, enum lock_types type, void *object) {
    worker->cleanup = cons(cons((void *) type, object), worker->cleanup);
}

void worker_cleanup_remove(Worker *worker, enum lock_types type, void *object) {
    List *prev = NULL;
    List *cur = worker->cleanup;

    while (!null(cur)) {
        if ((enum lock_types) caar(cur) == type && cdar(cur) == object) {
            if (prev == NULL)
                worker->cleanup = cdr(cur);
            else
                setcdr(prev, cdr(cur));

            return;
        }

        prev = cur;
        cur = cdr(cur);
    }

    /* fail if we didn't find the requested entry */
    assert(0);
}
