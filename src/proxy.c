#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __GLIBC__
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

typedef struct {
    config_t *config;
    net_t client;
    net_t server;
    buf_t client_send_buf;
    buf_t server_send_buf;
} conn_t;

static int accept_callback(int listen_fd, short revents, void *p_data);
static int establish_connections(int listen_fd, conn_t *p_conn);
static int establish_connection(int listen_fd, conn_t *p_conn, enum ProxySide side);
static int conn_callback(int conn_fd, short revents, void *p_data);
static int handle_recv(conn_t *p_conn, enum ProxySide side);
static int handle_send(conn_t *p_conn, enum ProxySide side);
static void close_connection(conn_t **pp_conn);

int proxy_run(config_t *p_config) {
    ASSERT_RET(NULL != p_config); // NOLINT (misc-include-cleaner)

    int err = 0;
    bool b_event_added = false;
    int run = 1;

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

    err = event_run_loop(&run, -1);

CLEANUP:
    if (b_event_added) {
        (void)event_remove(server_fd);
    }
    if (-1 != server_fd) {
        close(server_fd);
    }
    return err;
}

static int accept_callback(int listen_fd, short revents, void *p_data) {
    ASSERT_RET(-1 < listen_fd);
    ASSERT_RET(0 != revents);
    ASSERT_RET(NULL != p_data);

    config_t *p_config = (config_t *)p_data;

    if ((POLLERR | POLLHUP | POLLNVAL) & revents) { // NOLINT (hicpp-signed-bitwise)
        LOG(ERR, "listener revents: %hx", (unsigned short)revents);
        close(listen_fd);
        event_remove(listen_fd);
        return PROXY_ERR;
    }

    if (POLLIN & revents) {
        conn_t *p_conn = (conn_t *)calloc(1, sizeof(conn_t));
        if (NULL == p_conn) {
            LOG(ERR, "conn_t calloc");
            return PROXY_ERR;
        }
        p_conn->config = p_config;

        return establish_connections(listen_fd, p_conn);
    }

    return 0; // Spurious event?
}

static int establish_connections(int listen_fd, conn_t *p_conn) {
    ASSERT_RET(-1 < listen_fd);
    ASSERT_RET(NULL != p_conn);

    if ((0 != establish_connection(listen_fd, p_conn, CLIENT)) ||
        (0 != establish_connection(listen_fd, p_conn, SERVER))) {
        close_connection(&p_conn);
        return PROXY_ERR;
    }

    return 0;
}

static int establish_connection(int listen_fd, conn_t *p_conn, enum ProxySide side) {
    ASSERT_RET(-1 < listen_fd);
    ASSERT_RET(NULL != p_conn);
    ASSERT_RET((CLIENT == side) || (SERVER == side));

    int err = PROXY_SUCCESS;

    net_t *p_net = NULL;
    if (CLIENT == side) {
        p_net = &p_conn->client;
        p_net->sock_fd = accept(listen_fd, NULL, NULL); // NOLINT (android-cloexec-accept)
        if (-1 == p_net->sock_fd) {
            LOG(ERR, "accept");
            err = PROXY_ERR;
            goto CLEANUP;
        }
        p_net->b_encrypted = p_conn->config->b_encrypt_in;

    } else { // SERVER == side
        p_net = &p_conn->server;
        p_net->sock_fd = network_connect_to_server(p_conn->config->server_addr, p_conn->config->server_port);
        if (-1 == p_net->sock_fd) {
            err = PROXY_ERR;
            goto CLEANUP;
        }
        p_net->b_encrypted = p_conn->config->b_encrypt_out;
    }

    if (p_net->b_encrypted) {
        err = netnacl_wrap(p_net->sock_fd, &p_net->netnacl);
        if (err) {
            goto CLEANUP;
        }
    }

    err = network_set_sock_nonblocking(p_net->sock_fd);
    if (err) {
        goto CLEANUP;
    }

    err = event_add(p_net->sock_fd, POLLIN, p_conn, conn_callback);
    if (err) {
        goto CLEANUP;
    }

    LOG(INF, "Received connection on [%d]", p_net->sock_fd);
    return err;

CLEANUP:
    if (-1 != p_net->sock_fd) {
        close(p_net->sock_fd);
        p_net->sock_fd = -1;
    }

    return err;
}

static int conn_callback(int conn_fd, short revents, void *p_data) {
    ASSERT_RET(-1 < conn_fd);
    ASSERT_RET(NULL != p_data);

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

    // We got POLLIN to recv more data but the other socket has not sent it yet; so return to let it send
    if (0 != p_buf->size) {
        return PROXY_SUCCESS;
    }

    int err = buf_recv(p_net, p_buf, 0);

    if ((PROXY_SUCCESS == err) || ((PROXY_DISCONNECT == err) && (0 != p_buf->size))) {
        // If this side disconnects but we also got data, we need to make make sure the data still gets sent.
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

    int err = buf_send(p_conn_net, p_buf, 0);

    if (0 == err) {
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
    assert(NULL != pp_conn);
    assert(NULL != *pp_conn);

    conn_t *p_conn = *pp_conn;
    if (-1 != p_conn->client.sock_fd) {
        (void)event_remove(p_conn->client.sock_fd);
        (void)close(p_conn->client.sock_fd);
        p_conn->client.sock_fd = -1;
        if (p_conn->client.b_encrypted) {
            FREE_AND_NULL(p_conn->client.netnacl);
        }
    }

    if (-1 != p_conn->server.sock_fd) {
        (void)event_remove(p_conn->server.sock_fd);
        (void)close(p_conn->server.sock_fd);
        p_conn->server.sock_fd = -1;
        if (p_conn->client.b_encrypted) {
            FREE_AND_NULL(p_conn->client.netnacl);
        }
    }

    FREE_AND_NULL(*pp_conn);
}
