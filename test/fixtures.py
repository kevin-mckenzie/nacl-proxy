import dataclasses
import time
import subprocess

import pytest


@dataclasses.dataclass
class Proxy:
    proc: subprocess.Popen
    in_addr: str
    in_port: int
    out_addr: str
    out_port: int


@dataclasses.dataclass
class HTTPServer:
    proc: subprocess.Popen
    addr: str
    port: int


@pytest.fixture(scope="function")
def single_unencrypted_proxy_fs(request):
    bin_path = request.config.getoption("--bin_path")
    emulator_str = request.config.getoption("--emulator")

    proc_args_list = [bin_path] if not emulator_str else emulator_str.split(" ") + [bin_path]

    proc_args_list += ["127.0.0.1", "7999", "127.0.0.1", "8000"]

    proxy = Proxy(
        proc=subprocess.Popen(proc_args_list),
        in_addr="127.0.0.1",
        in_port=7999,
        out_addr="127.0.0.1",
        out_port=8000,
    )
    time.sleep(0.2)
    yield proxy

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
