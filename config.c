#include <assert.h>
#include <gc/gc.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <getopt.h>
#include "types.h"
#include "9p.h"
#include "list.h"
#include "connection.h"
#include "util.h"
#include "config.h"

/* values that are configured at startup time */

int GLOBAL_MAX_SIZE = 1024 * 32 + TSWRITE_DATA_OFFSET + STORAGE_SLUSH + 1;
int PORT;
int isstorage;
char *objectroot;
Address *my_address;
Address *root_address;
int storage_server_count;
Connection **storage_servers;
Address **storage_addresses;
int ter_disabled = 0;
double ter_halflife = 5.0;
double ter_urgent = 100.0;
double ter_idle = 5.0;
double ter_maxtime = 120.0;
double ter_mintime = 5.0;
double ter_rate = 0.0;
int DEBUG = 0;
int DEBUG_VERBOSE = 0;
int DEBUG_AUDIT = 0;
int DEBUG_STORAGE = 0;
int DEBUG_CLIENT = 0;
int DEBUG_ENVOY = 0;
int DEBUG_ENVOY_ADMIN = 0;
int DEBUG_PAYLOAD = 0;
int DEBUG_TRANSFER = 0;
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
"                                 storage servers (default localhost:%d)\n"
"    -c, --cache=<PATH>         path to the root of the object cache\n"
"    = = = = = = = = dynamic territory management = = = = = = = =\n"
"    -a, --noauto               disable automatic territory management\n"
"    -l, --halflife=<SECONDS>   half-life of ops-per-second (OPS) value\n"
"                                 (default %g)\n"
"    -t, --mintime=<SECONDS>    shortest time to wait before a change\n"
"                                 (default %g)\n"
"    -T, --maxtime=<SECONDS>    longest time to wait before a change\n"
"                                 (default %g)\n"
"    -u, --idle=<OPS>           lowest rate to trigger a change (at maxtime)\n"
"                                 (default %g)\n"
"    -U, --urgent=<OPS>         rate to trigger a change fastest (at mintime)\n"
"                                 (default %g)\n",
            STORAGE_PORT,
            ter_halflife, ter_mintime, ter_maxtime, ter_idle, ter_urgent);
    }
    fprintf(stderr,
"    -i, --ip=<IPADDRESS>       override the ip address of this host\n"
"                                 (default %s)\n"
"    -p, --port=<PORT>          listen on the given port (default %d)\n"
"    -m, --messagesize=<SIZE>   maximum message size (default %d)\n"
"    -d, --debug=<FLAGS>        debug options:\n"
"                                 v: verbose debug output\n"
"                                 d: data structure audits\n"
"                                 s: message to/from storage servers\n",
            addr_to_dotted(my_address),
            (isstorage ? STORAGE_PORT : ENVOY_PORT), GLOBAL_MAX_SIZE);
    if (!isstorage) {
        fprintf(stderr,
"                                 c: messages to/from clients\n"
"                                 e: messages to/from other envoys\n"
"                                 a:   envoy admin messages only\n"
"                                 t: territory transfer decisions\n");
    }
    fprintf(stderr,
"                                 p: include data payloads for read/write\n");
}

int config_envoy(int argc, char **argv) {
    int finished = 0;
    static struct option long_options[] = {
        { "help",       no_argument,            NULL,   'h' },
        { "root",       required_argument,      NULL,   'r' },
        { "storage",    required_argument,      NULL,   's' },
        { "cache",      required_argument,      NULL,   'c' },
        { "noauto",     no_argument,            NULL,   'a' },
        { "halflife",   required_argument,      NULL,   'l' },
        { "mintime",    required_argument,      NULL,   't' },
        { "maxtime",    required_argument,      NULL,   'T' },
        { "idle",       required_argument,      NULL,   'u' },
        { "urgent",     required_argument,      NULL,   'U' },
        { "ip",         required_argument,      NULL,   'i' },
        { "port",       required_argument,      NULL,   'p' },
        { "debug",      required_argument,      NULL,   'd' },
        { "messagesize", required_argument,     NULL,   'm' },
        { 0,            0,                      0,      0   }
    };

    /* fill in the defaults */
    root_oid = 0LL;
    root_address = NULL;
    storage_server_count = 1;
    storage_addresses = GC_MALLOC(sizeof(Address *));
    assert(storage_addresses != NULL);
    storage_addresses[0] = make_address("localhost", STORAGE_PORT);
    storage_servers = NULL;
    objectroot = NULL;
    PORT = ENVOY_PORT;
    my_address = get_my_address();
    DEBUG_VERBOSE =
        DEBUG_AUDIT =
        DEBUG_STORAGE =
        DEBUG_CLIENT =
        DEBUG_ENVOY = DEBUG_ENVOY_ADMIN =
        DEBUG_PAYLOAD =
        DEBUG_TRANSFER = 0;

    while (!finished) {
        u64 rootobj;
        char *end;
        List *addrs;
        char cwd[100];
        struct stat info;
        int i;
        double d;

        switch (getopt_long(argc, argv, "hr:s:c:al:t:T:u:U:i:p:d:m:",
                    long_options, NULL))
        {
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
            case 'c':
                /* get the cache directory */
                assert(getcwd(cwd, 100) == cwd);
                objectroot = resolvePath(resolvePath("/", cwd, &info), optarg,
                        &info);
                if (objectroot == NULL) {
                    fprintf(stderr, "Invalid cache path: %s\n", optarg);
                    return -1;
                }
                break;
            case 'a':
                ter_disabled = 1;
                break;
            case 'l':
                d = strtod(optarg, &end);
                if (*end == 0 && d >= 1.0 && d <= 3600.0) {
                    ter_halflife = d;
                } else {
                    fprintf(stderr, "Invalid halflife: %s\n", optarg);
                    return -1;
                }
                break;
            case 't':
                d = strtod(optarg, &end);
                if (*end == 0 && d >= 0.0 && d <= 3600.0) {
                    ter_mintime = d;
                } else {
                    fprintf(stderr, "Invalid mintime: %s\n", optarg);
                    return -1;
                }
                break;
            case 'T':
                d = strtod(optarg, &end);
                if (*end == 0 && d >= 1.0 && d <= 3600.0) {
                    ter_maxtime = d;
                } else {
                    fprintf(stderr, "Invalid maxtime: %s\n", optarg);
                    return -1;
                }
                break;
            case 'u':
                d = strtod(optarg, &end);
                if (*end == 0 && d >= 0.0 && d < 10000.0) {
                    ter_idle = d;
                } else {
                    fprintf(stderr, "Invalid idle value: %s\n", optarg);
                    return -1;
                }
                break;
            case 'U':
                d = strtod(optarg, &end);
                if (*end == 0 && d >= 1.0 && d < 10000.0) {
                    ter_urgent = d;
                } else {
                    fprintf(stderr, "Invalid max urgency value: %s\n", optarg);
                    return -1;
                }
                break;
            case 'i':
                my_address = make_address(optarg, PORT);
                if (my_address == NULL) {
                    fprintf(stderr, "Invalid IP address: %s\n", optarg);
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
                    my_address->port = PORT = i;
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
                DEBUG = 1;
                for (i = 0; optarg[i]; i++) {
                    switch (optarg[i]) {
                        case 'c':       DEBUG_CLIENT = 1;               break;
                        case 's':       DEBUG_STORAGE = 1;              break;
                        case 'e':       DEBUG_ENVOY = 1;                /* ft */
                        case 'a':       DEBUG_ENVOY_ADMIN = 1;          break;
                        case 'v':       DEBUG_VERBOSE = 1;              break;
                        case 'd':       DEBUG_AUDIT = 1;                break;
                        case 'p':       DEBUG_PAYLOAD = 1;              break;
                        case 't':       DEBUG_TRANSFER = 1;             break;
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
    if (!ter_disabled) {
        if (ter_urgent <= ter_idle) {
            fprintf(stderr, "Max urgency value must be greater than "
                    "idle value\n");
            return -1;
        }
        if (ter_maxtime <= ter_mintime) {
            fprintf(stderr, "Max delay value must be greater than "
                    "min delay value\n");
            return -1;
        }
        ter_rate = (ter_maxtime - ter_mintime) / (ter_urgent - ter_idle);
    }
    /*
    if (objectroot == NULL) {
        char *home = getenv("HOME");
        struct stat info;
        if (home != NULL) {
            objectroot = resolvePath(resolvePath("/", home, &info),
                    "cache", &info);
        }
        if (objectroot == NULL) {
            fprintf(stderr, "No root directory for cache specified\n");
            return -1;
        }
    }
    */

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
    my_address = get_my_address();
    DEBUG_VERBOSE =
        DEBUG_STORAGE =
        DEBUG_PAYLOAD = 0;

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
            case 'i':
                my_address = make_address(optarg, PORT);
                if (my_address == NULL) {
                    fprintf(stderr, "Invalid IP address: %s\n", optarg);
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
                    my_address->port = PORT = i;
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
                DEBUG = 1;
                for (i = 0; optarg[i]; i++) {
                    switch (optarg[i]) {
                        case 's':       DEBUG_STORAGE = 1;              break;
                        case 'v':       DEBUG_VERBOSE = 1;              break;
                        case 'p':       DEBUG_PAYLOAD = 1;              break;
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
        char *home = getenv("HOME");
        struct stat info;
        if (home != NULL) {
            objectroot = resolvePath(resolvePath("/", home, &info),
                    "root", &info);
        }
        if (objectroot == NULL) {
            fprintf(stderr, "No root directory for object storage specified\n");
            return -1;
        }
    }

    return 0;
}
