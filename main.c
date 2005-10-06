#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <assert.h>
#include <gc/gc.h>
#include "9p.h"
#include "config.h"
#include "state.h"
#include "list.h"
#include "util.h"
#include "transport.h"
#include "dispatch.h"
#include "map.h"

void test_dump(void) {
    struct message m;
    char *path[] = { "local", "scratch", "rgr22", "1234567" };
    u8 *data;
    int size;
    struct p9stat *stat;
    
    m.raw = GC_MALLOC_ATOMIC(m.maxSize = 32768);

    m.id = TVERSION;
    m.tag = 1;
    m.msg.tversion.msize = 8192;
    m.msg.tversion.version = "9P2000.u";

    printMessage(stdout, &m);
    packMessage(&m);
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

    packMessage(&m);
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

    packMessage(&m);
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

    packMessage(&m);
    printf("packed size = %d\n", m.size);
    dumpBytes(stdout, "    ", m.raw, m.size);
}

void test_map(void) {
    struct map *root = GC_NEW(struct map);
    struct cons *a, *b;
    struct sockaddr_in *addr = GC_NEW_ATOMIC(struct sockaddr_in);
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

void *test_connect(void *arg) {
    worker_start();

    struct connection *conn = conn_new_unopened(CONN_ENVOY_OUT,
            make_address("boulderdash", PORT));
    if (!memcmp(&conn->addr->sin_addr, &state->my_address->sin_addr,
                sizeof(conn->addr->sin_addr)))
    {
        printf("I am boulderdash\n");
    } else {
        struct transaction *trans = connect_envoy(conn);
        if (trans != NULL)
            printf("test_connect done\n");
        else
            printf("connect_envoy returned NULL to test_connect\n");
    }

    worker_finish();
    return NULL;
}

void config_init(void) {
    map_insert(state->map, cons("home", cons("rgr22", NULL)),
            make_address("boulderdash", PORT));
    map_insert(state->map, cons("usr", cons("lib", NULL)),
            make_address("boulderdash", PORT));
    map_insert(state->map, cons("lib", NULL),
            make_address("donkeykong", PORT));
    map_insert(state->map, cons("usr", cons("bin", NULL)),
            make_address("donkeykong", PORT));
}

int main(int argc, char **argv) {
    char cwd[100];
    struct stat info;

    assert(sizeof(off_t) == 8);
    assert(sizeof(u8) == 1);
    assert(sizeof(u16) == 2);
    assert(sizeof(u32) == 4);
    assert(sizeof(u64) == 8);

    if (argc != 2) {
        fprintf(stderr, "Usage: %s <root path>\n", argv[0]);
        return -1;
    }
    
    assert(getcwd(cwd, 100) == cwd);
    rootdir = resolvePath(resolvePath("/", cwd, &info), argv[1], &info);
    if (rootdir == NULL) {
        fprintf(stderr, "Invalid path\n");
        return -1;
    }

    state_init();
    /* acquire the big lock */
    worker_start();
    transport_init();
    config_init();

    /*test_dump();*/
    /*test_map();*/
    worker_create(test_connect, NULL);
    printf("root directory = [%s]\n", rootdir);

    main_loop();
    return 0;
}
