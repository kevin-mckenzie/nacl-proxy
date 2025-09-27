#include <arpa/inet.h>
#include <assert.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "errors.h"
#include "log.h"
#include "network.h"
#include "utils.h"

enum {
    MAX_QUEUED_CONNECTIONS = 128,
    MIN_PORT = 1024,
    MAX_PORT = 65535
};

static int get_ipv4_listener(const char *addr, uint16_t port);
static int get_ipv6_listener(const char *addr, uint16_t port);

int network_set_sock_nonblocking(int sock_fd) {
    ASSERT_RET(-1 < sock_fd);

    int flags = fcntl(sock_fd, F_GETFL, 0);
    if (-1 == flags) {
        LOG(ERR, "fcntl F_GETFL");
        return PROXY_ERR;
    }

    flags |= O_NONBLOCK; // NOLINT (hicpp-signed-bitwise)
    if (-1 == fcntl(sock_fd, F_SETFL, flags)) {
        LOG(ERR, "fcntl F_SETFL");
        return PROXY_ERR;
    }

    return PROXY_SUCCESS;
}

int network_connect_to_server(const char *addr, const char *port_str) {
    ASSERT_RET(NULL != addr); // NOLINT (misc-include-cleaner)
    ASSERT_RET(NULL != port_str);

    int sock_fd = -1;
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV;

    struct addrinfo *p_gai_result = NULL;
    int gai_error_code = getaddrinfo(addr, port_str, &hints, &p_gai_result);
    if (0 != gai_error_code) {
        LOG(WRN, "getaddrinfo: %s", gai_strerror(gai_error_code));
        return -1;
    }

    for (struct addrinfo *p_curr = p_gai_result; p_curr != NULL; p_curr = p_curr->ai_next) {
        sock_fd = socket(p_curr->ai_family, p_curr->ai_socktype | SOCK_NONBLOCK, p_curr->ai_protocol);
        if (-1 == sock_fd) {
            LOG(ERR, "socket");
            continue;
        }

        if (0 != connect(sock_fd, p_curr->ai_addr, p_curr->ai_addrlen)) {
            if (EINPROGRESS == errno) {
                break;
            }
            LOG(ERR, "connect");
            (void)close(sock_fd);
            sock_fd = -1;
            continue;
        }

        break;
    }

    freeaddrinfo(p_gai_result);

    return sock_fd;
}

int network_get_listen_socket(const char *addr_str, const char *port_str) {
    ASSERT_RET(NULL != addr_str);
    ASSERT_RET(NULL != port_str);

    int server_fd = -1;

    uint16_t port = 0;
    unsigned long long_port = strtoul(port_str, NULL, DECIMAL);
    if ((0 == long_port) || (MAX_PORT < long_port)) {
        LOG(WRN, "invalid bind port %s", port_str);
        return -1;
    }

    port = (uint16_t)long_port;

    bool b_ipv6 = false;
    if (NULL != strstr(addr_str, ":")) { // good enough for government work
        b_ipv6 = true;
    }

    if (b_ipv6) {
        server_fd = get_ipv6_listener(addr_str, port);
    } else {
        server_fd = get_ipv4_listener(addr_str, port);
    }

    return server_fd;
}

static int get_ipv4_listener(const char *addr_str, uint16_t port) {
    ASSERT_RET(NULL != addr_str);
    ASSERT_RET(0 != port);

    int server_fd = -1;
    int yes_to_reuse = 1;

    struct sockaddr_in serv_addr = {0};
    memset(&serv_addr, 0, sizeof(serv_addr));

    if (0 == inet_pton(AF_INET, addr_str, &serv_addr.sin_addr)) {
        LOG(ERR, "inet_pton");
        goto CLEANUP;
    }

    server_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (-1 == server_fd) {
        LOG(ERR, "socket");
        goto CLEANUP;
    }

    if (-1 == setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes_to_reuse, sizeof(int))) {
        LOG(ERR, "setsockopt");
        goto CLEANUP;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    if (-1 == bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) {
        LOG(ERR, "bind");
        goto CLEANUP;
    }

    if (-1 == listen(server_fd, MAX_QUEUED_CONNECTIONS)) {
        LOG(ERR, "listen");
        goto CLEANUP;
    }

    return server_fd;

CLEANUP:
    if (-1 != server_fd) {
        (void)close(server_fd);
    }
    return -1;
}

static int get_ipv6_listener(const char *addr_str, uint16_t port) {
    ASSERT_RET(NULL != addr_str);
    ASSERT_RET(0 != port);

    int server_fd = -1;
    int yes_to_reuse = 1;

    struct sockaddr_in6 serv_addr = {0};
    memset(&serv_addr, 0, sizeof(serv_addr));

    if (0 == inet_pton(AF_INET6, addr_str, &serv_addr.sin6_addr)) {
        LOG(ERR, "inet_pton");
        goto CLEANUP;
    }

    server_fd = socket(AF_INET6, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (-1 == server_fd) {
        LOG(ERR, "socket");
        goto CLEANUP;
    }

    if (-1 == setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes_to_reuse, sizeof(int))) {
        LOG(ERR, "setsockopt");
        goto CLEANUP;
    }

    serv_addr.sin6_family = AF_INET6;
    serv_addr.sin6_port = htons(port);
    if (-1 == bind(server_fd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))) {
        LOG(ERR, "bind");
        goto CLEANUP;
    }

    if (-1 == listen(server_fd, MAX_QUEUED_CONNECTIONS)) {
        LOG(ERR, "listen");
        goto CLEANUP;
    }

    return server_fd;

CLEANUP:
    if (-1 != server_fd) {
        (void)close(server_fd);
    }
    return -1;
}

/*** END OF FILE ***/
