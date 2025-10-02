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

/**
 * @brief Free cached server address info.
 *
 * Call this function at program exit to avoid memory leaks.
 */
void network_free_cached_address(void);

/**
 * @brief Sets a socket to non-blocking mode.
 *
 * Uses fcntl() to set the O_NONBLOCK flag on the given socket file descriptor.
 *
 * @param sock_fd Socket file descriptor.
 * @return PROXY_SUCCESS on success, PROXY_ERR on failure.
 */
int network_set_sock_nonblocking(int sock_fd);

/**
 * @brief Connects to a remote server using the specified address and port.
 *
 * Resolves the address and port using getaddrinfo(), attempts to connect using a non-blocking socket.
 * Returns the connected socket file descriptor, or -1 on failure.
 *
 * @param addr Server address (IP or hostname).
 * @param port_str Server port as a string.
 * @return Connected socket file descriptor on success, -1 on failure.
 */
int network_connect_to_server(const char *addr, const char *port_str);

/**
 * @brief Creates a listening socket bound to the specified address and port.
 *
 * Handles both IPv4 and IPv6 addresses, sets socket options, binds, and listens.
 * Returns the listening socket file descriptor, or -1 on failure.
 *
 * @param addr_str Address to bind (IP string).
 * @param port_str Port to bind as a string.
 * @return Listening socket file descriptor on success, -1 on failure.
 */
int network_get_listen_socket(const char *addr_str, const char *port_str);

#endif /* NETWORK_H */

/*** END OF FILE ***/
