add_executable(test
    "${CMAKE_CURRENT_SOURCE_DIR}/test.c"
)

target_link_libraries(test
    "dirwatcher"
)
