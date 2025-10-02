# netnacl

A minimal, portable C library for secure, authenticated, encrypted messaging over sockets using [TweetNaCl](https://tweetnacl.cr.yp.to/). Designed for use in event-driven network applications, `netnacl` provides a simple API for key exchange, message encryption, and message authentication.

---

## Features

- **Authenticated encryption** using Curve25519, XSalsa20, and Poly1305 (via TweetNaCl)
- **Simple socket-based API** for secure send/receive
- **Automatic key exchange** and symmetric key derivation
- **Non-blocking I/O support**
- **Minimal dependencies** (only libc)
- **IPv4/IPv6 compatible**

---

## Getting Started

### Building

This library uses CMake for configuration. By default, it builds as a static library.

```sh
git clone https://github.com/kevin-mckenzie/netnacl.git
cd netnacl
mkdir -p build
cmake -S . -B build
cmake --build build
```

The resulting library (`libnetnacl.a`) and headers will be in the `build` directory.

#### Build Options

- `LINK_TYPE`: Set to `STATIC` (default) or `SHARED` for static/shared library.
- `COVERAGE=ON`: Enable coverage instrumentation.
- `ASAN=ON`: Enable AddressSanitizer.
- `CMAKE_BUILD_TYPE`: Use `Debug` for debug symbols, `MinSizeRel` for minimal size.

---

## API Overview

Include the main header:

```c
#include "netnacl.h"
```

### Types

- `netnacl_t`: Opaque context for encrypted communication.

### Main Functions

#### 1. Context Creation

```c
netnacl_t *netnacl_create(int sock_fd);
```
Allocates and initializes a context for a socket. Required for all further encrypted communication.

#### 2. Key Exchange

```c
int netnacl_wrap(netnacl_t *p_nn);
```
Performs key exchange and derives a shared symmetric key. Must be called before sending or receiving encrypted messages. If the underlying socket is non-blocking a send() would have blocked, NN_WANT_WRITE is returned. If the underlying socket is non-blocking and recv() would have blocked, NN_WANT_READ is returned.

#### 3. Receiving Messages

```c
ssize_t netnacl_recv(netnacl_t *p_nn, uint8_t *buf, size_t len, int flags);
```
Receives and decrypts a message from the socket. Handles partial reads, header parsing, ciphertext reception, decryption, and buffering. Returns up to `len` bytes of plaintext. If the underlying socket is non-blocking and recv() would have blocked, NN_WANT_READ is returned.

#### 4. Sending Messages

```c
ssize_t netnacl_send(netnacl_t *p_nn, const uint8_t *buf, size_t len, int flags);
```
Encrypts and sends a message over the socket. Handles buffering and partial writes. Resets internal state after a full message is sent. If the underlying socket is non-blocking a send() would have blocked, NN_WANT_WRITE is returned.

#### 5. Random Bytes

```c
void randombytes(uint8_t *buf, uint64_t sz);
```
Fills a buffer with cryptographically secure random bytes. Implementation required by TweetNaCl.

---

## Usage Example

```c
#include "netnacl.h"
#include <sys/socket.h>
#include <unistd.h>

int sock_fd = /* ...connected socket... */;
netnacl_t *ctx = netnacl_create(sock_fd);

// Perform key exchange
while (1) {
    int status = netnacl_wrap(ctx);
    if (status == NN_SUCCESS) break;
    // Handle NN_WANT_READ, NN_WANT_WRITE, NN_ERR as needed
}

// Send encrypted message
const char *msg = "Hello, secure world!";
ssize_t sent = netnacl_send(ctx, (const uint8_t *)msg, strlen(msg), 0);

// Receive encrypted message
uint8_t buf[4096];
ssize_t recvd = netnacl_recv(ctx, buf, sizeof(buf), 0);

// Clean up
close(sock_fd);
free(ctx);
```

---

## Return Codes

- `NN_SUCCESS`: Operation completed successfully
- `NN_WANT_READ`: More data needed (non-blocking)
- `NN_WANT_WRITE`: Socket not ready for writing (non-blocking)
- `NN_ERR`: Generic error
- `NN_CRYPTO_ERR`: Cryptographic error
- `NN_DISCONNECT`: Remote disconnected

---

## License

GPL 3 License. See LICENSE.

---

## Author

Kevin McKenzie

---

## Contributing

Pull requests and issues are welcome! Please run static analysis and tests before submitting code.

---

## References

- [TweetNaCl](https://tweetnacl.cr.yp.to/)
- [NaCl Documentation](https://nacl.cr.yp.to/)

---

## Contact

For questions or support, open an issue on GitHub.
