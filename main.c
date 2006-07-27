#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "9p.h"
#include "connection.h"
#include "fid.h"
#include "util.h"
#include "config.h"
#include "transport.h"
#include "worker.h"
#include "oid.h"
#include "claim.h"
#include "lease.h"
#include "walk.h"

void test_dump(void) {
    struct leaserecord *lease;
    struct leaserecord **leaselist;
    struct fidrecord *fid;
    struct fidrecord **fidlist;
    Message *m = message_new();
    Message *m2 = message_new();
    m->id = TEGRANT;
    m->tag = 1;

    lease = GC_NEW(struct leaserecord);
    assert(lease != NULL);

    lease->pathname = "/home/on/the/range";
    lease->readonly = 0;
    lease->oid = 1234567890;
    lease->address = 127 * 256 * 256 * 256 + 1;
    lease->port = 9922;

    leaselist = GC_MALLOC(sizeof(struct leaserecord *) * 5);
    assert(leaselist != NULL);
    leaselist[0] = lease;
    leaselist[1] = lease;
    leaselist[2] = lease;
    leaselist[3] = lease;
    leaselist[4] = lease;

    fid = GC_NEW(struct fidrecord);
    assert(fid != NULL);
    fid->fid = 5;
    fid->pathname = "/home/a/me/give/oh";
    fid->user = "russ";
    fid->status = 2;
    fid->omode = 0644;
    fid->readdir_cookie = 9876543210LL;
    fid->address = 192LL * 256 * 256 * 256 + 168 * 256 * 256 + 1;
    fid->port = 9924;

    fidlist = GC_MALLOC(sizeof(struct fidrecord *) * 4);
    fidlist[0] = fid;
    fidlist[1] = fid;
    fidlist[2] = fid;
    fidlist[3] = fid;

    set_tegrant(m, 1, lease, 0x7f000001, 9922, 5, leaselist, 4, fidlist);
    printMessage(stdout, m);
    m->raw = GC_MALLOC(0x10000);
    assert(packMessage(m, 0x10000) == 0);
    m2->raw = m->raw;
    m2->size = m->size;
    assert(unpackMessage(m2) == 0);
    printMessage(stdout, m2);
}

void test_oid(void) {
    printf("first available: %llx\n", oid_find_next_available());
}

int main(int argc, char **argv) {
    char *name;

    assert(sizeof(off_t) == 8);
    assert(sizeof(u8) == 1);
    assert(sizeof(u16) == 2);
    assert(sizeof(u32) == 4);
    assert(sizeof(u64) == 8);

    GC_use_entire_heap = 1;
    GC_init();
    GC_expand_hp(1024 * 8192);

    /* were we called as envoy or storage? */
    name = strstr(argv[0], "storage");

    if (name != NULL && !strcmp(name, "storage")) {
        isstorage = 1;
        if (config_storage(argc, argv) < 0)
            return -1;
        if (DEBUG_VERBOSE) {
            printf("starting storage server with root path:\n    [%s]\n",
                    objectroot);
        }

        my_address = get_my_address();
        worker_state_init();
        conn_init();
        oid_state_init();
        fid_state_init();
        util_state_init();
        transport_init();
    } else {
        isstorage = 0;
        if (config_envoy(argc, argv) < 0)
            return -1;
        if (DEBUG_VERBOSE) {
            if (root_address == NULL) {
                printf("starting envoy server: root oid = [%llu]\n", root_oid);
            } else {
                printf("starting envoy server: root server = [%s]\n",
                        addr_to_string(root_address));
            }
        }

        my_address = get_my_address();
        worker_state_init();
        conn_init();
        lease_state_init();
        claim_state_init();
        walk_state_init();
        fid_state_init();
        util_state_init();
        transport_init();
        if (root_address == NULL) {
            Claim *claim = claim_new_root("/", ACCESS_WRITEABLE, root_oid);
            Lease *lease = lease_new("/", NULL, 0, claim, 0);
            claim_add_to_cache(claim);
            lease_add(lease);
        }
    }

    main_loop();
    return 0;
}
