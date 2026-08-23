cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME zbb)
project(${PROJECT_NAME} CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    add_compile_options(/W3)
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
    src/sort3.cpp
)

set(HEADERS
    inc/byte_order.h
    inc/cli.h
    inc/cmstruct.h
    inc/status.h
    inc/ecp.h
    inc/file_guard.h
    inc/mio.h
    inc/arithm.h
    inc/bwt.h
    inc/lzp_prep.h
    inc/mtf.h
    inc/sort3.h
    inc/workspace.h
)

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(ARCH "x64")
else()
    set(ARCH "x86")
endif()

set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "" FORCE)

foreach(CONFIG ${CMAKE_CONFIGURATION_TYPES})
    string(TOLOWER ${CONFIG} CONFIG_LOWER)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${CONFIG} ${CMAKE_BINARY_DIR}/${CONFIG_LOWER}/${ARCH})
endforeach()
