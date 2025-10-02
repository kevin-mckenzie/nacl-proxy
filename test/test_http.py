"""
Unit tests for proxy functionality to an HTTP server.

This module contains tests that verify the integrity, concurrency, and correctness of HTTP traffic
forwarded through various proxy configurations. It ensures that payloads are unaltered, large file
transfers are reliable, concurrent clients are handled properly, aborted connections do not affect
subsequent requests, hostname resolution works as expected, and IPv6 traffic is supported.
"""

import concurrent.futures as cf
import hashlib
import os
from pathlib import Path

import pytest
import requests

# pylint: disable=W0621,W0611

from fixtures import (
    HTTPServer,
    Proxy,  # type: ignore
    proxy_configuration,
    python_http_server_ms,
    single_proxy_ipv6_fs,
    single_proxy_unencrypted_fs,
    double_proxy_unencrypted_fs,
    triple_proxy_unencrypted_fs,
    double_proxy_encrypted_fs,
    triple_proxy_encrypted_fs,
    quad_proxy_encrypted_fs,
    python_http_server_ipv6_fs,
)

DOCROOT = Path("/tmp")
SMALL_TIMEOUT = 3
LARGE_TIMEOUT = 10


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
def test_root_bytes_match(proxy_configuration, python_http_server_ms):
    """
    Test that fetching '/' directly from the server and through the proxy yields identical content.
    Ensures the proxy does not alter HTTP payloads.
    """
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = python_http_server_ms

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
def test_large_transfer_integrity(proxy_configuration, python_http_server_ms, tmp_path):
    """
    Test integrity of large file transfers through the proxy.
    Compares SHA256 hashes of direct and proxied downloads to ensure no corruption.
    """
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = python_http_server_ms

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
def test_concurrent_clients(proxy_configuration, python_http_server_ms, tmp_path):
    """
    Test concurrent client downloads through the proxy.
    Ensures all clients receive identical data and the proxy handles concurrency correctly.
    """
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = python_http_server_ms

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

    num_threads = 12
    with cf.ThreadPoolExecutor(max_workers=num_threads) as ex:
        results = list(ex.map(lambda _: fetch_hash(), range(num_threads)))

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
    proxy_configuration,
    python_http_server_ms,
    tmp_path,  # pylint: disable=W0613
):
    """
    Test that aborting a client download does not affect subsequent downloads through the proxy.
    Ensures proxy recovers and serves correct data to new clients.
    """
    proxy: Proxy = proxy_configuration[0]

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
def test_hostname_resolution_to_proxy(proxy_configuration, python_http_server_ms):
    """
    Test that the proxy correctly handles hostname resolution for incoming connections.
    Verifies that requests to 'localhost' are properly forwarded.
    """
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = python_http_server_ms

    direct = requests.get(f"http://{server.addr}:{server.port}", timeout=SMALL_TIMEOUT)
    via = requests.get(f"http://localhost:{proxy.in_port}", timeout=SMALL_TIMEOUT)

    assert direct.status_code // 100 == 2
    assert via.status_code // 100 == 2
    assert direct.content == via.content


def test_ipv6(single_proxy_ipv6_fs, python_http_server_ipv6_fs):
    """
    Test proxying HTTP traffic over IPv6.
    Ensures the proxy can forward requests and responses using IPv6 addresses.
    """
    proxy: Proxy = single_proxy_ipv6_fs[0]
    server: HTTPServer = python_http_server_ipv6_fs

    url_v6 = f"http://[::1]:{proxy.in_port}"
    via = requests.get(url_v6, timeout=SMALL_TIMEOUT)

    direct = requests.get(
        f"http://[{server.addr}]:{server.port}", timeout=SMALL_TIMEOUT
    )
    assert direct.status_code // 100 == 2
    assert via.status_code // 100 == 2
    assert direct.content == via.content
