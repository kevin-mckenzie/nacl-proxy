#include <assert.h>
#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __GLIBC__
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#include <sys/poll.h>
#else
#include <poll.h>
#endif

#include "buf.h"
#include "errors.h"
#include "event.h"
#include "log.h"
#include "netnacl.h"
#include "network.h"
#include "proxy.h"

enum ProxySide {
    INVALID = -1,
    CLIENT = 1,
    SERVER = 2,
};

typedef struct { // NOLINT (clang-diagnostic-padded)
    config_t *config;
    net_t client;
    net_t server;
    buf_t client_send_buf;
    buf_t server_send_buf;
    int ref;
} conn_t;

static int accept_callback(int listen_fd, short revents, void *p_data);
/** Accept event callback: Allocates a new connection context and handles incoming client connections. */

static int handle_accept(int listen_fd, conn_t *p_conn);
/** Accepts a client connection and initiates outgoing server connection. Cleans up on error. */

static int pending_connect_callback(int conn_fd, short revents, void *p_data);
/** Handles completion of non-blocking server connect. Cleans up on error. */

static int add_connection_events(conn_t *p_conn);
/** Registers POLLIN/POLLOUT events for both client and server sockets, depending on encryption. */

static int handshake_callback(int conn_fd, short revents, void *p_data);
/** Handles handshake for encrypted connections, switching to data events after success. */

static int do_handshake(conn_t *p_conn, enum ProxySide side);
/** Performs key exchange and sets up event for encrypted communication. */

static int conn_callback(int conn_fd, short revents, void *p_data);
/** Main connection event handler: routes to recv/send logic and manages disconnects. */

static int handle_recv(conn_t *p_conn, enum ProxySide side);
/** Receives data from one side, buffers for forwarding, and manages disconnect logic. */

static int handle_send(conn_t *p_conn, enum ProxySide side);
/** Sends buffered data to one side, manages event state and disconnect logic. */

static void close_connection(conn_t **pp_conn);
/** Removes events, closes sockets, and frees connection context. */

static void free_connection(conn_t *p_conn);
/** Frees connection context, including encryption state, when refcount reaches zero. */

volatile sig_atomic_t g_run_flag = 1; // NOLINT

static void signal_handler(int arg) {
    (void)arg;
    ssize_t _ = write(STDERR_FILENO, "GOT SIGINT/SIGTERM\n", 1);
    g_run_flag = 0; // Stop event loop on signal
}

int proxy_run(config_t *p_config) {
    ASSERT_RET(NULL != p_config); // NOLINT (misc-include-cleaner)

    LOG(DBG, "START: %d", getpid());

    struct sigaction act = {0};
    act.sa_handler = signal_handler;
    sigaction(SIGINT, &act, NULL);
    sigaction(SIGTERM, &act, NULL);

    int err = 0;
    bool b_event_added = false;

    int server_fd = network_get_listen_socket(p_config->bind_addr, p_config->bind_port);
    if (-1 == server_fd) {
        err = PROXY_ERR;
        goto CLEANUP;
    }

    err = event_add(server_fd, POLLIN, p_config, accept_callback);
    if (err) {
        goto CLEANUP;
    }
    b_event_added = true;

    err = event_run_loop(&g_run_flag, -1);

CLEANUP:
    if (b_event_added) {
        (void)event_remove(server_fd);
    }
    if (-1 != server_fd) {
        close(server_fd);
    }
    event_teardown((void (*)(void *))free_connection); // NOLINT (clang-diagnostic-cast-function-type-strict)
    return err;
}

static int accept_callback(int listen_fd, short revents, void *p_data) {
    // Accept new client connection and allocate connection context.
    ASSERT_RET(-1 < listen_fd);
    ASSERT_RET(0 != revents);
    ASSERT_RET(NULL != p_data);

    // If listener socket has error/hangup, log and return error.
    if ((POLLERR | POLLHUP | POLLNVAL | POLLOUT) & revents) { // NOLINT (hicpp-signed-bitwise)
        LOG(ERR, "listener revents: %hx", (unsigned short)revents);
        return PROXY_ERR;
    }

    int err = PROXY_SUCCESS;
    config_t *p_config = (config_t *)p_data;
    conn_t *p_conn = NULL;

    if (POLLIN & revents) {

        p_conn = (conn_t *)calloc(1, sizeof(conn_t));
        if (NULL == p_conn) {
            LOG(ERR, "conn_t calloc");
            err = PROXY_ERR;
            goto CLEANUP;
        }
        p_conn->config = p_config;
        p_conn->client.b_encrypted = p_conn->config->b_encrypt_in;
        p_conn->server.b_encrypted = p_conn->config->b_encrypt_out;

        err = handle_accept(listen_fd, p_conn);
        if (err) {
            goto CLEANUP;
        }
    }

    return err;

CLEANUP:

    if (NULL != p_conn) {
        FREE_AND_NULL(p_conn);
    }

    // Listener should keep running if these errors happened, otherwise entire proxy will exit.
    if ((PROXY_MAX_EVENTS == err) || (PROXY_INCOMPLETE_ACCEPT == err) || (PROXY_CONNECT_ERR == err)) {
        err = PROXY_SUCCESS;
    }

    return err;
}

static int handle_accept(int listen_fd, conn_t *p_conn) {
    // Accept client and connect to server. Clean up on error.
    ASSERT_RET(NULL != p_conn);

    int err = PROXY_SUCCESS;

    p_conn->client.sock_fd = accept(listen_fd, NULL, NULL); // NOLINT (android-cloexec-accept)
    if (-1 == p_conn->client.sock_fd) {
        LOG(ERR, "accept");
        // Don't exit for transient errors, just try again later.
        if ((ECONNABORTED == errno) && (EAGAIN == errno) && (EWOULDBLOCK == errno)) {
            err = PROXY_INCOMPLETE_ACCEPT;
        } else {
            err = PROXY_ERR;
        }
        goto CLEANUP;
    }

    err = network_set_sock_nonblocking(p_conn->client.sock_fd);
    if (err) {
        goto CLEANUP;
    }

    p_conn->server.sock_fd = network_connect_to_server(p_conn->config->server_addr, p_conn->config->server_port);
    if (-1 == p_conn->server.sock_fd) {
        LOG(ERR, "Could not connect to server");
        // Could not connect to server, clean up and continue accepting.
        err = PROXY_CONNECT_ERR;
        goto CLEANUP;
    }

    if (EINPROGRESS == errno) {
        errno = 0;
        // Connection is pending, register POLLOUT to complete handshake later.
        err = event_add(p_conn->server.sock_fd, POLLOUT, p_conn, pending_connect_callback);
    } else {
        // Connection established immediately, register data events.
        err = add_connection_events(p_conn);
    }

    return err;

CLEANUP:

    if (-1 != p_conn->server.sock_fd) {
        close(p_conn->server.sock_fd);
        p_conn->server.sock_fd = -1;
    }

    if (-1 != p_conn->client.sock_fd) {
        close(p_conn->client.sock_fd);
        p_conn->client.sock_fd = -1;
    }

    return err;
}

static int pending_connect_callback(int conn_fd, short revents, void *p_data) {
    // Complete non-blocking connect and register data events.
    ASSERT_RET(-1 < conn_fd);
    ASSERT_RET(0 != revents);
    ASSERT_RET(NULL != p_data);

    int err = PROXY_SUCCESS;
    conn_t *p_conn = (conn_t *)p_data;
    ASSERT_RET(p_conn->server.sock_fd == conn_fd);

    if (POLLNVAL & revents) {
        goto CLEANUP;
    }

    if ((POLLERR | POLLHUP | POLLOUT) & revents) { // NOLINT (hicpp-signed-bitwise)
        int sock_err = 1;
        socklen_t opt_len = sizeof(int);
        if (-1 == getsockopt(conn_fd, SOL_SOCKET, SO_ERROR, &sock_err, &opt_len)) {
            LOG(ERR, "getsockopt");
            err = PROXY_ERR;
            goto CLEANUP;
        }

        if (0 != sock_err) {
            LOG(INF, "Could not complete pending connection for %d", conn_fd);
            goto CLEANUP;
        }

        // Connection established, switch to normal events.
        (void)event_remove(conn_fd); // Avoid duplicate event error
        err = add_connection_events(p_conn);
        if (err) {
            goto CLEANUP;
        }
    }

    return err;

CLEANUP:
    close_connection(&p_conn);
    return err;
}

static int add_connection_events(conn_t *p_conn) {
    // Register POLLIN/POLLOUT events for both client and server sockets.
    ASSERT_RET(NULL != p_conn);
    ASSERT_RET(-1 < p_conn->client.sock_fd);
    ASSERT_RET(-1 < p_conn->server.sock_fd);

    int err = PROXY_SUCCESS;

    // If encrypted, start with handshake (POLLOUT), else start with data (POLLIN).
    if (p_conn->client.b_encrypted) {
        err = event_add(p_conn->client.sock_fd, POLLOUT, p_conn, handshake_callback);
    } else {
        err = event_add(p_conn->client.sock_fd, POLLIN, p_conn, conn_callback);
    }

    if (err) {
        return err;
    }

    p_conn->ref++;

    if (p_conn->server.b_encrypted) {
        err = event_add(p_conn->server.sock_fd, POLLOUT, p_conn, handshake_callback);
    } else {
        err = event_add(p_conn->server.sock_fd, POLLIN, p_conn, conn_callback);
    }

    if (err) {
        (void)event_remove(p_conn->client.sock_fd);
    } else {
        p_conn->ref++;
    }

    return err;
}

static int handshake_callback(int conn_fd, short revents, void *p_data) {
    // Handles handshake for encrypted connections, switching to data events after success.
    ASSERT_RET(-1 < conn_fd);
    ASSERT_RET(0 != revents);
    ASSERT_RET(NULL != p_data);

    int err = PROXY_SUCCESS;
    conn_t *p_conn = (conn_t *)p_data;

    if ((POLLERR | POLLHUP | POLLNVAL) & revents) { // NOLINT (hicpp-signed-bitwise)
        LOG(ERR, "Handshake conn revents: %hx", (unsigned short)revents);
        err = PROXY_ERR;
        goto CLEANUP;
    }

    if (conn_fd == p_conn->client.sock_fd) {
        err = do_handshake(p_conn, CLIENT);
    } else if (conn_fd == p_conn->server.sock_fd) {
        err = do_handshake(p_conn, SERVER);
    } else {
        err = PROXY_ERR;
    }

    if (err) {
        if (NN_ERR == err) {
            err = PROXY_SUCCESS; // Handshake failed, keep the event loop running though
        }
        goto CLEANUP;
    }

    return err;

CLEANUP:
    LOG(ERR, "removing conn");
    close_connection(&p_conn);
    return err;
}

static int do_handshake(conn_t *p_conn, enum ProxySide side) {
    // Perform key exchange and set up event for encrypted communication.
    ASSERT_RET(NULL != p_conn);

    short events = POLLIN;
    net_t *p_net = NULL;

    switch (side) { // NOLINT (clang-diagnostic-switch-enum)
    case CLIENT:
        p_net = &p_conn->client;
        if (0 < p_conn->client_send_buf.size) {
            events = POLLOUT;
        }
        break;
    case SERVER:
        p_net = &p_conn->server;
        if (0 < p_conn->server_send_buf.size) {
            events = POLLOUT;
        }
        break;
    default:
        return PROXY_ERR;
    }

    ASSERT_RET(p_net->b_encrypted);

    if (NULL == p_net->netnacl) {
        p_net->netnacl = netnacl_create(p_net->sock_fd);
        if (NULL == p_net->netnacl) {
            return PROXY_ERR;
        }
    }

    int err = netnacl_wrap(p_net->netnacl);

    switch (err) {
    case NN_SUCCESS:
        (void)event_remove(p_net->sock_fd);
        return event_add(p_net->sock_fd, events, p_conn, conn_callback);
    case NN_WANT_READ:
        return event_modify(p_net->sock_fd, POLLIN);
    case NN_WANT_WRITE:
        return event_modify(p_net->sock_fd, POLLOUT);
    default:
        return err;
    }
}

static int conn_callback(int conn_fd, short revents, void *p_data) {
    // Main connection event handler: routes to recv/send logic and manages disconnects.
    ASSERT_RET(-1 < conn_fd);
    ASSERT_RET(NULL != p_data);
    ASSERT_RET(0 != revents);

    conn_t *p_conn = (conn_t *)p_data;

    if (POLLNVAL & revents) {
        LOG(WRN, "POLLNVAL: closed socket %d should not be in event handler", conn_fd);
        return PROXY_ERR;
    }

    if ((POLLERR | POLLHUP) & revents) // NOLINT (hicpp-signed-bitwise)
    {
        close_connection(&p_conn);
        LOG(INF, "removing %d: revents: %hx", conn_fd, (unsigned short)revents);
        return PROXY_SUCCESS;
    }

    enum ProxySide side = INVALID;
    if (conn_fd == p_conn->client.sock_fd) {
        side = CLIENT;
    } else if (conn_fd == p_conn->server.sock_fd) {
        side = SERVER;
    } else {
        LOG(ERR, "Event FD does not correspond to either client (%d) or server (%d): %d", p_conn->client.sock_fd,
            conn_fd == p_conn->server.sock_fd, conn_fd);
        return PROXY_ERR;
    }

    int err = 0;
    if ((POLLIN & revents)) {
        err = handle_recv(p_conn, side);
    }

    if ((POLLOUT & revents) && (0 == err)) {
        err = handle_send(p_conn, side);
    }

    if (PROXY_DISCONNECT == err) {
        err = PROXY_SUCCESS;
    }

    return err;
}

static int handle_recv(conn_t *p_conn, enum ProxySide side) {
    // Receives data from one side, buffers for forwarding, and manages disconnect logic.
    ASSERT_RET(NULL != p_conn);
    ASSERT_RET((CLIENT == side) || (SERVER == side));

    buf_t *p_buf = NULL;
    net_t *p_net = NULL;
    int mod_fd = -1;

    if (CLIENT == side) {
        p_net = &p_conn->client;
        mod_fd = p_conn->server.sock_fd;
        p_buf = &p_conn->server_send_buf;
    } else { // SERVER == side
        p_net = &p_conn->server;
        mod_fd = p_conn->client.sock_fd;
        p_buf = &p_conn->client_send_buf;
    }

    // If we have buffered data waiting to send, don't receive more until it's sent.
    if (0 != p_buf->size) {
        return PROXY_SUCCESS;
    }

    int err = buf_recv(p_net, p_buf, 0);

    if ((PROXY_SUCCESS == err) || ((PROXY_DISCONNECT == err) && (0 != p_buf->size))) {
        // If this side disconnects but we also got data, we need to make sure the data still gets sent.
        // Close this side's socket then mark the other socket for sending as normal.
        if (PROXY_DISCONNECT == err) {
            LOG(INF, "Disconnect on [%d]", p_net->sock_fd);
            (void)event_remove(p_net->sock_fd);
            (void)close(p_net->sock_fd);
            p_net->sock_fd = -1;
            if (p_net->b_encrypted) {
                FREE_AND_NULL(p_net->netnacl);
            }
        }

        if (-1 == mod_fd) {
            LOG(INF, "Cannot send because other end is already disconnected");
            close_connection(&p_conn);
            return PROXY_DISCONNECT;
        }
        return event_modify(mod_fd, POLLIN | POLLOUT);
    }

    if (PROXY_WOULD_BLOCK == err) {
        return PROXY_SUCCESS;
    }

    LOG(INF, "Closing both ends of connection due to recv error or disconnect");
    close_connection(&p_conn);
    return err;
}

static int handle_send(conn_t *p_conn, enum ProxySide side) {
    // Sends buffered data to one side, manages event state and disconnect logic.
    ASSERT_RET(NULL != p_conn);
    ASSERT_RET((CLIENT == side) || (SERVER == side));

    buf_t *p_buf = NULL;
    net_t *p_conn_net = NULL;
    const net_t *p_mod_net = NULL;

    if (CLIENT == side) {
        p_conn_net = &p_conn->client;
        p_mod_net = &p_conn->server;
        p_buf = &p_conn->client_send_buf;
    } else { // SERVER == side
        p_conn_net = &p_conn->server;
        p_mod_net = &p_conn->client;
        p_buf = &p_conn->server_send_buf;
    }

    int err = buf_send(p_conn_net, p_buf, MSG_NOSIGNAL);

    if (0 == err) {
        // If peer is disconnected, close both ends after sending buffered data.
        if (-1 == p_mod_net->sock_fd) {
            LOG(INF, "After completing pending send, closing [%d] due to prior peer disconnect", p_mod_net->sock_fd);
            close_connection(&p_conn);
            return PROXY_DISCONNECT;
        }
        return event_modify(p_conn_net->sock_fd, POLLIN);
    }

    if (PROXY_WOULD_BLOCK == err) {
        return PROXY_SUCCESS;
    }

    LOG(WRN, "Closing both ends of connection due to send error");
    close_connection(&p_conn);
    return err;
}

static void close_connection(conn_t **pp_conn) {
    // Remove events, close sockets, and free connection context.
    assert(NULL != pp_conn);
    assert(NULL != *pp_conn);

    conn_t *p_conn = *pp_conn;
    if (-1 != p_conn->client.sock_fd) {
        (void)event_remove(p_conn->client.sock_fd);
        (void)close(p_conn->client.sock_fd);
        p_conn->client.sock_fd = -1;
    }

    if (-1 != p_conn->server.sock_fd) {
        (void)event_remove(p_conn->server.sock_fd);
        (void)close(p_conn->server.sock_fd);
        p_conn->server.sock_fd = -1;
    }

    p_conn->ref = 0;
    free_connection(p_conn);
    *pp_conn = NULL;
}

static void free_connection(conn_t *p_conn) {
    // Frees connection context, including encryption state, when refcount reaches zero.
    if (NULL != p_conn) {
        if (1 >= p_conn->ref) {
            if (NULL != p_conn->client.netnacl) {
                FREE_AND_NULL(p_conn->client.netnacl);
            }

            if (NULL != p_conn->server.netnacl) {
                FREE_AND_NULL(p_conn->server.netnacl);
            }
            free(p_conn);
        } else {
            p_conn->ref--;
        }
    }
}
