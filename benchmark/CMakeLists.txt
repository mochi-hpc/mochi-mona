add_executable (mona-usend-urecv-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/usend_urecv_benchmark.c)
target_link_libraries (mona-usend-urecv-benchmark mona MPI::MPI_C)

add_executable (mona-send-recv-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/send_recv_benchmark.c)
target_link_libraries (mona-send-recv-benchmark mona MPI::MPI_C)

add_executable (mona-allreduce-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/allreduce_benchmark.c)
target_link_libraries (mona-allreduce-benchmark mona MPI::MPI_C)

add_executable (mona-reduce-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/reduce_benchmark.c)
target_link_libraries (mona-reduce-benchmark mona MPI::MPI_C)

add_executable (mona-bcast-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/bcast_benchmark.c)
target_link_libraries (mona-bcast-benchmark mona MPI::MPI_C)

add_executable (mona-allgather-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/allgather_benchmark.c)
target_link_libraries (mona-allgather-benchmark mona MPI::MPI_C)

add_executable (mona-scatter-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/scatter_benchmark.c)
target_link_libraries (mona-scatter-benchmark mona MPI::MPI_C)

add_executable (mona-gather-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/gather_benchmark.c)
target_link_libraries (mona-gather-benchmark mona MPI::MPI_C)

add_executable (mona-alltoall-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/alltoall_benchmark.c)
target_link_libraries (mona-alltoall-benchmark mona MPI::MPI_C)

install (TARGETS mona-usend-urecv-benchmark
         DESTINATION bin)
install (TARGETS mona-send-recv-benchmark
         DESTINATION bin)
install (TARGETS mona-allreduce-benchmark
         DESTINATION bin)
install (TARGETS mona-reduce-benchmark
         DESTINATION bin)
install (TARGETS mona-bcast-benchmark
         DESTINATION bin)
install (TARGETS mona-allgather-benchmark
         DESTINATION bin)
install (TARGETS mona-scatter-benchmark
         DESTINATION bin)
install (TARGETS mona-gather-benchmark
         DESTINATION bin)
install (TARGETS mona-alltoall-benchmark
         DESTINATION bin)
