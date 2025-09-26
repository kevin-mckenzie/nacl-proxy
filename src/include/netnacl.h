#ifndef NETNACL_H
#define NETNACL_H

#include <stdint.h>
#include <sys/types.h>

#include "tweetnacl.h"

enum {
    NN_DISCONNECT = -5,
    NN_CRYPTO_ERR,
    NN_WANT_READ,
    NN_WANT_WRITE,
    NN_ERR,
    NN_SUCCESS,
};

enum {
    MAX_MESSAGE_LEN = 4096,
};

typedef struct {
    uint16_t len;
    uint8_t nonce[crypto_box_NONCEBYTES];
} __attribute__((packed)) hdr_t;

typedef struct netnacl_t netnacl_t;

netnacl_t *netnacl_create(int sock_fd);

int netnacl_wrap(netnacl_t *p_nn);

ssize_t netnacl_recv(netnacl_t *p_nn, uint8_t *buf, size_t len, int flags);

ssize_t netnacl_send(netnacl_t *p_nn, const uint8_t *buf, size_t len, int flags);

#endif
