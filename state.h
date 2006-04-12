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
#define LEASE_FIDS_HASHTABLE_SIZE 64
#define LEASE_WALK_LRU_SIZE 64
#define LEASE_CLAIM_LRU_SIZE 64
#define LEASE_CLAIM_HASHTABLE_SIZE 64

void print_address(Address *addr);
int addr_cmp(const Address *a, const Address *b);
u32 generic_hash(const void *elt, int len, u32 hash);
u32 string_hash(const char *str);
void state_dump(void);
Message *message_new(void);

/* persistent state */
struct state {
    int isstorage;
    Address *my_address;
    Address *root_address;

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
