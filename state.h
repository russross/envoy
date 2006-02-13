#ifndef _STATE_H_
#define _STATE_H_

#include <pthread.h>
#include <gc/gc.h>
#include <netinet/in.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "vector.h"
#include "hashtable.h"
#include "handles.h"
#include "map.h"
#include "lru.h"

#define CONN_HASHTABLE_SIZE 64
#define CONN_VECTOR_SIZE 64
#define TAG_VECTOR_SIZE 64
#define FID_VECTOR_SIZE 64
#define FORWARD_VECTOR_SIZE 64
#define GLOBAL_FORWARD_VECTOR_SIZE 64
#define HANDLES_INITIAL_SIZE 32
#define THREAD_LIFETIME 1024
#define OBJECTDIR_CACHE_SIZE 32
#define FD_CACHE_SIZE 32
#define MAX_HOSTNAME 255
#define LEASE_HASHTABLE_SIZE 64

void print_address(Address *addr);
int addr_cmp(const Address *a, const Address *b);
u32 generic_hash(const void *elt, int len, u32 hash);
void state_dump(void);
Message *message_new(void);

/* persistent state */
struct state {
    int isstorage;
    Address *my_address;

    Handles *handles_listen;
    Handles *handles_read;
    Handles *handles_write;
    int *refresh_pipe;

    Vector *conn_vector;
    Vector *forward_fids;
    Hashtable *addr_2_conn;

    List *error_queue;

    pthread_mutex_t *biglock;

    List *thread_pool;

    Map *map;
    Hashtable *lease_owned;
    Hashtable *lease_shared;

    Lru *objectdir_lru;
    Lru *openfile_lru;
    u64 oid_next_available;
};

extern pthread_mutex_t *oid_dir_lock;
extern pthread_mutex_t *oid_fd_lock;

extern struct state *state;
void state_init_storage(void);
void state_init_envoy(void);

#endif
