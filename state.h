#ifndef _STATE_H_
#define _STATE_H_

#include <pthread.h>
#include <gc/gc.h>
#include <netinet/in.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "handles.h"
#include "lru.h"

#define CONN_HASHTABLE_SIZE 64
#define CONN_VECTOR_SIZE 64
#define CONN_STORAGE_LRU_SIZE 64
#define TAG_VECTOR_SIZE 64
#define FID_VECTOR_SIZE 64
#define HANDLES_INITIAL_SIZE 32
#define THREAD_LIFETIME 1024
#define OBJECTDIR_CACHE_SIZE 32
#define FD_CACHE_SIZE 32
#define MAX_HOSTNAME 255
#define LEASE_HASHTABLE_SIZE 64
#define LEASE_FIDS_HASHTABLE_SIZE 64
#define LEASE_WALK_LRU_SIZE 64
#define LEASE_CLAIM_LRU_SIZE 64
#define LEASE_CLAIM_HASHTABLE_SIZE 64
#define CLAIM_HASHTABLE_SIZE 64
#define WALK_CACHE_SIZE 256
#define WORKER_READY_QUEUE_SIZE 64

void print_address(Address *addr);
void state_dump(void);
Message *message_new(void);
struct p9stat *p9stat_new(void);

/* persistent state */
struct state {
    int isstorage;
    Address *my_address;
    Address *root_address;

    Handles *handles_listen;
    Handles *handles_read;
    Handles *handles_write;
    int *refresh_pipe;

    List *error_queue;

    pthread_mutex_t *biglock;

    List *thread_pool;

    Lru *objectdir_lru;
    Lru *openfile_lru;
    u64 oid_next_available;
};

extern struct state *state;
void state_init_storage(void);
void state_init_envoy(void);
void storage_server_connection_init(void);

#endif
