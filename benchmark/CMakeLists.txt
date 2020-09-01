add_executable (mona-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/benchmark.c)
target_link_libraries (mona-benchmark mona)

install (FILES "${CMAKE_CURRENT_BINARY_DIR}/mona-benchmark"
         DESTINATION bin)
