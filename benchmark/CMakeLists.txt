add_executable (mona-usend-urecv-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/usend_urecv_benchmark.c)
target_link_libraries (mona-usend-urecv-benchmark mona MPI::MPI_C)

add_executable (mona-allreduce-benchmark ${CMAKE_CURRENT_SOURCE_DIR}/allreduce_benchmark.c)
target_link_libraries (mona-allreduce-benchmark mona MPI::MPI_C)

add_executable (mona-bcast_benchmark ${CMAKE_CURRENT_SOURCE_DIR}/bcast_benchmark.c)
target_link_libraries (mona-bcast_benchmark mona MPI::MPI_C)

add_executable (mona-allgather_benchmark ${CMAKE_CURRENT_SOURCE_DIR}/allgather_benchmark.c)
target_link_libraries (mona-allgather_benchmark mona MPI::MPI_C)

add_executable (mona-scatter_benchmark ${CMAKE_CURRENT_SOURCE_DIR}/scatter_benchmark.c)
target_link_libraries (mona-scatter_benchmark mona MPI::MPI_C)

add_executable (mona-gather_benchmark ${CMAKE_CURRENT_SOURCE_DIR}/gather_benchmark.c)
target_link_libraries (mona-gather_benchmark mona MPI::MPI_C)

install (TARGETS mona-usend-urecv-benchmark
         DESTINATION bin)
install (TARGETS mona-allreduce-benchmark
         DESTINATION bin)
install (TARGETS mona-bcast_benchmark
         DESTINATION bin)
install (TARGETS mona-allgather_benchmark
         DESTINATION bin)
install (TARGETS mona-scatter_benchmark
         DESTINATION bin)
install (TARGETS mona-gather_benchmark
         DESTINATION bin)
