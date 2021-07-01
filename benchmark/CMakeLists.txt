add_executable (mona-send-recv-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/send_recv_benchmark.c)
target_link_libraries (mona-send-recv-benchmark mona MPI::MPI_C)

add_executable (mona-allreduce-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/allreduce_benchmark.c)
target_link_libraries (mona-allreduce-benchmark mona MPI::MPI_C)

install (FILES "${CMAKE_CURRENT_BINARY_DIR}/mona-send-recv-benchmark"
         DESTINATION bin)
install (FILES "${CMAKE_CURRENT_BINARY_DIR}/mona-allreduce-benchmark"
         DESTINATION bin)
