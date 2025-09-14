import requests

from fixtures import HTTPServer, Proxy, single_unencrypted_proxy_fs, python_http_server_fs

# pylint: disable=W0621

def test_get(single_unencrypted_proxy_fs, python_http_server_fs):
    proxy: Proxy = single_unencrypted_proxy_fs
    server: HTTPServer = python_http_server_fs

    direct = requests.get(f"http://{server.addr}:{server.port}")
    via_proxy = requests.get(f"http://{proxy.in_addr}:{proxy.in_port}")

    assert direct.content == via_proxy.content
