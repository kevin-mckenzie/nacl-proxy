cmake_minimum_required(VERSION 3.16.1)
project(proxy-nacl)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
# set(CMAKE_C_CLANG_TIDY
#     clang-tidy -p build-${BUILD_NAME}
#     --config-file=${CMAKE_SOURCE_DIR}/.clang-tidy)# --warnings-as-errors=*)

# set(CMAKE_C_CPPCHECK cppcheck --enable=all --std=c11 --error-exitcode=1 --inline-suppr
#     --suppress=missingIncludeSystem
#     --suppress=unusedFunction
# )

add_subdirectory(src)

