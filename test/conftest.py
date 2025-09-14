import pytest

def pytest_addoption(parser: pytest.Parser):
    parser.addoption("--bin_path", type=str, required=True)
    parser.addoption("--emulator", type=str, default="")

