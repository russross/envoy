#ifndef _CONFIG_H_
#define _CONFIG_H_

#include "types.h"
#include "9p.h"
#include "connection.h"

#define CONN_HASHTABLE_SIZE 32
#define CONN_VECTOR_SIZE 16
#define CONN_STORAGE_LRU_SIZE 16
#define TAG_VECTOR_SIZE 16
#define FID_VECTOR_SIZE 128
#define HANDLES_INITIAL_SIZE 32
#define THREAD_LIFETIME 16384
#define OBJECTDIR_CACHE_SIZE_STORAGE 512
#define OBJECTDIR_CACHE_SIZE_ENVOY 512
#define FD_CACHE_SIZE_STORAGE 256
#define FD_CACHE_SIZE_ENVOY 256
#define MAX_HOSTNAME 255
#define LEASE_HASHTABLE_SIZE 64
#define LEASE_FIDS_HASHTABLE_SIZE 128
#define LEASE_CLAIM_HASHTABLE_SIZE 1024
#define CLAIM_LRU_SIZE 256
#define LEASE_DIR_HASHTABLE_SIZE 32
#define LEASE_DIR_CACHE_SIZE 64
#define WALK_CACHE_SIZE 1024
#define WORKER_READY_QUEUE_SIZE 16
#define FID_REMOTE_VECTOR_SIZE 256
#define GROUP_HASHTABLE_SIZE 128
#define USER_HASHTABLE_SIZE 128
#define OBJECT_CACHE_STATE_SIZE 16384

#define ENVOY_PORT 9922
#define STORAGE_PORT 9923

#define BITS_PER_DIR_OBJECTS 6
#define BITS_PER_DIR_DIRS 8
#define BLOCK_SIZE 4096

extern int GLOBAL_MAX_SIZE;
#define GLOBAL_MIN_SIZE (BLOCK_SIZE + TSWRITE_DATA_OFFSET + 8)
extern int PORT;

extern Address *my_address;

extern int DEBUG_VERBOSE;
extern int DEBUG_STORAGE;
extern int DEBUG_PAYLOAD;

extern int isstorage;

/* storage servers */
extern char *objectroot;

/* envoy servers */
extern Address *root_address;
extern u64 root_oid;
extern int storage_server_count;
extern Connection **storage_servers;
extern Address **storage_addresses;

extern int DEBUG_CLIENT;
extern int DEBUG_ENVOY;
extern int DEBUG_ENVOY_ADMIN;
extern int DEBUG_AUDIT;

int config_envoy(int argc, char **argv);
int config_storage(int argc, char **argv);

#endif
