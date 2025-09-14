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

int proxy_run(config_t *p_config);

#endif /* PROXY_H */