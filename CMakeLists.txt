cmake_minimum_required (VERSION 3.8)

project("dirwatcher")

if (MSVC)
    string(REPLACE "/W3" "" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
    string(REPLACE "/W3" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
    add_compile_options(
        /W4
        /permissive-
        /w14242
        /w14254
        /w14263
        /w14265
        /w14287
        /wd4200
    )
else()
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
    )
endif()

add_library(dirwatcher STATIC
    "${CMAKE_SOURCE_DIR}/src/dirwatcher_win32.c"
)

target_include_directories(dirwatcher
    PUBLIC "${CMAKE_SOURCE_DIR}/include/"
)

if (DIRWATCHER_TEST_BUILD)
    add_subdirectory("test")
endif()
