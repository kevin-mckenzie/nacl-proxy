set(TARGET proxy-${BUILD_NAME})

include_directories(include)
add_subdirectory(${CMAKE_SOURCE_DIR}/src/netnacl)

add_executable(${TARGET} main.c buf.c event.c network.c proxy.c)
add_compile_options(${TARGET} PRIVATE -Wall -Wpedantic -Werror)

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

if(${LINKING} STREQUAL "static")
  target_link_options(${TARGET} PRIVATE -static)
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "Debug")
  target_compile_options(${TARGET} PRIVATE -g -Og)
endif()

if(${CMAKE_BUILD_TYPE} STREQUAL "MinSizeRel")
  target_compile_options(${TARGET} PRIVATE -Os -ffunction-sections
                                           -fdata-sections)
  target_link_options(${TARGET} PRIVATE -s -Wl,--gc-sections)
endif()

target_link_libraries(${TARGET} PRIVATE netnacl)

install(TARGETS ${TARGET} DESTINATION ${CMAKE_SOURCE_DIR}/dist/bin)
