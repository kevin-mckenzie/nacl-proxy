#ifndef PROXY_H
#define PROXY_H

typedef struct {
    char *server_addr;
    char *server_port;
    char *bind_addr;
    char *bind_port;
    bool b_encrypt_in;
    bool b_encrypt_out;
} config_t;

/**
 * @brief Runs the proxy main loop using the provided configuration.
 *
 * Sets up signal handlers, creates a listening socket, and registers the accept event.
 * Handles incoming client connections, establishes outgoing server connections,
 * and manages encrypted or unencrypted data forwarding between endpoints.
 * Cleans up all resources and events on exit.
 *
 * @param p_config Pointer to proxy configuration.
 * @return PROXY_SUCCESS on clean exit,
 *         PROXY_ERR or other error codes on failure.
 */
int proxy_run(config_t *p_config);

#endif /* PROXY_H */