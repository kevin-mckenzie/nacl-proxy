import dataclasses
import time
import subprocess
import os

import pytest


@dataclasses.dataclass
class Proxy:
    proc: subprocess.Popen
    in_addr: str
    in_port: int
    out_addr: str
    out_port: int
    encrypt_in: bool = False
    encrypt_out: bool =False


@dataclasses.dataclass
class HTTPServer:
    proc: subprocess.Popen
    addr: str
    port: int


def create_proxy(in_port, out_port, encrypt_in=False, encrypt_out=False) -> Proxy:
    bin_path = os.getenv("BIN_PATH")
    emulator_str = os.getenv("EMULATOR")

    proc_args_list = [bin_path] if not emulator_str else emulator_str.split(" ") + [bin_path]

    if encrypt_in:
        proc_args_list += ["-i"]

    if encrypt_out:
        proc_args_list += ["-o"]

    proc_args_list += ["127.0.0.1", str(in_port), "127.0.0.1", str(out_port)]

    proxy = Proxy(
        proc=subprocess.Popen(proc_args_list),
        in_addr="127.0.0.1",
        in_port=in_port,
        out_addr="127.0.0.1",
        out_port=out_port,
        encrypt_in=encrypt_in,
        encrypt_out=encrypt_out,
    )

    if "valgrind" in proc_args_list:
        time.sleep(1)
    else:
        time.sleep(0.2)

    return proxy



@pytest.fixture(scope="function")
def single_proxy_unencrypted_fs():
    proxies = [create_proxy(7999, 8000)]

    yield proxies

    for proxy in proxies:
        proxy.proc.terminate()

@pytest.fixture(scope="function")
def double_proxy_unencrypted_fs():
    proxies = []
    proxies.append(create_proxy(7998, 7999))
    proxies.append(create_proxy(7999, 8000))

    yield proxies

    for proxy in proxies:
        proxy.proc.terminate()

@pytest.fixture(scope="function")
def triple_proxy_unencrypted_fs():
    proxies = []
    proxies.append(create_proxy(7997, 7998))
    proxies.append(create_proxy(7998, 7999))
    proxies.append(create_proxy(7999, 8000))

    yield proxies

    for proxy in proxies:
        proxy.proc.terminate()


@pytest.fixture(scope="function")
def double_proxy_encrypted_fs():
    proxies = []
    proxies.append(create_proxy(7998, 7999, encrypt_out=True))
    proxies.append(create_proxy(7999, 8000, encrypt_in=True))

    yield proxies

    for proxy in proxies:
        proxy.proc.terminate()


@pytest.fixture(scope="function")
def triple_proxy_encrypted_fs():
    proxies = []
    proxies.append(create_proxy(7997, 7998, encrypt_out=True))
    proxies.append(create_proxy(7998, 7999, encrypt_in=True, encrypt_out=True))
    proxies.append(create_proxy(7999, 8000, encrypt_in=True))

    yield proxies

    for proxy in proxies:
        proxy.proc.terminate()

@pytest.fixture(scope="function")
def quad_proxy_encrypted_fs():
    proxies = []
    proxies.append(create_proxy(7996, 7997, encrypt_out=True))
    proxies.append(create_proxy(7997, 7998, encrypt_in=True, encrypt_out=True))
    proxies.append(create_proxy(7998, 7999, encrypt_in=True, encrypt_out=True))
    proxies.append(create_proxy(7999, 8000, encrypt_in=True))

    yield proxies

    for proxy in proxies:
        proxy.proc.terminate()


@pytest.fixture(scope="function")
def python_http_server_fs():
    run_path = ["python3", "-m", "http.server", "-d", "/tmp"]

    server = HTTPServer(
        proc=subprocess.Popen(run_path),
        addr="127.0.0.1",
        port=8000,
    )
    time.sleep(0.2)
    yield server

    server.proc.terminate()
