#ifndef _TYPES_H_
#define _TYPES_H_

#include <netinet/in.h>

typedef struct sockaddr_in Address;
typedef struct transaction Transaction;
typedef struct connection Connection;
typedef struct worker_thread Worker;
typedef struct fid Fid;
typedef struct forward Forward;
typedef struct message Message;
typedef struct hashtable Hashtable;
typedef struct map Map;
typedef struct vector Vector;
typedef struct list List;
typedef struct handles Handles;

#endif
