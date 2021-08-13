add_executable (example ${CMAKE_CURRENT_SOURCE_DIR}/example.c)
target_link_libraries (example mona MPI::MPI_C)

add_executable (barrier ${CMAKE_CURRENT_SOURCE_DIR}/barrier.c)
target_link_libraries (barrier mona MPI::MPI_C)

add_executable (bcast ${CMAKE_CURRENT_SOURCE_DIR}/bcast.c)
target_link_libraries (bcast mona MPI::MPI_C)
