#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <getopt.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "connection.h"
#include "util.h"
#include "config.h"

/* values that are configured at startup time */

int GLOBAL_MAX_SIZE = 32799;
int PORT;
int isstorage;
char *objectroot;
Address *my_address;
Address *root_address;
int storage_server_count;
Connection **storage_servers;
Address **storage_addresses;
int DEBUG_VERBOSE = 0;
int DEBUG_AUDIT = 0;
int DEBUG_STORAGE = 0;
int DEBUG_CLIENT = 0;
int DEBUG_ENVOY = 0;
int DEBUG_ENVOY_ADMIN = 0;
u64 root_oid;

void print_usage(void) {
    fprintf(stderr,
"Usage: %s <opt>\n"
"    -h, --help                 print this help message\n",
            (isstorage ? "storage" : "envoy"));
    if (isstorage) {
        fprintf(stderr,
"    -r, --root=<PATH>          path to the root of the object store\n");
    } else {
        fprintf(stderr,
"    -r, --root=<ROOT>          connect to the root envoy instance\n"
"                                 specified, or start a root instance and\n"
"                                 use the given object ID as the root object\n"
"                                 (default 0)\n"
"    -s, --storage=<SERVERS>    connect to the comma-seperated list of\n"
"                                 storage servers (default localhost:%d)\n",
            STORAGE_PORT);
    }
    fprintf(stderr,
"    -p, --port=<PORT>          listed on the given port (default %d)\n"
"    -m, --messagesize=<SIZE>   maximum message size (default %d)\n"
"    -d, --debug=<FLAGS>        debug options:\n"
"                                 v: verbose debug output\n"
"                                 d: data structure audits\n"
"                                 s: message to/from storage servers\n",
            (isstorage ? STORAGE_PORT : ENVOY_PORT), GLOBAL_MAX_SIZE);
    if (!isstorage) {
        fprintf(stderr,
"                                 c: messages to/from clients\n"
"                                 e: messages to/from other envoys\n"
"                                 a:   envoy admin messages only\n");
    }
}

int config_envoy(int argc, char **argv) {
    int finished = 0;
    static struct option long_options[] = {
        { "help",       no_argument,            NULL,   'h' },
        { "root",       required_argument,      NULL,   'r' },
        { "storage",    required_argument,      NULL,   's' },
        { "port",       required_argument,      NULL,   'p' },
        { "debug",      required_argument,      NULL,   'd' },
        { "messagesize", required_argument,     NULL,   'm' },
        { 0,            0,                      0,      0   }
    };

    /* fill in the defaults */
    root_oid = 0LL;
    root_address = NULL;
    storage_server_count = 0;
    storage_servers = NULL;
    storage_addresses = NULL;
    PORT = ENVOY_PORT;
    DEBUG_VERBOSE =
        DEBUG_AUDIT =
        DEBUG_STORAGE =
        DEBUG_CLIENT =
        DEBUG_ENVOY = DEBUG_ENVOY_ADMIN = 0;

    if (argc < 2) {
        print_usage();
        return -1;
    }

    while (!finished) {
        u64 rootobj;
        char *end;
        List *addrs;
        int i;

        switch (getopt_long(argc, argv, "hr:s:p:d:m:", long_options, NULL)) {
            case EOF:
                finished = 1;
                break;
            case 'r':
                rootobj = strtoull(optarg, &end, 10);
                if (*end == 0) {
                    root_oid = rootobj;
                    root_address = NULL;
                } else {
                    root_oid = NOOID;
                    root_address = parse_address(optarg, ENVOY_PORT);
                }
                if (root_oid == NOOID && root_address == NULL) {
                    fprintf(stderr, "Bad root address/object id: %s\n", optarg);
                    return -1;
                }
                break;
            case 's':
                addrs = parse_address_list(optarg, STORAGE_PORT);
                if (null(addrs)) {
                    fprintf(stderr, "Bad list of storage addresses: %s\n",
                            optarg);
                    return -1;
                }
                storage_server_count = length(addrs);
                storage_addresses =
                    GC_MALLOC(sizeof(Address *) * storage_server_count);
                assert(storage_addresses != NULL);
                i = 0;
                while (!null(addrs)) {
                    storage_addresses[i++] = car(addrs);
                    addrs = cdr(addrs);
                }
                break;
            case 'p':
                i = strtol(optarg, &end, 10);
                if (*end == 0 && i > 0 && i < 0x10000) {
                    if (i < 1024 && geteuid() != 0) {
                        fprintf(stderr, "Only root can use ports < 1024\n");
                        return -1;
                    }
                    PORT = i;
                } else {
                    fprintf(stderr, "Invalid port: %s\n", optarg);
                    return -1;
                }
                break;
            case 'm':
                i = strtol(optarg, &end, 10);
                if (*end == 0 && i >= GLOBAL_MIN_SIZE && i < 0x10000) {
                    GLOBAL_MAX_SIZE = i;
                } else {
                    fprintf(stderr, "Invalid max message size: %s\n", optarg);
                    return -1;
                }
                break;
            case 'd':
                for (i = 0; optarg[i]; i++) {
                    switch (optarg[i]) {
                        case 'c':       DEBUG_CLIENT = 1;               break;
                        case 's':       DEBUG_STORAGE = 1;              break;
                        case 'e':       DEBUG_ENVOY = 1;                /* ft */
                        case 'a':       DEBUG_ENVOY_ADMIN = 1;          break;
                        case 'v':       DEBUG_VERBOSE = 1;              break;
                        case 'd':       DEBUG_AUDIT = 1;                break;
                        default:
                            fprintf(stderr, "Unknown debug option: %c\n",
                                    optarg[i]);
                            return -1;
                    }
                }
                break;
            case 'h':
            default:
                print_usage();
                return -1;
        }
    }

    if (root_oid == NOOID && root_address == NULL) {
        fprintf(stderr, "The root object id/server must be specified.\n");
        return -1;
    }
    if (storage_server_count == 0) {
        fprintf(stderr, "No storage servers specified\n");
        return -1;
    }

    return 0;
}

int config_storage(int argc, char **argv) {
    int finished = 0;
    static struct option long_options[] = {
        { "help",       no_argument,            NULL,   'h' },
        { "root",       required_argument,      NULL,   'r' },
        { "port",       required_argument,      NULL,   'p' },
        { "debug",      required_argument,      NULL,   'd' },
        { "messagesize", required_argument,     NULL,   'm' },
        { NULL,         0,                      NULL,   0   }
    };

    /* fill in the defaults */
    objectroot = NULL;
    PORT = STORAGE_PORT;
    DEBUG_VERBOSE =
        DEBUG_STORAGE = 0;

    if (argc < 2) {
        print_usage();
        return -1;
    }

    while (!finished) {
        int i;
        char *end;
        char cwd[100];
        struct stat info;

        switch (getopt_long(argc, argv, "hr:p:d:m:", long_options, NULL)) {
            case EOF:
                finished = 1;
                break;
            case 'r':
                /* get the root directory */
                assert(getcwd(cwd, 100) == cwd);
                objectroot = resolvePath(resolvePath("/", cwd, &info), optarg,
                        &info);
                if (objectroot == NULL) {
                    fprintf(stderr, "Invalid root path: %s\n", optarg);
                    return -1;
                }
                break;
            case 'p':
                i = strtol(optarg, &end, 10);
                if (*end == 0 && i > 0 && i < 0x10000) {
                    if (i < 1024 && geteuid() != 0) {
                        fprintf(stderr, "Only root can use ports < 1024\n");
                        return -1;
                    }
                    PORT = i;
                } else {
                    fprintf(stderr, "Invalid port: %s\n", optarg);
                    return -1;
                }
                break;
            case 'm':
                i = strtol(optarg, &end, 10);
                if (*end == 0 && i >= GLOBAL_MIN_SIZE && i < 0x10000) {
                    GLOBAL_MAX_SIZE = i;
                } else {
                    fprintf(stderr, "Invalid max message size: %s\n", optarg);
                    return -1;
                }
                break;
            case 'd':
                for (i = 0; optarg[i]; i++) {
                    switch (optarg[i]) {
                        case 's':       DEBUG_STORAGE = 1;              break;
                        case 'v':       DEBUG_VERBOSE = 1;              break;
                        default:
                            fprintf(stderr, "Unknown debug option: %c\n",
                                    optarg[i]);
                            return -1;
                    }
                }
                break;
            case 'h':
            default:
                print_usage();
                return -1;
        }
    }

    if (objectroot == NULL) {
        fprintf(stderr, "No root directory for object storage specified\n");
        return -1;
    }

    return 0;
}
