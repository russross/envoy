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
#include "state.h"
#include "worker.h"
#include "oid.h"

/*
 * Worker threads
 */

static pthread_mutex_t *locks[LOCK_TOP];

void worker_init(void) {
    int i;
    for (i = 0; i < LOCK_TOP; i++) {
        locks[i] = GC_NEW(pthread_mutex_t);
        assert(locks[i] != NULL);
        pthread_mutex_init(locks[i], NULL);
    }
}

void worker_lock_acquire(enum lock_types type) {
    assert(type >= 0 && type < LOCK_TOP);

    pthread_mutex_lock(locks[type]);
}

void worker_lock_release(enum lock_types type) {
    assert(type >= 0 && type < LOCK_TOP);

    pthread_mutex_unlock(locks[type]);
}

static void *worker_loop(Worker *t) {
    state->active_worker_count++;
    pthread_mutex_lock(state->biglock);

    int i;
    for (i = 0; i < THREAD_LIFETIME; i++) {
        if (i > 0) {
            /* wait in the pool for a request */
            state->thread_pool = append_elt(state->thread_pool, t);
            pthread_cond_wait(t->wait, state->biglock);
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
        state->active_worker_count--;
        pthread_cond_signal(state->wait_workers);
    }

    pthread_mutex_unlock(state->biglock);
    return NULL;
}

void worker_create(void (*func)(Worker *, Transaction *), Transaction *arg) {
    if (null(state->thread_pool)) {
        Worker *t = GC_NEW(Worker);
        assert(t != NULL);
        t->wait = GC_NEW(pthread_cond_t);
        assert(t->wait != NULL);
        pthread_cond_init(t->wait, NULL);

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

        state->active_worker_count++;
        pthread_cond_signal(t->wait);
    }
}

void worker_wait(Transaction *trans) {
    state->active_worker_count--;
    pthread_cond_signal(state->wait_workers);
    pthread_cond_wait(trans->wait, state->biglock);
}

void worker_wait_multiple(pthread_cond_t *wait) {
    state->active_worker_count--;
    pthread_cond_signal(state->wait_workers);
    pthread_cond_wait(wait, state->biglock);
}

void worker_wakeup(Transaction *trans) {
    state->active_worker_count++;
    pthread_cond_signal(trans->wait);
}

void worker_wait_for_all(void) {
    while (state->active_worker_count > 0)
        pthread_cond_wait(state->wait_workers, state->biglock);
}

#define cleanup(var) do { \
    var = obj; \
    pthread_cond_broadcast(var->wait); \
    var->wait = NULL; \
} while (0)

void worker_cleanup(Worker *worker) {
    while (!null(worker->cleanup)) {
        struct oid_dir *dir;
        struct oid_fd *fd;
        enum lock_types type = (enum lock_types) caar(worker->cleanup);
        void *obj = cdar(worker->cleanup);
        worker->cleanup = cdr(worker->cleanup);
        worker_lock_acquire(type);

        switch (type) {
            case LOCK_DIRECTORY:        cleanup(dir);   break;
            case LOCK_FD:               cleanup(fd);    break;
            default:
                assert(0);
        }

        worker_lock_release(type);
    }
}
#undef cleanup
