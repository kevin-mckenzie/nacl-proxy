#include <getopt.h> // NOLINT (misc-include-cleaner)
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "proxy.h"
#include "utils.h"

static void print_usage(const char *prog_name);
static int parse_args(config_t *p_config, int argc, char *argv[]);

int main(int argc, char *argv[]) {
    config_t config = {0};
    if (0 != parse_args(&config, argc, argv)) {
        return EXIT_FAILURE;
    }

    if (0 != proxy_run(&config)) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void print_usage(const char *prog_name) {
    printf("Usage: %s [-io] <bind address> <bind port> <server address> <server port>\n", prog_name);
    printf("  -i : encrypt incoming client connections\n");
    printf("  -o : encrypt outgoing server connections\n");
    printf("  -io : encrypt both incoming and outgoing connections\n");
}

static int parse_args(config_t *p_config, int argc, char *argv[]) {
    ASSERT_RET(NULL != p_config); // NOLINT (misc-include-cleaner)
    ASSERT_RET(NULL != argv);

    int opt = 0;
    while ((opt = getopt(argc, argv, "ioh")) != -1) // NOLINT (concurrency-mt-unsafe)
    {
        switch (opt) {
        case 'i':
            p_config->b_encrypt_in = true;
            break;
        case 'o':
            p_config->b_encrypt_out = true;
            break;
        case 'h':
        default:
            print_usage(argv[0]);
            return -1;
        }
    }

    if ((optind + 4) != argc) { // NOLINT (misc-include-cleaner)
        print_usage(argv[0]);
        return -1;
    }

    p_config->bind_addr = argv[optind];
    p_config->bind_port = argv[optind + 1];
    p_config->server_addr = argv[optind + 2];
    p_config->server_port = argv[optind + 3];

    return 0;
}
