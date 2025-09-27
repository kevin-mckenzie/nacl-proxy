import concurrent.futures as cf
import hashlib
import os
import random
import string
import uuid
from pathlib import Path
from typing import Optional, Tuple

import pytest
import requests

# pylint: disable=W0621,W0611

from fixtures import (
    HTTPServer,
    Proxy,
    single_proxy_unencrypted_fs,
    double_proxy_unencrypted_fs,
    triple_proxy_unencrypted_fs,
    double_proxy_encrypted_fs,
    triple_proxy_encrypted_fs,
    quad_proxy_encrypted_fs,
)


@pytest.fixture
def proxy_configuration(request):
    # request.param is a fixture name; resolve it to the actual proxy chain object
    return request.getfixturevalue(request.param)


# ---------------- helpers (assume /tmp docroot) ----------------

DOCROOT = Path("/tmp")


def _unique_dir(prefix: str) -> Path:
    p = DOCROOT / f"{prefix}-{uuid.uuid4().hex[:10]}"
    p.mkdir(parents=True, exist_ok=True)
    return p


def _write_file(
    dirpath: Path, name: str, *, size: int = 0, binary: bool = False
) -> Path:
    """Create a file under /tmp/<dirpath>/<name> with optional size."""
    p = dirpath / name
    p.parent.mkdir(parents=True, exist_ok=True)
    if size > 0:
        if binary:
            block = os.urandom(8192)
            remaining = size
            with p.open("wb") as f:
                while remaining > 0:
                    n = min(remaining, len(block))
                    f.write(block[:n])
                    remaining -= n
        else:
            with p.open("w", encoding="utf-8") as f:
                while f.tell() < size:
                    f.write(
                        "".join(random.choice(string.ascii_letters) for _ in range(120))
                    )
                    f.write("\n")
    else:
        mode = "wb" if binary else "w"
        with p.open(mode) as f:
            if not binary:
                f.write("hello world\n")
    return p


def _base_urls(proxy: Proxy, server: HTTPServer) -> Tuple[str, str]:
    """Return (direct_base, proxy_base)."""
    return (
        f"http://{server.addr}:{server.port}",
        f"http://{proxy.in_addr}:{proxy.in_port}",
    )


def _sha256_stream(
    url: str, *, session: Optional[requests.Session] = None, timeout=10
) -> str:
    s = session or requests
    h = hashlib.sha256()
    with s.get(url, stream=True, timeout=timeout) as r:
        r.raise_for_status()
        for chunk in r.iter_content(chunk_size=64 * 1024):
            if chunk:
                h.update(chunk)
    return h.hexdigest()


# ---------------- baseline parity ----------------


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
def test_get_simple(proxy_configuration):
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = proxy_configuration[-1]

    print("going direct")
    direct = requests.get(f"http://{server.addr}:{server.port}", timeout=3)
    print("going via proxy")
    via_proxy = requests.get(f"http://{proxy.in_addr}:{proxy.in_port}", timeout=3)

    assert direct.status_code == 200
    assert via_proxy.status_code == 200
    assert direct.content == via_proxy.content
    assert direct.headers.get("Content-Type") == via_proxy.headers.get("Content-Type")


# ---------------- HTTP method parity & errors ----------------


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
def test_head_matches_get_length(proxy_configuration):
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = proxy_configuration[-1]


    direct_base, proxy_base = _base_urls(proxy, server)

    h_direct = requests.head(direct_base, timeout=3)
    h_proxy = requests.head(proxy_base, timeout=3)
    assert h_direct.status_code == h_proxy.status_code == 200
    assert h_direct.headers.get("Content-Length") == h_proxy.headers.get(
        "Content-Length"
    )
    assert not h_direct.content and not h_proxy.content


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
def test_404_passthrough(proxy_configuration):
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = proxy_configuration[-1]


    direct_base, proxy_base = _base_urls(proxy, server)

    missing = "/__missing__-" + uuid.uuid4().hex
    d = requests.get(direct_base + missing, timeout=3)
    p = requests.get(proxy_base + missing, timeout=3)

    assert d.status_code == p.status_code == 404
    assert abs(len(d.content) - len(p.content)) < 256  # allow minor diffs


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
def test_unsupported_methods_passthrough(proxy_configuration):
    """Python static servers usually only implement GET/HEAD—ensure status class parity."""
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = proxy_configuration[-1]


    direct_base, proxy_base = _base_urls(proxy, server)

    for method in ("POST", "PUT", "DELETE", "PATCH"):
        d = requests.request(method, direct_base, data=b"x=1", timeout=3)
        p = requests.request(method, proxy_base, data=b"x=1", timeout=3)
        assert d.status_code == p.status_code
        assert (d.status_code // 100) == (p.status_code // 100)


# ---------------- static files: content-type & integrity ----------------


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
def test_content_type_preserved_for_static_files(
    proxy_configuration
):
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = proxy_configuration[-1]



    base = _unique_dir("mimes")
    txt = _write_file(base, "test.txt")
    png = _write_file(base, "test.png", size=2048, binary=True)

    for rel in (txt, png):
        rel_url = "/" + str(rel.relative_to(DOCROOT)).replace("\\", "/")
        d = requests.get(f"http://{server.addr}:{server.port}{rel_url}", timeout=3)
        p = requests.get(f"http://{proxy.in_addr}:{proxy.in_port}{rel_url}", timeout=3)
        assert d.status_code == p.status_code == 200
        assert d.headers.get("Content-Type") == p.headers.get("Content-Type")
        assert d.content == p.content


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
def test_large_file_transfer_integrity(proxy_configuration):
    """~8MB streamed download: byte-for-byte identical via proxy."""
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = proxy_configuration[-1]



    base = _unique_dir("big")
    big = _write_file(base, "large.bin", size=8 * 1024 * 1024, binary=True)
    rel = "/" + str(big.relative_to(DOCROOT)).replace("\\", "/")

    direct_url = f"http://{server.addr}:{server.port}{rel}"
    proxy_url = f"http://{proxy.in_addr}:{proxy.in_port}{rel}"

    h_direct = _sha256_stream(direct_url)
    h_proxy = _sha256_stream(proxy_url)
    assert h_direct == h_proxy


# ---------------- ranges & directory listings ----------------


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
def test_range_requests_when_supported(proxy_configuration):
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = proxy_configuration[-1]



    base = _unique_dir("ranges")
    sample = _write_file(base, "sample.bin", size=512_000, binary=True)
    rel = "/" + str(sample.relative_to(DOCROOT)).replace("\\", "/")

    direct_url = f"http://{server.addr}:{server.port}{rel}"
    proxy_url = f"http://{proxy.in_addr}:{proxy.in_port}{rel}"

    head = requests.head(direct_url, timeout=3)
    if head.headers.get("Accept-Ranges", "").lower() != "bytes":
        pytest.skip("Server does not advertise byte-range support")

    headers = {"Range": "bytes=100-1099"}
    d = requests.get(direct_url, headers=headers, timeout=3)
    p = requests.get(proxy_url, headers=headers, timeout=3)
    assert d.status_code == p.status_code == 206
    assert d.headers.get("Content-Range") == p.headers.get("Content-Range")
    assert d.content == p.content

    full = requests.get(direct_url, timeout=3).content
    assert d.content == full[100:1100]


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
def test_directory_listing_parity(proxy_configuration):
    """Ensure directory listings keep filenames intact via proxy."""
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = proxy_configuration[-1]



    subdir = _unique_dir("listing")
    (subdir / "a.txt").write_text("A\n", encoding="utf-8")
    (subdir / "b.txt").write_text("B\n", encoding="utf-8")

    rel = "/" + str(subdir.relative_to(DOCROOT)).replace("\\", "/") + "/"
    d = requests.get(f"http://{server.addr}:{server.port}{rel}", timeout=3)
    p = requests.get(f"http://{proxy.in_addr}:{proxy.in_port}{rel}", timeout=3)

    assert d.status_code == p.status_code == 200
    for name in ("a.txt", "b.txt"):
        assert name.encode() in d.content
        assert name.encode() in p.content
    assert abs(len(d.content) - len(p.content)) < 2048


# ---------------- concurrency & persistence ----------------


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
def test_concurrent_clients_download_same_file(
    proxy_configuration
):
    """Many clients concurrently downloading the same file through the proxy."""
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = proxy_configuration[-1]

    base = _unique_dir("concurrency")
    payload = _write_file(base, "payload.dat", size=2 * 1024 * 1024, binary=True)
    rel = "/" + str(payload.relative_to(DOCROOT)).replace("\\", "/")

    direct_url = f"http://{server.addr}:{server.port}{rel}"
    proxy_url = f"http://{proxy.in_addr}:{proxy.in_port}{rel}"

    ref = _sha256_stream(direct_url)

    N = 12
    with cf.ThreadPoolExecutor(max_workers=N) as ex:
        futs = [ex.submit(_sha256_stream, proxy_url) for _ in range(N)]
        results = [f.result() for f in futs]

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
def test_keepalive_many_small_requests(proxy_configuration):
    """One TCP session through the proxy, many sequential GETs (keep-alive)."""
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = proxy_configuration[-1]



    base = _unique_dir("small")
    files = [_write_file(base, f"f{i}.txt") for i in range(20)]

    with requests.Session() as s:
        for pth in files:
            rel = "/" + str(pth.relative_to(DOCROOT)).replace("\\", "/")
            d = s.get(f"http://{server.addr}:{server.port}{rel}", timeout=3)
            p = s.get(f"http://{proxy.in_addr}:{proxy.in_port}{rel}", timeout=3)
            assert d.status_code == p.status_code == 200
            assert d.content == p.content


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
def test_client_abort_does_not_poison_next_request(
    proxy_configuration
):
    """
    Simulate a client that aborts mid-transfer (close early), then perform a fresh request.
    Proxy should properly clean up and serve subsequent requests.
    """
    proxy: Proxy = proxy_configuration[0]


    base = _unique_dir("abort")
    big = _write_file(base, "abort.bin", size=4 * 1024 * 1024, binary=True)
    rel = "/" + str(big.relative_to(DOCROOT)).replace("\\", "/")
    proxy_url = f"http://{proxy.in_addr}:{proxy.in_port}{rel}"

    # Start a streaming download and abort early
    r = requests.get(proxy_url, stream=True, timeout=5)
    next(r.iter_content(chunk_size=64 * 1024))  # read one chunk
    r.close()  # abort connection

    # Now the next request should still work
    h1 = _sha256_stream(proxy_url)
    h2 = _sha256_stream(proxy_url)
    assert h1 == h2


# ---------------- URL handling quirks ----------------


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
def test_query_string_and_encoded_paths(proxy_configuration):
    """Ensure proxies preserve path and query intact."""
    proxy: Proxy = proxy_configuration[0]
    server: HTTPServer = proxy_configuration[-1]

    base = _unique_dir("query")
    pth = _write_file(base, "sp ace.txt")
    rel = "/" + str(pth.relative_to(DOCROOT)).replace("\\", "/")  # contains space
    enc_rel = rel.replace(" ", "%20")
    qs = "?a=1&b=2&c=%7Bjson%7D"

    d = requests.get(f"http://{server.addr}:{server.port}{enc_rel}{qs}", timeout=3)
    p = requests.get(f"http://{proxy.in_addr}:{proxy.in_port}{enc_rel}{qs}", timeout=3)

    assert d.status_code == p.status_code == 200
    assert d.content == p.content
