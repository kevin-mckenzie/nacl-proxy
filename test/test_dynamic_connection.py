"""
Module: test_dynamic_connection

This module contains integration tests for verifying dynamic proxy connections
and bidirectional communication between a client and server through various proxy configurations.
It ensures that the server can initiate communication by sending a banner message,
and that data can be exchanged in both directions across single and multi-hop proxy setups,
including both encrypted and unencrypted file system-based proxies.
"""

import os
import socket
from contextlib import closing


import pytest

# pylint: disable=W0621,W0611

from fixtures import (
    Proxy,  # type: ignore
    proxy_configuration,
    single_proxy_unencrypted_fs,
    double_proxy_unencrypted_fs,
    triple_proxy_unencrypted_fs,
    double_proxy_encrypted_fs,
    triple_proxy_encrypted_fs,
    quad_proxy_encrypted_fs,
)


UPSTREAM_ADDR = ("127.0.0.1", 8000)
SMALL_TIMEOUT = 3
LARGE_TIMEOUT = 10

def _recv_exact(sock, n):
    buf = bytearray(n); view = memoryview(buf); got = 0
    while got < n:
        r = sock.recv_into(view[got:], n - got)
        assert r > 0, "peer closed early"
        got += r
    return bytes(buf)

def _connect_through_proxy(proxy):
    # Upstream listener on fixed port
    listen = socket.create_server(UPSTREAM_ADDR, backlog=16)
    listen.settimeout(SMALL_TIMEOUT)

    # Client connects to proxy entrypoint; proxy will connect upstream
    c = socket.create_connection((proxy.in_addr, proxy.in_port), timeout=SMALL_TIMEOUT)
    s, _ = listen.accept()
    listen.close()

    c.settimeout(LARGE_TIMEOUT)
    s.settimeout(LARGE_TIMEOUT)
    return c, s

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
def test_server_sends_first_banner_basic(proxy_configuration):
    """
    Test that the server can send data first and the client receives it through the proxy.
    Also verifies bidirectional communication through the proxy.
    """
    proxy = proxy_configuration[0]

    with closing(socket.create_server(UPSTREAM_ADDR, backlog=16)) as listen:
        listen.settimeout(SMALL_TIMEOUT)
        with closing(socket.create_connection((proxy.in_addr, proxy.in_port), timeout=SMALL_TIMEOUT)) as client:
            server, _ = listen.accept()
            with closing(server):
                client.settimeout(LARGE_TIMEOUT)
                server.settimeout(LARGE_TIMEOUT)

                # Server sends first
                banner = b"Hello, client!\n"
                server.sendall(banner)
                assert _recv_exact(client, len(banner)) == banner

                # Minimal bi-dir proof
                payload = b"PING"
                client.sendall(payload)
                assert _recv_exact(server, len(payload)) == payload
                server.sendall(payload)
                assert _recv_exact(client, len(payload)) == payload

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
@pytest.mark.parametrize("n", [1, 64, 1500, 65536])
def test_client_to_server_roundtrip_sizes(proxy_configuration, n):
    """
    Test roundtrip data integrity for various payload sizes sent from client to server and echoed back.
    Ensures the proxy correctly forwards data of different sizes.
    """
    proxy = proxy_configuration[0]
    client, server = _connect_through_proxy(proxy)
    with closing(client), closing(server):
        blob = os.urandom(n)
        client.sendall(blob)
        # server reads then echoes
        got = _recv_exact(server, n); assert got == blob
        server.sendall(got)
        back = _recv_exact(client, n); assert back == blob

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
@pytest.mark.parametrize("n", [3, 4096, 20000])
def test_server_push_sizes(proxy_configuration, n):
    """
    Test that the server can push data of various sizes to the client through the proxy.
    Verifies that the proxy correctly forwards server-to-client data.
    """
    proxy = proxy_configuration[0]
    client, server = _connect_through_proxy(proxy)
    with closing(client), closing(server):
        blob = os.urandom(n)
        server.sendall(blob)
        got = _recv_exact(client, n)
        assert got == blob

