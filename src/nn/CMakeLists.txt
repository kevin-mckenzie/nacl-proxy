cmake_minimum_required(VERSION 4.0)
project(netnacl C)

set(TARGET netnacl)

if(NOT LINK_TYPE)
  set(LINK_TYPE STATIC)
endif()

if (NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
endif()

add_library(${TARGET} ${LINK_TYPE}
  src/netnacl.c
  src/tweetnacl.c
)

target_include_directories(${TARGET} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(${TARGET} PRIVATE _POSIX_C_SOURCE=200809L)

if(COVERAGE)
  target_link_options(${TARGET} PRIVATE --coverage)
  add_compile_options(${TARGET} PRIVATE --coverage)
endif()

if(ASAN)
  target_link_options(${TARGET} PRIVATE -fsanitize=address,undefined
                      -fno-omit-frame-pointer)
  target_compile_options(${TARGET} PRIVATE -fsanitize=address,undefined)
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
  target_compile_options(${TARGET} PRIVATE -g -Og)
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "MinSizeRel")
  target_compile_options(${TARGET} PRIVATE -Os -ffunction-sections
                                           -fdata-sections)
  target_link_options(${TARGET} PRIVATE -s -Wl,--gc-sections)
endif()
