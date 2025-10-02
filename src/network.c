#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#ifdef __GLIBC__
#include <asm-generic/errno.h>
#endif

#include "errors.h"
#include "log.h"
#include "network.h"
#include "utils.h"

enum {
    MAX_QUEUED_CONNECTIONS = 128,
    MIN_PORT = 1024,
    MAX_PORT = 65535,
    TIME_SINCE_LAST_RESOLUTION_SEC = 300,
};

static int connect_to_server(struct addrinfo *p_addrinfo);

static struct addrinfo *gp_cached_server_address = NULL; // NOLINT (cppcoreguidelines-avoid-non-const-global-variables)
static struct timespec gts_last_resolution = {0, 0};     // NOLINT (cppcoreguidelines-avoid-non-const-global-variables)

/**
 * @brief Free cached server address info.
 *
 * Call this function at program exit to avoid memory leaks.
 */
void network_free_cached_address(void) {
    if (NULL != gp_cached_server_address) {
        freeaddrinfo(gp_cached_server_address);
        gp_cached_server_address = NULL;
    }
}

/**
 * @brief Set a socket to non-blocking mode.
 *
 * Uses fcntl() to set O_NONBLOCK. This is required for event-driven IO.
 */
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

/**
 * @brief Connect to a remote server using address and port.
 *
 * Uses getaddrinfo() to resolve address, tries each result until connect succeeds.
 * Uses non-blocking sockets for event-driven connection.
 */
int network_connect_to_server(const char *addr, const char *port_str) {
    ASSERT_RET(NULL != addr); // NOLINT (misc-include-cleaner)
    ASSERT_RET(NULL != port_str);

    int sock_fd = -1;

    // Check if we have a cached address and if it's still valid.
    struct timespec ts_now = {0, 0};
    (void)clock_gettime(CLOCK_MONOTONIC, &ts_now); // NOLINT (misc-include-cleaner)

    if ((0 != gts_last_resolution.tv_sec) &&
        ((ts_now.tv_sec - gts_last_resolution.tv_sec) > TIME_SINCE_LAST_RESOLUTION_SEC)) {
        freeaddrinfo(gp_cached_server_address);
        gp_cached_server_address = NULL;
    }

    // Try cached address first if available.
    if (NULL != gp_cached_server_address) {
        sock_fd = connect_to_server(gp_cached_server_address);

        if (-1 == sock_fd) {
            freeaddrinfo(gp_cached_server_address);
            gp_cached_server_address = NULL;
        } else {
            (void)clock_gettime(CLOCK_MONOTONIC, &gts_last_resolution); // NOLINT (misc-include-cleaner)
            return sock_fd;
        }
    }

    // Resolve address if no cached address or if connection failed.
    if (NULL == gp_cached_server_address) {
        struct addrinfo hints = {0};
        hints.ai_family = AF_UNSPEC;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_flags = AI_NUMERICSERV | AI_ADDRCONFIG;

        struct addrinfo *p_gai_result = NULL;
        int gai_error_code = getaddrinfo(addr, port_str, &hints, &p_gai_result);
        if (0 != gai_error_code) {
            LOG(WRN, "getaddrinfo: %s", gai_strerror(gai_error_code));
            return -1;
        }

        if (NULL == p_gai_result) {
            LOG(WRN, "No addresses found for %s:%s", addr, port_str);
            return -1;
        }

        sock_fd = connect_to_server(p_gai_result);

        if (-1 != sock_fd) {
            gp_cached_server_address = p_gai_result;
        } else {
            freeaddrinfo(p_gai_result);
        }
    }

    return sock_fd;
}

/**
 * @brief Create a listening socket bound to the specified address and port.
 *
 * Handles both IPv4 and IPv6. Returns -1 on error.
 */
int network_get_listen_socket(const char *addr_str, const char *port_str) {
    ASSERT_RET(NULL != addr_str);
    ASSERT_RET(NULL != port_str);

    int server_fd = -1;
    struct addrinfo *p_gai_result = NULL;

    // Use getaddrinfo() to handle both IPv4 and IPv6 addresses.
    struct addrinfo hints = {0};
    hints.ai_family = AF_UNSPEC; // Allow IPv4 or IPv6
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_NUMERICSERV | AI_NUMERICHOST;

    int gai_error_code = getaddrinfo(addr_str, port_str, &hints, &p_gai_result);

    if (0 != gai_error_code) {
        LOG(WRN, "getaddrinfo: %s", gai_strerror(gai_error_code));
        return -1;
    }

    if (NULL == p_gai_result) {
        LOG(WRN, "No addresses found for %s:%s", addr_str, port_str);
        return -1;
    }

    struct addrinfo *p_curr = NULL;
    for (p_curr = p_gai_result; p_curr != NULL; p_curr = p_curr->ai_next) {
        // NOLINTNEXTLINE (hicpp-signed-bitwise)
        server_fd = socket(p_curr->ai_family, p_curr->ai_socktype | SOCK_CLOEXEC | SOCK_NONBLOCK, p_curr->ai_protocol);

        if (-1 == server_fd) {
            LOG(ERR, "socket");
            continue;
        }

        int yes_to_reuse = 1;
        if ((0 == setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &yes_to_reuse, sizeof(int))) &&
            (0 == bind(server_fd, p_curr->ai_addr, p_curr->ai_addrlen)) &&
            (0 == listen(server_fd, MAX_QUEUED_CONNECTIONS))) {
            break; // Successfully created, bound, and listening
        }

        LOG(ERR, "setsockopt/bind/listen");
        (void)close(server_fd);
        server_fd = -1;
    }

    if (NULL != p_gai_result) {
        freeaddrinfo(p_gai_result);
    }

    return server_fd;
}

static int connect_to_server(struct addrinfo *p_addrinfo) {
    ASSERT_RET(NULL != p_addrinfo);

    struct addrinfo *p_curr = NULL;
    for (p_curr = p_addrinfo; p_curr != NULL; p_curr = p_curr->ai_next) {
        // Try each resolved address until one succeeds.

        // NOLINTNEXTLINE (hicpp-signed-bitwise)
        int sock_fd = socket(p_curr->ai_family, p_curr->ai_socktype | SOCK_NONBLOCK, p_curr->ai_protocol);
        if (-1 == sock_fd) {
            LOG(ERR, "socket");
            continue;
        }

        if (-1 == connect(sock_fd, p_curr->ai_addr, p_curr->ai_addrlen)) {
            if (EINPROGRESS != errno) {
                LOG(ERR, "connect");
                (void)close(sock_fd);
                continue;
            }
        }

        return sock_fd;
    }

    return -1;
}

/*** END OF FILE ***/
