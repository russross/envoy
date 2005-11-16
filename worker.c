#include <assert.h>
#include <pthread.h>
#include <gc/gc.h>

#include "worker.h"
#include "state.h"

/*
 * Worker threads
 */

static void *worker_thread_loop(Worker *t) {
    state->active_worker_count++;
    pthread_mutex_lock(state->biglock);

    int i;
    for (i = 0; i < THREAD_LIFETIME; i++) {
        if (i > 0) {
            /* wait in the pool for a request */
            state->thread_pool = append_elt(state->thread_pool, t);
            pthread_cond_wait(t->wait, state->biglock);
        }

        /* service the request */
        if (t->func != NULL)
            t->func(t->arg);
        t->func = NULL;
        state->active_worker_count--;
        pthread_cond_signal(state->wait_workers);
    }

    pthread_mutex_unlock(state->biglock);
    return NULL;
}

void worker_create(void * (*func)(void *), void *arg) {
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
                (void *(*)(void *)) worker_thread_loop, (void *) t);
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
