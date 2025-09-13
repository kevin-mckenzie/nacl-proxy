/**
 * @file network.h
 * @author Kevin McKenzie
 * @brief The `network` module defines networking related structs as defined in
 * the project requirements for requests and responses. It also provides helper
 * functions for sending and receiving data with a client, and provides
 * functions that return whether a request is valid based on structure of the
 * request and lengths of the request buffers.
 *
 */
#ifndef NETWORK_H
#define NETWORK_H

#include <stdbool.h>

#include "netnacl.h"
#include "utils.h"

typedef struct {
    int sock_fd;
    bool b_encrypted;
    netnacl_t *netnacl;
} net_t;

int network_set_sock_nonblocking(int sock_fd);

int network_connect_to_server(const char *addr, const char *port);

int network_get_listen_socket(const char *addr, const char *port);

#endif /* NETWORK_H */

/*** END OF FILE ***/
