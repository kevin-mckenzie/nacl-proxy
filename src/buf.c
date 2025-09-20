#include <assert.h>
#include <errno.h>
#include <stddef.h>
#include <sys/socket.h>
#include <sys/types.h> // NOLINT (misc-include-cleaner)

#ifdef __GLIBC__
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#endif

#include "buf.h"
#include "errors.h"
#include "log.h"
#include "netnacl.h"
#include "network.h"
#include "utils.h"

int buf_send(net_t *p_net, buf_t *p_buf, int flags) {
    ASSERT_RET(NULL != p_net); // NOLINT (misc-include-cleaner)
    ASSERT_RET(p_buf->size != 0);
    ASSERT_RET(p_buf->read_pos != p_buf->size);

    while (p_buf->read_pos < p_buf->size) {
        ssize_t sent = 0;
        if (p_net->b_encrypted) {
            LOG(DBG, "sending encrypted data");
            sent = netnacl_send(p_net->netnacl, p_buf->data + p_buf->read_pos, p_buf->size - p_buf->read_pos, flags);
        } else {
            LOG(DBG, "sending unencrypted data");
            sent = send(p_net->sock_fd, p_buf->data + p_buf->read_pos, p_buf->size - p_buf->read_pos, flags);
        }

        if (0 > sent) {
            if ((NN_WOULD_BLOCK == sent) || (EWOULDBLOCK == errno) || (EAGAIN == errno)) {
                // Unlike buf_recv, we know the size of the data and must make sure it is all sent
                return PROXY_WOULD_BLOCK;
            }

            if ((EPIPE == errno) || (ECONNRESET == errno)) {
                return PROXY_DISCONNECT;
            }

            LOG(ERR, "send");
            return PROXY_ERR;
        }

        p_buf->read_pos += (size_t)sent;
    }
    LOG(IO, "sent %zu / %zu bytes on %d", p_buf->read_pos, p_buf->size, p_net->sock_fd);

    // Reset state on success
    p_buf->read_pos = 0;
    p_buf->size = 0;

    return PROXY_SUCCESS;
}

int buf_recv(net_t *p_net, buf_t *p_buf, int flags) {
    ASSERT_RET(NULL != p_net);
    ASSERT_RET(p_buf->read_pos == 0); // we should never be recving before a send from the same buffer is complete
    ASSERT_RET(p_buf->size == 0);

    while (p_buf->size < BUF_SIZ) {

        ssize_t recvd = 0;

        if (p_net->b_encrypted) {
            LOG(DBG, "receiving encrypted data");
            recvd = netnacl_recv(p_net->netnacl, p_buf->data + p_buf->size, BUF_SIZ - p_buf->size, flags);
        } else {
            LOG(DBG, "receiving unencrypted data");
            recvd = recv(p_net->sock_fd, p_buf->data + p_buf->size, BUF_SIZ - p_buf->size, flags);
        }

        if (0 >= recvd) {
            if (0 == recvd) {
                return PROXY_DISCONNECT;
            }

            if ((NN_WOULD_BLOCK == recvd) || (EWOULDBLOCK == errno) || (EAGAIN == errno)) {
                if (0 == p_buf->size) {
                    return PROXY_WOULD_BLOCK;
                }
                return PROXY_SUCCESS; // Since we are receiving an unknown amount of data, anything > 0 is considered
                                      // success.
            }

            LOG(ERR, "recv");
            return PROXY_ERR;
        }

        p_buf->size += (size_t)recvd;
    }

    LOG(IO, "recvd %zu bytes on [%d]", p_buf->size, p_net->sock_fd);

    return PROXY_SUCCESS;
}
