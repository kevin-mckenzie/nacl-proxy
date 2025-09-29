#include <getopt.h> // NOLINT (misc-include-cleaner)
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>

#include "proxy.h"
#include "utils.h"

static void print_usage(const char *prog_name);
/** Print usage information for the proxy command-line interface. */

static int parse_args(config_t *p_config, int argc, char *argv[]);
/** Parse command-line arguments and fill config_t. Returns 0 on success, -1 on error. */

/**
 * @brief Entry point for the proxy application.
 *
 * Parses command-line arguments, runs the proxy main loop, and returns exit status.
 */
int main(int argc, char *argv[]) {
    config_t config = {0};
    if (0 != parse_args(&config, argc, argv)) {
        // Invalid arguments or help requested
        return EXIT_FAILURE;
    }

    if (0 != proxy_run(&config)) {
        // Proxy failed to start or encountered a fatal error
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

static void print_usage(const char *prog_name) {
    // Print usage and option help for users.
    printf("Usage: %s [-io] <bind address> <bind port> <server address> <server port>\n", prog_name);
    printf("  -i : encrypt incoming client connections\n");
    printf("  -o : encrypt outgoing server connections\n");
    printf("  -io : encrypt both incoming and outgoing connections\n");
}

/**
 * @brief Parse command-line arguments and fill config_t.
 *
 * Handles encryption flags and validates positional arguments.
 * Returns 0 on success, -1 on error or if help is requested.
 */
static int parse_args(config_t *p_config, int argc, char *argv[]) {
    ASSERT_RET(NULL != p_config); // NOLINT (misc-include-cleaner)
    ASSERT_RET(NULL != argv);

    int opt = 0;
    // getopt parses flags; -i and -o enable encryption for client/server.
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

    // Require exactly four positional arguments after options.
    if ((optind + 4) != argc) { // NOLINT (misc-include-cleaner)
        print_usage(argv[0]);
        return -1;
    }

    // Assign addresses and ports from argv.
    p_config->bind_addr = argv[optind];
    p_config->bind_port = argv[optind + 1];
    p_config->server_addr = argv[optind + 2];
    p_config->server_port = argv[optind + 3];

    return 0;
}
