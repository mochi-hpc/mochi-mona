add_executable (mona-send-recv-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/send_recv_benchmark.c)
target_link_libraries (mona-send-recv-benchmark mona MPI::MPI_C)

add_executable (mona-allreduce-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/allreduce_benchmark.c)
target_link_libraries (mona-allreduce-benchmark mona MPI::MPI_C)

add_executable (mona-bcast_benchmark ${CMAKE_CURRENT_SOURCE_DIR}/bcast_benchmark.c)
target_link_libraries (mona-bcast_benchmark mona MPI::MPI_C)

add_executable (mona-allgather_benchmark ${CMAKE_CURRENT_SOURCE_DIR}/allgather_benchmark.c)
target_link_libraries (mona-allgather_benchmark mona MPI::MPI_C)

add_executable (mona-scatter_benchmark ${CMAKE_CURRENT_SOURCE_DIR}/scatter_benchmark.c)
target_link_libraries (mona-scatter_benchmark mona MPI::MPI_C)

install (TARGETS mona-send-recv-benchmark
         DESTINATION bin)
install (TARGETS mona-allreduce-benchmark
         DESTINATION bin)
install (TARGETS mona-bcast_benchmark
         DESTINATION bin)
install (TARGETS mona-allgather_benchmark
         DESTINATION bin)
install (TARGETS mona-scatter_benchmark
         DESTINATION bin)
