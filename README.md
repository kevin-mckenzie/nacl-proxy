# n-proxy

A lightweight, event-driven TCP proxy server with optional end-to-end encryption using [TweetNaCl](https://tweetnacl.cr.yp.to/). Supports both IPv4 and IPv6, and can encrypt traffic on either or both sides of the proxy.

---

## Features

- Transparent TCP proxying between client and server
- Optional encryption for incoming and/or outgoing connections
- Non-blocking, event-driven architecture
- Minimal dependencies (TweetNaCl included)
- Easy to build and test on Linux

---

## Building

### Prerequisites

- **Linux** (tested on Ubuntu, Alpine, etc.)
- **CMake** (>= 3.15)
- **Ninja** (recommended)
- **Python 3** (for testing)
- **clang-format**, **lizard**, **cmake-format** (for linting/analysis)
- **invoke** (Python task runner)

### Build Targets

The project supports multiple build targets for different environments and analysis tools. These are defined in tasks.py:

- `local`: Standard build for your host system (static linking)
- `asan`: AddressSanitizer build for runtime memory checks (dynamic linking)
- `valgrind`: Build for running under Valgrind (dynamic linking)
- `linux-x86_64-musl`: Cross-compile for musl libc (static linking)

You can list all available targets with:

```sh
invoke build --list-targets
```

### Building with CMake and Ninja (manual)

```sh
mkdir -p build
cmake -S . -B build -G Ninja
cmake --build build --target install
```

The compiled binary will be in bin.

### Building with Invoke

Invoke automates builds, linting, and testing. Example usage:

```sh
# Build for your local system (static linking, debug)
invoke build --target local

# Build for your local system (release/minimal size)
invoke build --target local --release

# Build with AddressSanitizer (dynamic linking)
invoke build --target asan

# Build for Valgrind (dynamic linking)
invoke build --target valgrind

# Cross-compile for musl (static linking)
invoke build --target linux-x86_64-musl
```

---

## Linting and Formatting

Linting and formatting are automated via `invoke`:

```sh
invoke format      # Format code with clang-format, ruff, cmake-format
invoke lint        # Run lizard complexity, clang-format check, cmake-format check
```

---

## Static Analysis

Static analysis is performed using CodeChecker and other tools:

```sh
invoke analyze --target local
invoke analyze --target asan
invoke analyze --target linux-x86_64-musl
```

Reports are generated in `dist/reports/analysis/`.

---

## Testing

Unit and integration tests are provided. To run all tests:

```sh
invoke test
```

Or, to test a specific build target:

```sh
invoke test --target asan
invoke test --target valgrind
invoke test --target linux-x86_64-musl
```

You can also run tests manually with pytest:

```sh
pytest . -vv
```

---

## Usage

### Command-Line

```sh
./dist/bin/proxy-local-static-debug [-io] <bind address> <bind port> <server address> <server port>
```

#### Options

- `-i` : Encrypt incoming client connections
- `-o` : Encrypt outgoing server connections
- `-io` : Encrypt both directions
- `-h` : Show help

#### Example

Proxy traffic from local port 8080 to remote server 10.0.0.2:9000, encrypting both directions:

```sh
./dist/bin/proxy-local-static-debug -io 0.0.0.0 8080 10.0.0.2 9000
```

Proxy traffic from IPv6 localhost to a remote IPv6 server, encrypting only outgoing:

```sh
./dist/bin/proxy-local-static-debug -o ::1 8080 2001:db8::1 9000
```

---

## Configuration

All configuration is via command-line arguments. See above for details.

---

## Advanced

- **Cross-compilation:** See tasks.py for toolchain information all toolchains are Bootlin Linux toolchains.
- **Docker:** Build and run in Docker using `invoke docker`.
- **Packaging:** Create distributable archives with:
  ```sh
  invoke package
  ```

---

## CI/CD

GitHub Actions workflow is provided in pipeline.yaml for linting, building, analyzing, testing, and packaging across multiple targets.

---

## License

MIT License. See LICENSE.

---

## Author

Kevin McKenzie

---

## Troubleshooting

- If you see permission errors on ports <1024, run as root or use higher ports.
- For IPv6, ensure your system supports IPv6 and the addresses are valid.
- For debugging, use the `Debug` build type and check logs.

---

## Contributing

Pull requests and issues are welcome! Please ensure `invoke lint`, `invoke build-all`,  `invoke analyze-all`, and `invoke test-all` run/pass without error before submitting code.

---

## Contact

For questions or support, open an issue on GitHub.
