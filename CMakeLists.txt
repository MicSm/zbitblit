cmake_minimum_required(VERSION 3.10)
set(PROJECT_NAME zbb)
project(${PROJECT_NAME} C)

set(CMAKE_C_STANDARD 11)

add_compile_options(-Wall)
if(UNIX)
    add_compile_options(-Wextra)
endif()

set(SOURCES
    src/mio.c
    src/arithm.c
    src/bwt.c
    src/ecp.c
    src/lzp_prep.c
    src/mtf.c
    src/sort3.c
)

set(HEADERS
    inc/cmstruct.h
    inc/mio.h
    inc/arithm.h
    inc/bwt.h
    inc/lzp_prep.h
    inc/mtf.h
    inc/sort3.h
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

set_target_properties(${PROJECT_NAME} PROPERTIES
    OUTPUT_NAME "${PROJECT_NAME}_${ARCH}"
)

if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        DEBUG_POSTFIX "d"
    )
endif()
