#include <assert.h>
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

    GC_init();

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
        walk_state_init();
        fid_state_init();
        util_state_init();
        transport_init();
        if (root_address == NULL) {
            Claim *claim = claim_new_root("/", ACCESS_WRITEABLE, root_oid);
            Lease *lease = lease_new("/", NULL, 0, claim, NULL, 0);
            lease_add(lease);
        }
    }

    main_loop();
    return 0;
}
