#ifndef _STATE_H_
#define _STATE_H_

#include <pthread.h>
#include <gc/gc.h>
#include <netinet/in.h>
#include "types.h"
#include "list.h"
#include "vector.h"
#include "hashtable.h"
#include "handles.h"
#include "map.h"

#define CONN_HASHTABLE_SIZE 64
#define CONN_VECTOR_SIZE 64
#define TAG_VECTOR_SIZE 64
#define FID_VECTOR_SIZE 64
#define FORWARD_VECTOR_SIZE 64
#define GLOBAL_FORWARD_VECTOR_SIZE 64
#define HANDLES_INITIAL_SIZE 32
#define THREAD_LIFETIME 1024

void print_address(Address *addr);
int addr_cmp(const Address *a, const Address *b);
void state_dump(void);
Message *message_new(void);

/* persistent state */
struct state {
    pthread_mutex_t *biglock;
    Vector *conn_vector;
    Vector *forward_fids;
    Hashtable *addr_2_conn;
    Handles *handles_listen;
    Handles *handles_read;
    Handles *handles_write;
    Address *my_address;
    Map *map;
    List *error_queue;
    List *thread_pool;
    pthread_cond_t *wait_workers;
    int active_worker_count;
};

extern struct state *state;
void state_init(void);

#endif
