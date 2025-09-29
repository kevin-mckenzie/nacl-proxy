#ifndef BUF_H
#define BUF_H

#include <network.h>
#include <stddef.h>
#include <stdint.h>

enum {
    BUF_SIZ = 16348UL,
};

typedef struct {
    size_t size;           // amount of data in the buffer
    size_t read_pos;       // current read position in the buffer
    uint8_t data[BUF_SIZ]; // data
} buf_t;

/**
 * @brief Send the contents of a buf_t over a network connection.
 *
 * @param p_net Networking context over which to send the data.
 * @param p_buf Buffer containing the data to send.
 * @param flags Flags passed directly to send(2).
 * @return int 0 on success, negative value on error. PROXY_WOULD_BLOCK if the operation would block,
 * PROXY_DISCONNECT if the connection was closed, PROXY_ERR for other errors.
 */
int buf_send(net_t *p_net, buf_t *p_buf, int flags);

/**
 * @brief Receive up to BUF_SIZ bytes into a buf_t over a network network connection.
 *
 * @param p_net Networking context from which to receive the data.
 * @param p_buf Buffer to store the received data.
 * @param flags Flags passed directly to recv(2).
 * @return int 0 on success, negative value on error. PROXY_WOULD_BLOCK if the operation would block,
 * PROXY_DISCONNECT if the connection was closed, PROXY_ERR for other errors.
 */
int buf_recv(net_t *p_net, buf_t *p_buf, int flags);

#endif
