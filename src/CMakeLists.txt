set(TARGET proxy-${BUILD_NAME})

include_directories(include)
add_executable(${TARGET} main.c buf.c event.c network.c proxy.c netnacl.c tweetnacl.c)
set_source_files_properties(tweetnacl.c PROPERTIES SKIP_LINTING ON)
target_compile_options(${TARGET} PRIVATE -Wall -Wpedantic -Werror)

target_compile_definitions(${TARGET} PRIVATE _POSIX_C_SOURCE=200809L)

if(COVERAGE)
  target_link_options(${TARGET} PRIVATE --coverage)
  target_compile_options(${TARGET} PRIVATE --coverage)
endif()

if(ASAN)
  target_link_options(${TARGET} PRIVATE -fsanitize=address,undefined
                      -fno-omit-frame-pointer)
  target_compile_options(${TARGET} PRIVATE -fsanitize=address,undefined)
endif()

if(${LINKING} STREQUAL "static")
  target_link_options(${TARGET} PRIVATE -static)
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
  target_compile_options(${TARGET} PRIVATE -g -Og)
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "MinSizeRel")
  target_compile_options(${TARGET} PRIVATE -O3 -ffunction-sections
                                           -fdata-sections -march=native)
  target_link_options(${TARGET} PRIVATE -s -Wl,--gc-sections)
endif()

install(TARGETS ${TARGET} DESTINATION ${CMAKE_SOURCE_DIR}/dist/bin)
