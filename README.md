# nacl-proxy

A lightweight, event-driven TCP proxy server with optional inter-proxy encryption using [TweetNaCl](https://tweetnacl.cr.yp.to/). Supports both IPv4 and IPv6, DNS, and simultaneous server-to-client and client-to-server traffic. Deploys as a stripped static binary (~80K on most architectures) with minimal symbols and strings.

---

## Features

- Fully non-blocking, event-driven architecture
- Optional authenticated encryption between proxies
- POSIX.1-2008 Compliant
- Deploys as a small (~70kB) static binary
- Minimal dependencies (only TweetNaCl)

---

## Usage

```bash
./proxy [-io] <bind address> <bind port> <server address> <server port>
```

### Options:
    `-i`: Encrypt client-side communications.
    `-o`: Encrypt server-side communications.
    `<bind address>`: Numeric local IPv4/IPv6 address to which the proxy will bind.
    `<bind port>`: The TCP port on `<bind address>` to which the proxy will bind. Must be between 1 and 65535.
    `<server address>`: Numeric IPv4/IPv6 address or domain name of the server to which the proxy will connect upon receiving a client connection.
    `<server port>`: The TCP port on `<server address>` to which the proxy will attempt to connect. Must be between 1 and 65535.


### Example Usage

```bash
# Start a python3 http server in its own terminal window on 8000
python3 -m http.server -d /tmp

# Create a large temporary file to retrieve via proxy
fallocate -l 100M /tmp/testfile

# Start three proxies where the one in the middle encrypts both incoming and outgoing data (each in their own terminal)
user@dev:~$ ./proxy-linux-x86_64-musl-static-minsizerel -o ::1 8000 localhost 8000
user@dev:~$ ./proxy-linux-x86_64-musl-static-minsizerel -io ::1 7999 ::1 8000
user@dev:~$ ./proxy-linux-x86_64-musl-static-minsizerel -o ::1 7998 ::1 7999

# Download the file
user@dev:~$ wget http://localhost:7998/testfile

Connecting to [::1]:7997... connected.
HTTP request sent, awaiting response... 200 OK
Length: 104857600 (100M) [application/octet-stream]
Saving to: ‘testfile.2’

testfile.2                      100%[======================================================>] 100.00M  7.88MB/s    in 12s
```

### Usage Considerations
`nacl-proxy` uses `netnacl` to encrypt communications between nodes but cannot encrypt data between the client and first hop or server and last hop unless the endpoints also make use of `netnacl` for encyption.

### Known Issues
`nacl-proxy` does not currently forward partion shutdown semantics. For example, if the client or server close only the read end of their socket `nacl-proxy` will interpret this as a complete socket closure. This will be fixed in a future release.


## About This Repsitory

Linting, building, analysis, and testing are most easily done in `docker`. The script `tasks.py` offers lint/build/analyze/test shortcuts using the `invoke` python package.
The docker image includes all `apt` and `pip` packages needed as well as all of the `musl` cross-compilation toolchains.

To build the docker image and install invoke:

```bash
pip install invoke
inv docker --build
```

From the project root directory start the docker container CLI:
```bash
inv docker
```

From here, run `inv --list` to see available tasks and then `inv <task name> --help` to see task-specific help information. 

Examples:

```bash
# Run lizard, clang-format, yamllint, and cmake-format on the repository's contents 
inv lint

# List available build targets
inv build --list-targets

# Build a release version of the proxy for arm64
inv build --target linux-arm64-musl --release

# Run CodeChecker on a target that has already been built
inv analyze --target linux-arm64-musl --release

# Test a target that has already been built
inv test --target linux-arm64-musl --release
```

---

## CI/CD

GitHub Actions workflow is provided in `.github/workflows/pipeline.yaml` for linting, building, analyzing, testing, and packaging across supported targets.

---

## License

MIT License. See LICENSE.

---

## Author

Kevin McKenzie

---

## Contributing

Pull requests and issues are welcome! Please ensure `invoke lint`, `invoke build-all`,  `invoke analyze-all`, and `invoke test-all` run/pass without error before submitting code.

---

## Contact

For questions or support, open an issue on GitHub or contact kbmck [at] protonmail.com.
