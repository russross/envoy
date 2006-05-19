#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "connection.h"
#include "util.h"
#include "config.h"
#include "state.h"
#include "transport.h"
#include "envoy.h"
#include "worker.h"
#include "oid.h"

void test_dump(void) {
    Message m;
    char *path[] = { "local", "scratch", "rgr22", "1234567" };
    u8 *data;
    int size;
    struct p9stat *stat;

    m.raw = GC_MALLOC_ATOMIC(GLOBAL_MAX_SIZE);

    m.id = TVERSION;
    m.tag = 1;
    m.msg.tversion.msize = 8192;
    m.msg.tversion.version = "9P2000.u";

    printMessage(stdout, &m);
    packMessage(&m, GLOBAL_MAX_SIZE);
    printf("packed size = %d\n", m.size);
    dumpBytes(stdout, "    ", m.raw, m.size);

    unpackMessage(&m);
    printMessage(stdout, &m);

    m.id = TWALK;
    m.tag = 2;
    m.msg.twalk.fid = 3;
    m.msg.twalk.newfid = 4;
    m.msg.twalk.nwname = 4;
    m.msg.twalk.wname = path;

    printMessage(stdout, &m);

    packMessage(&m, GLOBAL_MAX_SIZE);
    printf("packed size = %d\n", m.size);
    dumpBytes(stdout, "    ", m.raw, m.size);

    m.id = RSTAT;
    m.tag = 3;
    stat = m.msg.rstat.stat = GC_NEW(struct p9stat);
    m.msg.rstat.stat->type = 128;
    m.msg.rstat.stat->dev = 129;
    m.msg.rstat.stat->qid.type = 255;
    m.msg.rstat.stat->qid.version = 512;
    m.msg.rstat.stat->qid.path = 0x1234567890123456LL;
    m.msg.rstat.stat->mode = 0644;
    m.msg.rstat.stat->atime = time(NULL);
    m.msg.rstat.stat->mtime = time(NULL) - 10;
    m.msg.rstat.stat->length = 42;
    m.msg.rstat.stat->name = "envoy.c";
    m.msg.rstat.stat->uid = "22";
    m.msg.rstat.stat->gid = "44";
    m.msg.rstat.stat->muid = "23";
    m.msg.rstat.stat->n_uid = 22;
    m.msg.rstat.stat->n_gid = 44;
    m.msg.rstat.stat->n_muid = 23;
    m.msg.rstat.stat->extension = "/usr/bin/sh";

    printMessage(stdout, &m);

    packMessage(&m, GLOBAL_MAX_SIZE);
    printf("packed size = %d\n", m.size);
    dumpBytes(stdout, "    ", m.raw, m.size);

    m.id = RREAD;
    m.tag = 4;
    data = GC_MALLOC_ATOMIC(32768);
    size = 0;
    packStat(data, &size, stat);
    packStat(data, &size, stat);
    packStat(data, &size, stat);
    m.msg.rread.count = size;
    m.msg.rread.data = data;

    printMessage(stdout, &m);

    packMessage(&m, GLOBAL_MAX_SIZE);
    printf("packed size = %d\n", m.size);
    dumpBytes(stdout, "    ", m.raw, m.size);
}

/*
void test_map(void) {
    Map *root = GC_NEW(Map);
    List *a, *b;
    Address *addr = GC_NEW_ATOMIC(Address);
    assert(root != NULL);
    root->prefix = NULL;
    root->addr = addr;
    root->nchildren = 0;
    root->children = NULL;

    a = cons("home", cons("rgr22", NULL));
    map_insert(root, a, addr);
    b = cons("usr", cons("bin", NULL));
    map_insert(root, b, addr);
    a = cons("home", NULL);
    map_insert(root, a, addr);
    dumpMap(root, "");
}
*/

/*
void *test_connect(void *arg) {
    Address *addr = make_address("boulderdash", ENVOY_PORT);
    if (!memcmp(&addr->sin_addr, &state->my_address->sin_addr,
                sizeof(addr->sin_addr)))
    {
        printf("I am boulderdash\n");
    } else {
        Connection *conn = conn_get_from_addr(addr);
        if (conn != NULL)
            printf("test_connect done\n");
        else
            printf("connect_envoy returned NULL to test_connect\n");
    }

    return NULL;
}
*/

void config_init(void) {
    char *in = getenv("ENVOY_STORAGE_SERVERS");
    char *ptr;
    List *servers = NULL;
    int i;

    storage_server_count = 0;
    storage_servers = NULL;

    if (in == NULL) {
        fprintf(stderr, "ENVOY_STORAGE_SERVERS must be defined\n");
        exit(-1);
    }
    printf("ENVOY_STORAGE_SERVERS: [%s]\n", in);

    while (in != NULL && *in != 0) {
        char *machine;
        int port = 0;

        /* extract a single address from the list */
        if ((ptr = strchr(in, ',')) != NULL) {
            machine = substring(in, 0, ptr - in);
            in = ptr + 1;
        } else {
            machine = in;
            in = NULL;
        }

        /* now convert it into an Address */
        if ((ptr = strchr(machine, ':')) != NULL) {
            port = atoi(ptr + 1);
            if (port < 1)
                port = STORAGE_PORT;
            *ptr = 0;
        } else {
            port = STORAGE_PORT;
        }
        servers = cons(make_address(machine, port), servers);
    }

    /* put the servers in the original order */
    servers = reverse(servers);

    storage_server_count = length(servers);
    storage_servers = GC_MALLOC(sizeof(Connection *) * storage_server_count);
    assert(storage_servers != NULL);

    for (i = 0; i < storage_server_count; i++) {
        Address *addr = car(servers);
        Connection *conn;

        printf("storage server %d: ", i);
        print_address(addr);
        printf("\n");

        conn = conn_connect_to_storage(addr);
        if (conn == NULL) {
            printf("Failed to connect to storage server %d\n", i);
            assert(0);
        }

        storage_servers[i] = conn;
        servers = cdr(servers);
    }

    assert(null(servers));
}

void test_oid(void) {
    printf("first available: %llx\n", oid_find_next_available());
}

int main(int argc, char **argv) {
    char cwd[100];
    struct stat info;
    char *name;
    int isstorage = 0;

    assert(sizeof(off_t) == 8);
    assert(sizeof(u8) == 1);
    assert(sizeof(u16) == 2);
    assert(sizeof(u32) == 4);
    assert(sizeof(u64) == 8);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <root path>\n", argv[0]);
        return -1;
    }

    objectroot = "/local/scratch/rgr22/root";
    assert(getcwd(cwd, 100) == cwd);
    rootdir = resolvePath(resolvePath("/", cwd, &info), argv[1], &info);
    if (rootdir == NULL) {
        fprintf(stderr, "Invalid path\n");
        return -1;
    }

    /* were we called as envoy or storage? */
    name = strstr(argv[0], "storage");
    if (name != NULL && !strcmp(name, "storage")) {
        printf("starting storage server\n");
        state_init_storage();
        isstorage = 1;
    } else {
        printf("starting envoy server\n");
        state_init_envoy();
        isstorage = 0;
    }
    transport_init();
    if (!isstorage)
        config_init();

    /*test_dump();*/
    /*worker_create(test_connect, NULL);*/
    printf("root directory = [%s]\n", rootdir);

    test_oid();
    main_loop();
    return 0;
}
