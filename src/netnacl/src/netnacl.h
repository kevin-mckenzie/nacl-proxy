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
    NN_MAX_MESSAGE_LEN = 4096,
};

typedef struct netnacl_t netnacl_t;

/**
 * @brief Allocates and initializes a netnacl_t context for a socket.
 *
 * Sets the socket file descriptor and zeroes all fields. The returned context
 * is required for all further encrypted communication using this API.
 *
 * @param sock_fd Socket file descriptor to use for communication.
 * @return Pointer to allocated netnacl_t, or NULL on allocation failure.
 */
netnacl_t *netnacl_create(int sock_fd);

/**
 * @brief Performs key exchange and derives a shared symmetric key.
 *
 * Generates a keypair if needed, sends the public key, receives the peer's public key,
 * and derives the shared symmetric key using crypto_box_beforenm().
 * Handles partial sends/receives and non-blocking IO.
 *
 * Call this before sending or receiving encrypted messages.
 *
 * @param p_nn Pointer to netnacl_t context.
 * @return NN_SUCCESS on success,
 *         NN_WANT_READ/NN_WANT_WRITE if non-blocking IO required,
 *         NN_ERR on error.
 */
int netnacl_wrap(netnacl_t *p_nn);

/**
 * @brief Receives and decrypts a message from the socket.
 *
 * Handles partial reads, header parsing, ciphertext reception, decryption,
 * and buffering of plaintext. Returns up to @p len bytes of plaintext.
 * Resets internal state after a full message is read.
 *
 * Call netnacl_wrap() before using this function.
 *
 * @param p_nn Pointer to netnacl_t context.
 * @param p_buf Buffer to store received plaintext.
 * @param len Maximum number of bytes to write to @p p_buf.
 * @param flags Flags for recv() (e.g., MSG_DONTWAIT).
 * @return Number of bytes read,
 *         0 on disconnect,
 *         NN_WANT_READ if more data needed,
 *         NN_ERR or NN_CRYPTO_ERR on error.
 */
ssize_t netnacl_recv(netnacl_t *p_nn, uint8_t *buf, size_t len, int flags);

/**
 * @brief Encrypts and sends a message over the socket.
 *
 * Encrypts the plaintext, prepares the header, and sends the ciphertext.
 * Handles partial writes and buffering. Resets internal state after a full message is sent.
 *
 * Call netnacl_wrap() before using this function.
 *
 * @param p_nn Pointer to netnacl_t context.
 * @param p_buf Buffer containing plaintext to send.
 * @param len Number of bytes to send from @p p_buf.
 * @param flags Flags for send() (e.g., MSG_DONTWAIT).
 * @return Number of bytes sent,
 *         NN_WANT_WRITE if more data needs to be sent,
 *         NN_ERR on error.
 */
ssize_t netnacl_send(netnacl_t *p_nn, const uint8_t *buf, size_t len, int flags);

#endif
