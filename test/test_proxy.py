# test_transport_passthrough.py
import concurrent.futures as cf
import hashlib
import os
import socket
import struct
import time
from contextlib import closing
from pathlib import Path
from typing import Optional, Tuple

import pytest
import requests

# pylint: disable=W0621,W0611

from fixtures import (
    HTTPServer,
    Proxy,  # type: ignore
    python_http_server_ss,
    single_proxy_unencrypted_fs,
    double_proxy_unencrypted_fs,
    triple_proxy_unencrypted_fs,
    double_proxy_encrypted_fs,
    triple_proxy_encrypted_fs,
    quad_proxy_encrypted_fs,
)

# ---------------- config ----------------

DOCROOT = Path("/tmp")
SMALL_TIMEOUT = 3
LARGE_TIMEOUT = 10


@pytest.fixture
def proxy_configuration(request):
    # request.param is a fixture name; resolve it to the actual proxy chain object
    return request.getfixturevalue(request.param)


# ============================================================
#                 PROTOCOL-AGNOSTIC BASELINE
# ============================================================


@pytest.mark.parametrize(
    "proxy_configuration",
    [
        "single_proxy_unencrypted_fs",
        "double_proxy_unencrypted_fs",
        "triple_proxy_unencrypted_fs",
        "double_proxy_encrypted_fs",
        "triple_proxy_encrypted_fs",
        "quad_proxy_encrypted_fs",
    ],
    indirect=True,
)
def test_root_bytes_match(proxy_configuration, python_http_server_ss):
    """
    Minimal parity: fetch '/' directly vs through proxy and compare bytes.
    No HTTP semantics asserted other than a successful fetch.
    """
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = python_http_server_ss

    direct = requests.get(f"http://{server.addr}:{server.port}", timeout=SMALL_TIMEOUT)
    via = requests.get(f"http://{proxy.in_addr}:{proxy.in_port}", timeout=SMALL_TIMEOUT)

    assert direct.status_code // 100 == 2
    assert via.status_code // 100 == 2
    assert direct.content == via.content


@pytest.mark.parametrize(
    "proxy_configuration",
    [
        "single_proxy_unencrypted_fs",
        "double_proxy_unencrypted_fs",
        "triple_proxy_unencrypted_fs",
        "double_proxy_encrypted_fs",
        "triple_proxy_encrypted_fs",
        "quad_proxy_encrypted_fs",
    ],
    indirect=True,
)
def test_large_transfer_integrity(proxy_configuration, python_http_server_ss, tmp_path):
    """
    ~8MB download, streamed: hash must match direct vs via proxy.
    """
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = python_http_server_ss

    # Make a large file under /tmp for the HTTP file server to serve.
    big = tmp_path / "large.bin"
    with big.open("wb") as f:
        remaining = 8 * 1024 * 1024
        block = os.urandom(8192)
        while remaining > 0:
            n = min(remaining, len(block))
            f.write(block[:n])
            remaining -= n

    rel = "/" + str(big.relative_to(DOCROOT)).replace("\\", "/")

    def sha256_stream(url: str) -> str:
        h = hashlib.sha256()
        with requests.get(url, stream=True, timeout=LARGE_TIMEOUT) as r:
            r.raise_for_status()
            for chunk in r.iter_content(64 * 1024):
                if chunk:
                    h.update(chunk)
        return h.hexdigest()

    d = sha256_stream(f"http://{server.addr}:{server.port}{rel}")
    p = sha256_stream(f"http://{proxy.in_addr}:{proxy.in_port}{rel}")
    assert d == p


@pytest.mark.parametrize(
    "proxy_configuration",
    [
        "single_proxy_unencrypted_fs",
        "double_proxy_unencrypted_fs",
        "triple_proxy_unencrypted_fs",
        "double_proxy_encrypted_fs",
        "triple_proxy_encrypted_fs",
        "quad_proxy_encrypted_fs",
    ],
    indirect=True,
)
def test_concurrent_clients(proxy_configuration, python_http_server_ss, tmp_path):
    """
    Many clients concurrently pulling the same bytes through the proxy.
    """
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = python_http_server_ss

    payload = tmp_path / "payload.bin"
    payload.write_bytes(os.urandom(2 * 1024 * 1024))
    rel = "/" + str(payload.relative_to(DOCROOT)).replace("\\", "/")

    direct_url = f"http://{server.addr}:{server.port}{rel}"
    proxy_url = f"http://{proxy.in_addr}:{proxy.in_port}{rel}"

    ref = hashlib.sha256(
        requests.get(direct_url, timeout=LARGE_TIMEOUT).content
    ).hexdigest()

    def fetch_hash():
        return hashlib.sha256(
            requests.get(proxy_url, timeout=LARGE_TIMEOUT).content
        ).hexdigest()

    N = 12
    with cf.ThreadPoolExecutor(max_workers=N) as ex:
        results = list(ex.map(lambda _: fetch_hash(), range(N)))

    assert all(h == ref for h in results)


@pytest.mark.parametrize(
    "proxy_configuration",
    [
        "single_proxy_unencrypted_fs",
        "double_proxy_unencrypted_fs",
        "triple_proxy_unencrypted_fs",
        "double_proxy_encrypted_fs",
        "triple_proxy_encrypted_fs",
        "quad_proxy_encrypted_fs",
    ],
    indirect=True,
)
def test_client_abort_then_next_ok(
    proxy_configuration, python_http_server_ss, tmp_path
):
    """
    Client closes early mid-transfer through proxy; a subsequent fresh transfer still works.
    """
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = python_http_server_ss

    big = tmp_path / "abort.bin"
    big.write_bytes(os.urandom(4 * 1024 * 1024))
    rel = "/" + str(big.relative_to(DOCROOT)).replace("\\", "/")

    url = f"http://{proxy.in_addr}:{proxy.in_port}{rel}"
    r = requests.get(url, stream=True, timeout=LARGE_TIMEOUT)
    next(r.iter_content(64 * 1024))
    r.close()

    h1 = hashlib.sha256(requests.get(url, timeout=LARGE_TIMEOUT).content).hexdigest()
    h2 = hashlib.sha256(requests.get(url, timeout=LARGE_TIMEOUT).content).hexdigest()
    assert h1 == h2


# ============================================================
#                 HOSTNAME & IPv6 REACHABILITY
# ============================================================


@pytest.mark.parametrize(
    "proxy_configuration",
    [
        "single_proxy_unencrypted_fs",
        "double_proxy_unencrypted_fs",
        "triple_proxy_unencrypted_fs",
        "double_proxy_encrypted_fs",
        "triple_proxy_encrypted_fs",
        "quad_proxy_encrypted_fs",
    ],
    indirect=True,
)
def test_hostname_resolution_to_proxy(proxy_configuration, python_http_server_ss):
    """
    Connect to the proxy via 'localhost' (host->addr resolution on the client side),
    then fetch bytes; compare with direct path. This asserts the proxy does not
    depend on a specific literal IP at the client connect step.
    """
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = python_http_server_ss

    direct = requests.get(f"http://{server.addr}:{server.port}", timeout=SMALL_TIMEOUT)
    via = requests.get(f"http://localhost:{proxy.in_port}", timeout=SMALL_TIMEOUT)

    assert direct.status_code // 100 == 2
    assert via.status_code // 100 == 2
    assert direct.content == via.content


@pytest.mark.parametrize(
    "proxy_configuration",
    [
        "single_proxy_unencrypted_fs",
        "double_proxy_unencrypted_fs",
        "triple_proxy_unencrypted_fs",
        "double_proxy_encrypted_fs",
        "triple_proxy_encrypted_fs",
        "quad_proxy_encrypted_fs",
    ],
    indirect=True,
)
def test_ipv6_client_to_proxy_when_available(
    proxy_configuration, python_http_server_ss
):
    """
    If the proxy is listening on ::1 as well (dual-stack or v6), verify reachability
    by connecting via IPv6 literal. If not, skip with a helpful message.
    """
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = python_http_server_ss

    try:
        url_v6 = f"http://[::1]:{proxy.in_port}"
        via = requests.get(url_v6, timeout=SMALL_TIMEOUT)
    except Exception as e:  # noqa: BLE001
        pytest.skip(f"IPv6 localhost connect failed or proxy not bound on ::1: {e!r}")

    direct = requests.get(f"http://{server.addr}:{server.port}", timeout=SMALL_TIMEOUT)
    assert direct.status_code // 100 == 2
    assert via.status_code // 100 == 2
    assert direct.content == via.content


# ============================================================
#                 SERVER-SENDS-FIRST (BANNER) CASES
# ============================================================


@pytest.fixture
def single_proxy_to_target():
    """
    OPTIONAL: start a single proxy instance forwarding to (host, port).
    Return a callable (host, port) -> (listen_addr, listen_port).

    Wire this to your Proxy class if it supports spawning a one-off proxy
    to an arbitrary upstream. If not available, tests using this fixture
    will skip gracefully.

    Example wiring you might implement here (pseudo):
        def _start(dst_host, dst_port):
            px = Proxy.spawn(dst_host, dst_port, enc=False)
            px.start()
            return px.in_addr, px.in_port
        return _start
    """

    def _unavailable(*_args, **_kwargs):
        pytest.skip("single_proxy_to_target fixture not wired to your Proxy class.")

    return _unavailable


@pytest.mark.parametrize("use_ipv6", [False, True])
def test_server_sends_first_banner(single_proxy_to_target, tcp_banner_server, use_ipv6):
    """
    The upstream sends a banner immediately (no client bytes first). Verify the proxy
    faithfully forwards the banner and then bidirectional echo works.
    """
    host, port = tcp_banner_server["v6" if use_ipv6 else "v4"]
    try:
        in_addr, in_port = single_proxy_to_target(host, port)
    except pytest.skip.Exception:  # propagate skip from the fixture
        raise

    # 1) Read the banner (server -> client) through the proxy
    with closing(
        socket.create_connection((in_addr, in_port), timeout=SMALL_TIMEOUT)
    ) as s:
        banner = s.recv(128)
        assert banner.startswith(b"HELLO ")
        # 2) Then prove bi-dir still works
        s.sendall(b"PING")
        got = s.recv(4)
        assert got == b"PING"


@pytest.mark.parametrize("use_ipv6", [False, True])
def test_transport_integrity_over_echo(
    single_proxy_to_target, tcp_echo_server, use_ipv6
):
    """
    Byte-for-byte echo integrity for arbitrary payload over TCP, both IPv4 and IPv6,
    proving no transformation by the proxy.
    """
    host, port = tcp_echo_server["v6" if use_ipv6 else "v4"]
    try:
        in_addr, in_port = single_proxy_to_target(host, port)
    except pytest.skip.Exception:
        raise

    msg = os.urandom(256 * 1024)  # 256 KiB arbitrary bytes
    with closing(
        socket.create_connection((in_addr, in_port), timeout=LARGE_TIMEOUT)
    ) as s:
        s.sendall(msg)
        view = memoryview(bytearray(len(msg)))
        n = 0
        while n < len(msg):
            r = s.recv_into(view[n:], 64 * 1024)
            assert r > 0
            n += r
        assert bytes(view) == msg


# ============================================================
#              Tiny TCP fixtures (protocol-agnostic)
# ============================================================


@pytest.fixture(scope="function")
def tcp_echo_server():
    """
    Start a dual-stack echo server (v4 on 127.0.0.1, v6 on ::1) for integrity checks.
    Returns {'v4': ('127.0.0.1', port), 'v6': ('::1', port6)}
    """
    servers = {}

    def _start(family, addr):
        s = socket.socket(family, socket.SOCK_STREAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((addr, 0))
        s.listen(16)
        port = s.getsockname()[1]

        def _loop():
            try:
                while True:
                    try:
                        conn, _ = s.accept()
                    except OSError:
                        break
                    with conn:
                        while True:
                            data = conn.recv(64 * 1024)
                            if not data:
                                break
                            conn.sendall(data)
            finally:
                s.close()

        import threading

        t = threading.Thread(target=_loop, daemon=True)
        t.start()
        return port

    # v4
    servers["v4"] = ("127.0.0.1", _start(socket.AF_INET, "127.0.0.1"))
    # v6 (may fail on systems without ::1); skip later if connect fails
    try:
        servers["v6"] = ("::1", _start(socket.AF_INET6, "::1"))
    except OSError:
        servers["v6"] = ("::1", None)

    return servers


@pytest.fixture(scope="function")
def tcp_banner_server():
    """
    Start a small server that sends first (banner), then echoes thereafter.
    Returns {'v4': ('127.0.0.1', port), 'v6': ('::1', port6)}
    """
    servers = {}

    def _start(family, addr):
        s = socket.socket(family, socket.SOCK_STREAM)
        s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        s.bind((addr, 0))
        s.listen(16)
        port = s.getsockname()[1]

        def _loop():
            try:
                while True:
                    try:
                        conn, _ = s.accept()
                    except OSError:
                        break
                    with conn:
                        # Send banner immediately
                        conn.sendall(b"HELLO BANNER\r\n")
                        # Then echo whatever arrives
                        while True:
                            data = conn.recv(64 * 1024)
                            if not data:
                                break
                            conn.sendall(data)
            finally:
                s.close()

        import threading

        t = threading.Thread(target=_loop, daemon=True)
        t.start()
        return port

    servers["v4"] = ("127.0.0.1", _start(socket.AF_INET, "127.0.0.1"))
    try:
        servers["v6"] = ("::1", _start(socket.AF_INET6, "::1"))
    except OSError:
        servers["v6"] = ("::1", None)

    return servers
