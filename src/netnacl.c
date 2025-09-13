#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef __GLIBC__
#include <asm-generic/errno-base.h>
#include <asm-generic/errno.h>
#endif

#include "log.h"
#include "netnacl.h"
#include "tweetnacl.h"
#include "utils.h"

struct netnacl_t { // NOLINT (clang-diagnostic-padded)
    size_t hdr_bytes_recvd;
    size_t ct_bytes_recvd;
    uint8_t pk[crypto_box_PUBLICKEYBYTES];
    uint8_t sk[crypto_box_SECRETKEYBYTES];
    uint8_t sym_key[crypto_box_BEFORENMBYTES];
    uint8_t recv_ct[crypto_box_ZEROBYTES + MAX_MESSAGE_LEN];
    uint8_t recv_pt[crypto_box_ZEROBYTES + MAX_MESSAGE_LEN];
    uint16_t recv_pt_pos;
    uint16_t recv_pt_len;
    uint16_t send_buf_len;
    uint16_t send_buf_pos;
    int sock_fd;
    uint8_t send_buf[sizeof(hdr_t) + crypto_box_ZEROBYTES + MAX_MESSAGE_LEN];
    hdr_t recv_hdr;
};

static int recv_hdr(netnacl_t *p_nn, int flags);
static int recv_ciphertext(netnacl_t *p_nn, int flags);
static int decrypt_ciphertext(netnacl_t *p_nn);
static ssize_t copy_plaintext_to_buffer(netnacl_t *p_nn, uint8_t *buf, size_t len);
static void encrypt_plaintext(netnacl_t *p_nn, const uint8_t *buf, size_t len);
static ssize_t send_ciphertext(netnacl_t *p_nn, size_t len, int flags);

static int g_urandom = -1; // NOLINT (cppcoreguidelines-avoid-non-const-global-variables)

void randombytes(uint8_t *buf, uint64_t sz) { // NOLINT (clang-diagnostic-missing-prototypes)

    size_t total_read_sz = 0;
    while (total_read_sz < sz) {
        ssize_t read_sz = getrandom(buf + total_read_sz, sz - total_read_sz, 0);

        if (-1 == read_sz) {
            LOG(ERR, "getrandom");
            if (EINTR == errno) {
                continue;
            }

            if (ENOSYS == errno) {
                goto READ_DEV_URANDOM;
            }
            _exit(1);
        }

        total_read_sz += (size_t)read_sz;
    }

READ_DEV_URANDOM:
    if (-1 == g_urandom) {
        g_urandom = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        if (-1 == g_urandom) {
            LOG(ERR, "open: /dev/urandom");
            _exit(1);
        }
    }

    total_read_sz = 0;
    while (total_read_sz < sz) {
        ssize_t read_sz = read(g_urandom, buf + total_read_sz, sz - total_read_sz);

        if (-1 == read_sz) {
            if (EINTR == errno) {
                continue;
            }

            LOG(ERR, "read: /dev/urandom");
            _exit(1);
        }

        total_read_sz += (size_t)read_sz;
    }
}

// TODO: make this non-blocking too
int netnacl_wrap(int sock_fd, netnacl_t **pp_nn) {
    ASSERT_RET(NULL == *pp_nn);
    ASSERT_RET(-1 < sock_fd);

    uint8_t peer_pk[crypto_box_PUBLICKEYBYTES] = {0};

    netnacl_t *p_nn = (netnacl_t *)calloc(1, sizeof(netnacl_t));
    if (NULL == p_nn) {
        LOG(ERR, "netnacl_t calloc");
        goto CLEANUP;
    }

    crypto_box_keypair(p_nn->pk, p_nn->sk);

    if ((crypto_box_PUBLICKEYBYTES != send(sock_fd, p_nn->pk, crypto_box_PUBLICKEYBYTES, 0)) ||
        (crypto_box_PUBLICKEYBYTES != recv(sock_fd, peer_pk, crypto_box_PUBLICKEYBYTES, 0))) {
        LOG(ERR, "could not exchange keys");
        goto CLEANUP;
    }

    crypto_box_beforenm(p_nn->sym_key, peer_pk, p_nn->sk);
    p_nn->sock_fd = sock_fd;

    *pp_nn = p_nn;
    return NN_SUCCESS;

CLEANUP:
    FREE_AND_NULL(p_nn);
    return NN_ERR;
}

ssize_t netnacl_recv(netnacl_t *p_nn, uint8_t *buf, size_t len, int flags) {
    ASSERT_RET(NULL != p_nn);
    ASSERT_RET(NULL != buf);

    ssize_t ret = 0;

    if (p_nn->hdr_bytes_recvd < sizeof(hdr_t)) {
        ret = recv_hdr(p_nn, flags);
        LOG(IO, "recvd %zu / %zu of header", p_nn->hdr_bytes_recvd, sizeof(hdr_t));
        if (ret) {
            goto EXIT;
        }
    }

    if ((p_nn->hdr_bytes_recvd == sizeof(hdr_t)) && (p_nn->ct_bytes_recvd < p_nn->recv_hdr.len)) {
        ret = recv_ciphertext(p_nn, flags);
        LOG(IO, "recvd %zu / %hu of ciphertext", p_nn->ct_bytes_recvd, p_nn->recv_hdr.len);
        if (ret) {
            goto EXIT;
        }
    }

    if ((p_nn->hdr_bytes_recvd == sizeof(hdr_t)) && (p_nn->ct_bytes_recvd == p_nn->recv_hdr.len) &&
        (0 == p_nn->recv_pt_len)) {
        ret = decrypt_ciphertext(p_nn);
        if (ret) {
            goto EXIT;
        }
    }

    if ((p_nn->hdr_bytes_recvd == sizeof(hdr_t)) && (p_nn->ct_bytes_recvd == p_nn->recv_hdr.len) &&
        (0 < p_nn->recv_pt_len)) {
        ret = copy_plaintext_to_buffer(p_nn, buf, len);
        LOG(IO, "read %ld / %zu requested", ret, len);
    }

    assert(p_nn->hdr_bytes_recvd <= sizeof(hdr_t));
    assert(p_nn->ct_bytes_recvd <= p_nn->recv_hdr.len);

EXIT:
    if (NN_DISCONNECT == ret) {
        ret = 0; // Match regular recv() semantics if remote end hangs up
    }

    return ret;
}

ssize_t netnacl_send(netnacl_t *p_nn, const uint8_t *buf, size_t len, int flags) {
    ASSERT_RET(NULL != p_nn);
    ASSERT_RET(NULL != buf);

    // only even try to send len bytes
    if (0 == p_nn->send_buf_len) {
        encrypt_plaintext(p_nn, buf, len);
    }

    ssize_t sent = 0;
    if (p_nn->send_buf_pos < p_nn->send_buf_len) {
        sent = send_ciphertext(p_nn, len, flags);
    }

    return sent;
}

static int recv_hdr(netnacl_t *p_nn, int flags) {

    while (p_nn->hdr_bytes_recvd < sizeof(hdr_t)) {
        ssize_t recvd =
            recv(p_nn->sock_fd, &p_nn->recv_hdr + p_nn->hdr_bytes_recvd, sizeof(hdr_t) - p_nn->hdr_bytes_recvd, flags);

        if (-1 == recvd) {
            LOG(ERR, "recv");
            if ((EAGAIN == errno) || (EWOULDBLOCK == errno)) {
                return NN_WOULD_BLOCK;
            }
            return NN_ERR;
        }

        if (0 == recvd) {
            LOG(INF, "disconnect");
            return NN_DISCONNECT;
        }

        p_nn->hdr_bytes_recvd += (size_t)recvd;
    }

    p_nn->recv_hdr.len = ntohs(p_nn->recv_hdr.len);

    return NN_SUCCESS;
}

static int recv_ciphertext(netnacl_t *p_nn, int flags) {
    while (p_nn->ct_bytes_recvd < p_nn->recv_hdr.len) {
        ssize_t recvd =
            recv(p_nn->sock_fd, p_nn->recv_ct + p_nn->ct_bytes_recvd, p_nn->recv_hdr.len - p_nn->ct_bytes_recvd, flags);

        if (-1 == recvd) {
            LOG(ERR, "recv");
            if ((EAGAIN == errno) || (EWOULDBLOCK == errno)) {
                return NN_WOULD_BLOCK;
            }
            return NN_ERR;
        }

        if (0 == recvd) {
            LOG(INF, "disconnect");
            return NN_DISCONNECT;
        }

        p_nn->ct_bytes_recvd += (size_t)recvd;
    }

    LOG(IO, "recvd %zu / %hu of ciphertext", p_nn->ct_bytes_recvd, p_nn->recv_hdr.len);

    return NN_SUCCESS;
}

static int decrypt_ciphertext(netnacl_t *p_nn) {
    ASSERT_RET(NULL != p_nn);
    LOG(DBG, "decrypting %hu bytes of ciphertext", p_nn->recv_hdr.len);

    if (0 != crypto_box_open_afternm(p_nn->recv_pt, p_nn->recv_ct, p_nn->recv_hdr.len, p_nn->recv_hdr.nonce,
                                     p_nn->sym_key)) {
        LOG(WRN, "crypto_box_open_afternm failed");
        return NN_CRYPTO_ERR;
    }

    p_nn->recv_pt_len = p_nn->recv_hdr.len - crypto_box_ZEROBYTES;
    memmove(p_nn->recv_pt, p_nn->recv_pt + crypto_box_ZEROBYTES, p_nn->recv_pt_len);

    return NN_SUCCESS;
}

static ssize_t copy_plaintext_to_buffer(netnacl_t *p_nn, uint8_t *buf, size_t len) {
    ASSERT_RET(NULL != p_nn);
    ASSERT_RET(NULL != buf);

    size_t read_sz = MIN(p_nn->recv_pt_len, len);
    memcpy(buf, p_nn->recv_pt + p_nn->recv_pt_pos, read_sz);

    p_nn->recv_pt_pos += read_sz;
    ASSERT_RET(p_nn->recv_pt_pos <= p_nn->recv_pt_len);
    if (p_nn->recv_pt_pos == p_nn->recv_pt_len) {
        memset(&p_nn->recv_hdr, 0, sizeof(hdr_t));
        memset(p_nn->recv_pt, 0, crypto_box_ZEROBYTES + MAX_MESSAGE_LEN);
        memset(p_nn->recv_ct, 0, crypto_box_ZEROBYTES + MAX_MESSAGE_LEN);
        p_nn->recv_pt_len = 0;
        p_nn->recv_pt_pos = 0;
        p_nn->hdr_bytes_recvd = 0;
        p_nn->ct_bytes_recvd = 0;
    }

    return (ssize_t)read_sz;
}

static void encrypt_plaintext(netnacl_t *p_nn, const uint8_t *buf, size_t len) {
    assert(NULL != p_nn);
    assert(NULL != buf);

    hdr_t send_hdr = {0};
    uint8_t pt_buf[crypto_box_ZEROBYTES + MAX_MESSAGE_LEN] = {0};

    LOG(DBG, "encrypting %zu bytes of plaintext", len);

    randombytes(send_hdr.nonce, crypto_box_NONCEBYTES);
    size_t pt_len = MIN(len, MAX_MESSAGE_LEN);
    size_t padded_pt_len = pt_len + crypto_box_ZEROBYTES;
    send_hdr.len = (uint16_t)pt_len + crypto_box_ZEROBYTES;
    p_nn->send_buf_len = send_hdr.len + sizeof(hdr_t);

    memcpy(pt_buf + crypto_box_ZEROBYTES, buf, pt_len);
    crypto_box_afternm(p_nn->send_buf + sizeof(hdr_t), pt_buf, padded_pt_len, send_hdr.nonce, p_nn->sym_key);

    send_hdr.len = htons(send_hdr.len);
    memcpy(p_nn->send_buf, &send_hdr, sizeof(hdr_t));
}

static ssize_t send_ciphertext(netnacl_t *p_nn, size_t len, int flags) {
    ASSERT_RET(NULL != p_nn);

    while (p_nn->send_buf_pos < p_nn->send_buf_len) {
        ssize_t sent =
            send(p_nn->sock_fd, p_nn->send_buf + p_nn->send_buf_pos, p_nn->send_buf_len - p_nn->send_buf_pos, flags);

        if (-1 == sent) {
            LOG(ERR, "send");
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                return NN_WOULD_BLOCK;
            }
            return NN_ERR;
        }

        assert((UINT16_MAX - p_nn->send_buf_pos) >= sent);

        p_nn->send_buf_pos += (uint16_t)sent;
    }

    memset(p_nn->send_buf, 0, p_nn->send_buf_len);
    p_nn->send_buf_len = 0;
    p_nn->send_buf_pos = 0;
    LOG(DBG, "finished sending message, reset state");

    return (ssize_t)MIN(len, MAX_MESSAGE_LEN);
}
