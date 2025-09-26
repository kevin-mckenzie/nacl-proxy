import pathlib
import json

import invoke

# pylint: disable=C0116,C0301

DOCKER_IMAGE = "ghcr.io/kevin-mckenzie/n-proxy:latest"
PROJECT_NAME = "nacl-proxy"
VERSION = "1.0.0"

TOOLCHAIN_INSTALL_DIR = "/opt/cross"
BOOTLIN_CMAKE_TOOLCHAIN_POSTFIX = "share/buildroot/toolchainfile.cmake"

TARGETS = {
    "local": {
        "test": True,
    },
    "asan": {
        "test": True,
        "linking": "dynamic",
    },
    "valgrind": {
        "test": True,
        "emulator": "valgrind --leak-check=full --show-leak-kinds=all --exit-on-first-error=yes --error-exitcode=1 --errors-for-leak-kinds=all",
        "linking": "dynamic",
    },
    "linux-x86_64-musl": {
        "url": "https://toolchains.bootlin.com/downloads/releases/toolchains/x86-64/tarballs/x86-64--musl--stable-2024.05-1.tar.xz",
        "toolchain_file": f"{TOOLCHAIN_INSTALL_DIR}/x86-64--musl--stable-2024.05-1/{BOOTLIN_CMAKE_TOOLCHAIN_POSTFIX}",
        "test": True,
    },
}


def filenames_string(*patterns) -> str:
    files = []
    for pattern in patterns:
        files += [f.as_posix() for f in pathlib.Path(".").rglob(pattern) if f.is_file()]
    return " ".join(files)


C_FILES = filenames_string("src/**/*.c", "src/**/*.h")
CMAKE_FILES = filenames_string("cmake/*.cmake", "CMakeLists.txt")


@invoke.task
def format(ctx):  # pylint: disable=W0622
    ctx.run("ruff format")
    ctx.run(f"clang-format -i {C_FILES}")
    ctx.run(f"cmake-format -i {CMAKE_FILES}")


@invoke.task
def lint(ctx):
    ctx.run(f"lizard -w -C 12 -L 60 {C_FILES}")
    ctx.run(f"clang-format -i --dry-run -Werror {C_FILES}")
    ctx.run(f"cmake-format --check -l debug {CMAKE_FILES}")


@invoke.task
def analyze(
    ctx: invoke.context,
    target: str = "local",
    release: bool = False,
):
    if target not in TARGETS:
        raise invoke.Exit(
            f"Invalid target: {target} must be one of {list(TARGETS.keys())}"
        )
    linking = TARGETS[target].get("linking", "static")
    build_type = "MinSizeRel" if release else "Debug"
    build_name = f"{target}-{linking}-{build_type.lower()}"
    build_dir = f"build-{build_name}"

    ctx.run(f"mkdir -p dist/reports/analysis/{build_dir}")
    ctx.run(
        f"CodeChecker analyze {build_dir}/compile_commands.json \
                --skip .skip \
                --analyzers cppcheck gcc clangsa clang-tidy \
                --enable-all \
                --analyzer-config clang-tidy:take-config-from-directory=true \
                --analyzer-config cppcheck:cc-verbatim-args-file=.cppcheck \
                --disable security.insecureAPI.DeprecatedOrUnsafeBufferHandling \
                -o dist/reports/analysis/{build_dir}/out"
    )
    ctx.run(
        f"CodeChecker parse dist/reports/analysis/{build_dir}/out -o dist/reports/analysis/{build_dir}/report -e html"
    )
    ctx.run(f"rm -fdr dist/reports/analysis/{build_dir}/out")


@invoke.task
def build(
    ctx: invoke.context,
    target: str = "local",
    release: bool = False,
    list_targets: bool = False,
):
    if target not in TARGETS:
        raise invoke.Exit(
            f"Invalid target: {target} must be one of {list(TARGETS.keys())}"
        )

    if list_targets:
        print(f"Available targets: {list(TARGETS.keys())}")
        return

    linking = TARGETS[target].get("linking", "static")
    build_type = "MinSizeRel" if release else "Debug"
    build_name = f"{target}-{linking}-{build_type.lower()}"
    build_dir = f"build-{build_name}"

    cmake_defines = (
        f"-DLINKING={linking} "
        f"-DBUILD_NAME={build_name} "
        f"-DCMAKE_BUILD_TYPE={build_type} "
        "-G Ninja "
    )

    if "asan" == target:
        cmake_defines += "-DASAN=ON"

    if "toolchain_file" in TARGETS[target]:
        cmake_defines += f"-DCMAKE_TOOLCHAIN_FILE={TARGETS[target]['toolchain_file']} "

    if linking != "static":
        cmake_defines += "-DBUILD_SHARED_LIBS=ON"

    ctx.run(f"cmake {cmake_defines} -S . -B {build_dir}")
    ctx.run(f"cmake --build {build_dir} --target install")


@invoke.task
def test(
    ctx: invoke.context,
    target: str = "local",
    k: str = "",
    release: bool = False,
):
    if target not in TARGETS:
        raise invoke.Exit(
            f"Invalid target: {target} must be one of {list(TARGETS.keys())}"
        )

    linking = TARGETS[target].get("linking", "static")
    build_type = "MinSizeRel" if release else "Debug"
    build_name = f"{target}-{linking}-{build_type.lower()}"

    bin_path = pathlib.Path(f"./dist/bin/proxy-{build_name}").absolute().as_posix()
    emulator = TARGETS[target].get("emulator", "")

    ctx.run(
        f'PYTHON_PATH=test EMULATOR="{emulator}" BIN_PATH="{bin_path}" pytest . -k={k} -vv'
    )


@invoke.task
def package(ctx: invoke.context):
    ctx.run(f"tar -czf dist/{PROJECT_NAME}_{VERSION}_pkg.tar.gz dist/bin")
    ctx.run(f"zip -r dist/{PROJECT_NAME}_{VERSION}_docs.zip dist/docs")


@invoke.task
def clean(ctx: invoke.context):
    ctx.run("rm -fdr build-* dist")


@invoke.task
def docker(ctx: invoke.context, build: bool = False, push: bool = False):  # pylint: disable=W0621
    if not build and not push:
        ctx.run(
            f"docker run --user $(id -u):$(id -u) --rm -it -v .:/project --network=host {DOCKER_IMAGE} /bin/bash",
            pty=True,
        )
        return

    if build:
        ctx.run(f"docker build --network=host -t {DOCKER_IMAGE} .")

    if push:
        ctx.run(f"docker push {DOCKER_IMAGE}")


@invoke.task
def install_toolchains(ctx, target="all"):
    install_targets = TARGETS
    if target != "all":
        install_targets[target] = TARGETS[target]

    for conf in install_targets.values():
        if "url" in conf:
            tarball_name = conf["url"].split("/")[-1]
            ctx.run(f"curl -O {conf['url']}")
            ctx.run(f"tar -xf {tarball_name} -C {TOOLCHAIN_INSTALL_DIR}")
            ctx.run(f"rm {tarball_name}")
