#include <assert.h>
#include <pthread.h>
#include <setjmp.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include "types.h"
#include "list.h"
#include "transaction.h"
#include "fid.h"
#include "state.h"
#include "worker.h"
#include "claim.h"
#include "lease.h"
#include "walk.h"

/*
 * Worker threads
 */

static void *worker_loop(Worker *t) {
    int i;

    lock();

    for (i = 0; i < THREAD_LIFETIME; i++) {
        if (i > 0) {
            /* wait in the pool for a request */
            state->thread_pool = append_elt(state->thread_pool, t);
            cond_wait(t->wait);
        }

        /* initialize the exception handler and catch exceptions */
        while (setjmp(t->jmp) == WORKER_RETRY) {
            /* wait for some condition to be true and retry */
            printf("WORKER_RETRY invoked\n");
            worker_cleanup(t);
        }

        /* service the request */
        if (t->func != NULL)
            t->func(t, t->arg);
        worker_cleanup(t);
        t->func = NULL;
    }

    unlock();

    return NULL;
}

void worker_create(void (*func)(Worker *, Transaction *), Transaction *arg) {
    if (null(state->thread_pool)) {
        Worker *t = GC_NEW(Worker);
        assert(t != NULL);
        t->wait = cond_new();

        t->func = func;
        t->arg = arg;

        pthread_t newthread;
        pthread_create(&newthread, NULL,
                (void *(*)(void *)) worker_loop, (void *) t);
    } else {
        Worker *t = car(state->thread_pool);
        state->thread_pool = cdr(state->thread_pool);

        t->func = func;
        t->arg = arg;

        cond_signal(t->wait);
    }
}

#define cleanup(var) do { \
    var = obj; \
    cond_broadcast(var->wait); \
    var->wait = NULL; \
} while (0)

void worker_cleanup(Worker *worker) {
    while (!null(worker->cleanup)) {
        Objectdir *dir;
        Openfile *file;
        Fid *fid;
        Claim *claim;
        enum lock_types type = (enum lock_types) caar(worker->cleanup);
        void *obj = cdar(worker->cleanup);
        worker->cleanup = cdr(worker->cleanup);

        switch (type) {
            case LOCK_DIRECTORY:        cleanup(dir);   break;
            case LOCK_OPENFILE:         cleanup(file);  break;
            case LOCK_FID:              cleanup(fid);   break;
            case LOCK_CLAIM:            cleanup(claim); break;
            case LOCK_LEASE:
                lease_finish_transaction((Lease *) obj);
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

void worker_retry(Worker *worker) {
    longjmp(worker->jmp, WORKER_RETRY);
}

void lock(void) {
    pthread_mutex_lock(state->biglock);
}

void unlock(void) {
    pthread_mutex_unlock(state->biglock);
}

void cond_signal(pthread_cond_t *cond) {
    pthread_cond_signal(cond);
}

void cond_broadcast(pthread_cond_t *cond) {
    pthread_cond_broadcast(cond);
}

void cond_wait(pthread_cond_t *cond) {
    pthread_cond_wait(cond, state->biglock);
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
        if (caar(cur) == (void *) type && cdar(cur) == object) {
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
