#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/types.h> // NOLINT (misc-include-cleaner)
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
    size_t key_bytes_sent;
    size_t key_bytes_recvd;
    size_t hdr_bytes_recvd;
    size_t ct_bytes_recvd;
    uint8_t pk[crypto_box_PUBLICKEYBYTES];
    uint8_t sk[crypto_box_SECRETKEYBYTES];
    uint8_t peer_pk[crypto_box_PUBLICKEYBYTES];
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
/** Receives message header, handling partial reads and disconnects. */

static int recv_ciphertext(netnacl_t *p_nn, int flags);
/** Receives ciphertext for a message, handling partial reads and disconnects. */

static int decrypt_ciphertext(netnacl_t *p_nn);
/** Decrypts received ciphertext and prepares plaintext buffer. */

static ssize_t copy_plaintext_to_buffer(netnacl_t *p_nn, uint8_t *buf, size_t len);
/** Copies decrypted plaintext to user buffer, resets state when done. */

static void encrypt_plaintext(netnacl_t *p_nn, const uint8_t *buf, size_t len);
/** Encrypts plaintext and prepares send buffer for transmission. */

static ssize_t send_ciphertext(netnacl_t *p_nn, size_t len, int flags);
/** Sends ciphertext buffer, handling partial writes and resetting state. */

static int g_urandom = -1; // NOLINT (cppcoreguidelines-avoid-non-const-global-variables)

/**
 * @brief Fill a buffer with cryptographically secure random bytes.
 *
 * Uses getrandom() if available, otherwise falls back to /dev/urandom.
 * Exits the process if no entropy source is available.
 */
void randombytes(uint8_t *buf, uint64_t sz) { // NOLINT (clang-diagnostic-missing-prototypes)
    size_t total_sz = (size_t)sz;
    size_t total_read_sz = 0;
    while (total_read_sz < total_sz) {
        ssize_t read_sz = getrandom(buf + total_read_sz, total_sz - total_read_sz, 0);

        if (-1 == read_sz) {
            LOG(ERR, "getrandom");
            if (EINTR == errno) {
                continue; // Retry if interrupted
            }

            if (ENOSYS == errno) {
                goto READ_DEV_URANDOM; // Fallback if getrandom is not available
            }
            _exit(1); // Fatal error, cannot continue
        }

        total_read_sz += (size_t)read_sz;
    }

READ_DEV_URANDOM:
    if (-1 == g_urandom) {
        // NOLINTNEXTLINE (clang-analyzer-unix.API) - False positive on mips
        g_urandom = open("/dev/urandom", O_RDONLY | O_CLOEXEC);
        if (-1 == g_urandom) {
            LOG(ERR, "open: /dev/urandom");
            _exit(1);
        }
    }

    total_read_sz = 0;
    while (total_read_sz < total_sz) {
        ssize_t read_sz = read(g_urandom, buf + total_read_sz, total_sz - total_read_sz);

        if (-1 == read_sz) {
            if (EINTR == errno) {
                continue; // Retry if interrupted
            }

            LOG(ERR, "read: /dev/urandom");
            _exit(1); // Fatal error, cannot continue
        }

        total_read_sz += (size_t)read_sz;
    }
}

netnacl_t *netnacl_create(int sock_fd) {
    netnacl_t *p_nn = (netnacl_t *)calloc(1, sizeof(netnacl_t));
    if (NULL == p_nn) { // NOLINT (misc-include-cleaner)
        LOG(ERR, "netnacl_t calloc");
    } else {
        p_nn->sock_fd = sock_fd;
    }
    return p_nn;
}

int netnacl_wrap(netnacl_t *p_nn) {
    ASSERT_RET(NULL != p_nn);

    if (p_nn->key_bytes_sent == 0) {
        crypto_box_keypair(p_nn->pk, p_nn->sk); // Generate keypair only once
    }

    // Send our public key, handling partial writes
    while (p_nn->key_bytes_sent < crypto_box_PUBLICKEYBYTES) {
        ssize_t sent = send(p_nn->sock_fd, p_nn->pk + p_nn->key_bytes_sent,
                            crypto_box_PUBLICKEYBYTES - p_nn->key_bytes_sent, MSG_NOSIGNAL);

        if (-1 == sent) {
            if ((EAGAIN == errno) || (EWOULDBLOCK == errno)) {
                return NN_WANT_WRITE; // Wait for socket to be writable
            }
            return NN_ERR;
        }

        p_nn->key_bytes_sent += (size_t)sent;
    }

    // Receive peer's public key, handling partial reads
    while (p_nn->key_bytes_recvd < crypto_box_PUBLICKEYBYTES) {
        ssize_t recvd = recv(p_nn->sock_fd, p_nn->peer_pk + p_nn->key_bytes_recvd,
                             crypto_box_PUBLICKEYBYTES - p_nn->key_bytes_recvd, 0);

        if (-1 == recvd) {
            if ((EAGAIN == errno) || (EWOULDBLOCK == errno)) {
                return NN_WANT_READ; // Wait for socket to be readable
            }
            return NN_ERR;
        }

        if (0 == recvd) {
            return NN_ERR; // Remote closed connection
        }

        p_nn->key_bytes_recvd += (size_t)recvd;
    }

    // Derive shared symmetric key for encryption
    crypto_box_beforenm(p_nn->sym_key, p_nn->peer_pk, p_nn->sk);

    return NN_SUCCESS;
}

ssize_t netnacl_recv(netnacl_t *p_nn, uint8_t *buf, size_t len, int flags) {
    // #lizard forgives
    // This is not that complicated and I want to keep the asserts
    ASSERT_RET(NULL != p_nn);
    ASSERT_RET(NULL != buf);

    ssize_t ret = 0;

    // Receive header if needed
    if (p_nn->hdr_bytes_recvd < sizeof(hdr_t)) {
        ret = recv_hdr(p_nn, flags);
        LOG(IO, "recvd %zu / %zu of header", p_nn->hdr_bytes_recvd, sizeof(hdr_t));
        if (ret) {
            goto EXIT;
        }
    }

    // Receive ciphertext if needed
    if ((p_nn->hdr_bytes_recvd == sizeof(hdr_t)) && (p_nn->ct_bytes_recvd < p_nn->recv_hdr.len)) {
        ret = recv_ciphertext(p_nn, flags);
        LOG(IO, "recvd %zu / %hu of ciphertext", p_nn->ct_bytes_recvd, p_nn->recv_hdr.len);
        if (ret) {
            goto EXIT;
        }
    }

    // Decrypt ciphertext if needed
    if ((p_nn->hdr_bytes_recvd == sizeof(hdr_t)) && (p_nn->ct_bytes_recvd == p_nn->recv_hdr.len) &&
        (0 == p_nn->recv_pt_len)) {
        ret = decrypt_ciphertext(p_nn);
        if (ret) {
            goto EXIT;
        }
    }

    // Copy plaintext to user buffer
    if ((p_nn->hdr_bytes_recvd == sizeof(hdr_t)) && (p_nn->ct_bytes_recvd == p_nn->recv_hdr.len) &&
        (0 < p_nn->recv_pt_len)) {
        ret = copy_plaintext_to_buffer(p_nn, buf, len);
        LOG(IO, "read %zd / %zu requested", ret, len);
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

    // Only even try to send len bytes
    if (0 == p_nn->send_buf_len) {
        encrypt_plaintext(p_nn, buf, len); // Prepare send buffer
    }

    ssize_t sent = 0;
    if (p_nn->send_buf_pos < p_nn->send_buf_len) {
        sent = send_ciphertext(p_nn, len, flags);
    }

    return sent;
}

static int recv_hdr(netnacl_t *p_nn, int flags) {
    // Receives message header, handling partial reads and disconnects.
    while (p_nn->hdr_bytes_recvd < sizeof(hdr_t)) {
        ssize_t recvd = recv(p_nn->sock_fd, (uint8_t *)&p_nn->recv_hdr + p_nn->hdr_bytes_recvd,
                             sizeof(hdr_t) - p_nn->hdr_bytes_recvd, flags);

        if (-1 == recvd) {
            if ((EAGAIN == errno) || (EWOULDBLOCK == errno)) {
                return NN_WANT_READ;
            }
            LOG(ERR, "recv");
            return NN_ERR;
        }

        if (0 == recvd) {
            LOG(INF, "disconnect");
            return NN_DISCONNECT;
        }

        p_nn->hdr_bytes_recvd += (size_t)recvd;
    }

    p_nn->recv_hdr.len = ntohs(p_nn->recv_hdr.len); // Convert length to host order

    return NN_SUCCESS;
}

static int recv_ciphertext(netnacl_t *p_nn, int flags) {
    // Receives ciphertext for a message, handling partial reads and disconnects.
    while (p_nn->ct_bytes_recvd < p_nn->recv_hdr.len) {
        ssize_t recvd =
            recv(p_nn->sock_fd, p_nn->recv_ct + p_nn->ct_bytes_recvd, p_nn->recv_hdr.len - p_nn->ct_bytes_recvd, flags);

        if (-1 == recvd) {
            LOG(ERR, "recv");
            if ((EAGAIN == errno) || (EWOULDBLOCK == errno)) {
                return NN_WANT_READ;
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
    // Decrypts received ciphertext and prepares plaintext buffer.
    ASSERT_RET(NULL != p_nn);
    LOG(DBG, "decrypting %hu bytes of ciphertext", p_nn->recv_hdr.len);

    if (0 != crypto_box_open_afternm(p_nn->recv_pt, p_nn->recv_ct, p_nn->recv_hdr.len, p_nn->recv_hdr.nonce,
                                     p_nn->sym_key)) {
        LOG(WRN, "crypto_box_open_afternm failed");
        return NN_CRYPTO_ERR;
    }

    p_nn->recv_pt_len = p_nn->recv_hdr.len - crypto_box_ZEROBYTES;
    memmove(p_nn->recv_pt, p_nn->recv_pt + crypto_box_ZEROBYTES, p_nn->recv_pt_len); // Remove padding

    return NN_SUCCESS;
}

static ssize_t copy_plaintext_to_buffer(netnacl_t *p_nn, uint8_t *buf, size_t len) {
    // Copies decrypted plaintext to user buffer, resets state when done.
    ASSERT_RET(NULL != p_nn);
    ASSERT_RET(NULL != buf);

    ASSERT_RET(p_nn->recv_pt_len <= MAX_MESSAGE_LEN);
    size_t read_sz = MIN(p_nn->recv_pt_len - p_nn->recv_pt_pos, len);
    memcpy(buf, p_nn->recv_pt + p_nn->recv_pt_pos, read_sz);

    p_nn->recv_pt_pos += read_sz;
    ASSERT_RET(p_nn->recv_pt_pos <= p_nn->recv_pt_len);
    if (p_nn->recv_pt_pos == p_nn->recv_pt_len) {
        // Reset state for next message
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
    // Encrypts plaintext and prepares send buffer for transmission.
    assert(NULL != p_nn);
    assert(NULL != buf);

    hdr_t send_hdr = {0};
    uint8_t pt_buf[crypto_box_ZEROBYTES + MAX_MESSAGE_LEN] = {0};

    randombytes(send_hdr.nonce, crypto_box_NONCEBYTES); // Always use a fresh nonce
    size_t pt_len = MIN(len, MAX_MESSAGE_LEN);
    LOG(DBG, "encrypting %zu / %zu bytes of plaintext", pt_len, len);
    size_t padded_pt_len = pt_len + crypto_box_ZEROBYTES;
    send_hdr.len = (uint16_t)pt_len + crypto_box_ZEROBYTES;
    p_nn->send_buf_len = send_hdr.len + sizeof(hdr_t);

    memcpy(pt_buf + crypto_box_ZEROBYTES, buf, pt_len); // Pad plaintext as required by NaCl
    crypto_box_afternm(p_nn->send_buf + sizeof(hdr_t), pt_buf, padded_pt_len, send_hdr.nonce, p_nn->sym_key);

    send_hdr.len = htons(send_hdr.len); // Store length in network order
    memcpy(p_nn->send_buf, &send_hdr, sizeof(hdr_t));
}

static ssize_t send_ciphertext(netnacl_t *p_nn, size_t len, int flags) {
    // Sends ciphertext buffer, handling partial writes and resetting state.
    ASSERT_RET(NULL != p_nn);

    while (p_nn->send_buf_pos < p_nn->send_buf_len) {
        ssize_t sent =
            send(p_nn->sock_fd, p_nn->send_buf + p_nn->send_buf_pos, p_nn->send_buf_len - p_nn->send_buf_pos, flags);

        if (-1 == sent) {
            LOG(ERR, "send");
            if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
                return NN_WANT_WRITE;
            }
            return NN_ERR;
        }

        assert((UINT16_MAX - p_nn->send_buf_pos) >= sent);

        p_nn->send_buf_pos += (uint16_t)sent;
    }

    // Reset state after sending the message
    memset(p_nn->send_buf, 0, p_nn->send_buf_len);
    p_nn->send_buf_len = 0;
    p_nn->send_buf_pos = 0;
    LOG(DBG, "finished sending message, reset state");

    return (ssize_t)MIN(len, MAX_MESSAGE_LEN);
}
