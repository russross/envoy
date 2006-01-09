#include <assert.h>
#include <pthread.h>
#include <setjmp.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include "types.h"
#include "list.h"
#include "transaction.h"
#include "fid.h"
#include "state.h"
#include "worker.h"
#include "forward.h"
#include "oid.h"

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
            /* wait for some condition to be try and retry */
            printf("WORKER_RETRY invoked\n");
            sleep(5);
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
        t->wait = new_cond();

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
        struct objectdir *dir;
        struct openfile *file;
        Fid *fid;
        Forward *fwd;
        enum lock_types type = (enum lock_types) caar(worker->cleanup);
        void *obj = cdar(worker->cleanup);
        worker->cleanup = cdr(worker->cleanup);

        switch (type) {
            case LOCK_DIRECTORY:        cleanup(dir);   break;
            case LOCK_OPENFILE:         cleanup(file);  break;
            case LOCK_FID:              cleanup(fid);   break;
            case LOCK_FORWARD:          cleanup(fwd);   break;
            default:
                assert(0);
        }
    }
}
#undef cleanup

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

pthread_cond_t *new_cond(void) {
    pthread_cond_t *cond = GC_NEW(pthread_cond_t);
    assert(cond != NULL);
    pthread_cond_init(cond, NULL);
    return cond;
}
