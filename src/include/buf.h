#ifndef BUF_H
#define BUF_H

#include <stddef.h>
#include <stdint.h>
#include <network.h>

enum {
    BUF_SIZ = 16348UL,
};

typedef struct {
    size_t size;
    size_t read_pos;
    uint8_t data[BUF_SIZ];
} buf_t;

int buf_send(net_t *p_net, buf_t *p_buf, int flags);
int buf_recv(net_t *p_net, buf_t *p_buf, int flags);

#endif
