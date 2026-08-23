cmake_minimum_required(VERSION 3.20)
project(zbb CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra)
endif()

set(SOURCES
    src/mio.cpp
    src/arithm.cpp
    src/bwt.cpp
    src/cli.cpp
    src/codec.cpp
    src/ecp.cpp
    src/lzp_prep.cpp
    src/mtf.cpp
)

set(HEADERS
    inc/byte_order.h
    inc/cli.h
    inc/format.h
    inc/status.h
    inc/ecp.h
    inc/file_guard.h
    inc/mio.h
    inc/arithm.h
    inc/bwt.h
    inc/lzp_prep.h
    inc/mtf.h
    inc/workspace.h
)

add_executable(zbb ${SOURCES} ${HEADERS})

target_include_directories(zbb PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
