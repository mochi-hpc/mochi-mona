add_executable (example ${CMAKE_CURRENT_SOURCE_DIR}/example.c)
target_link_libraries (example mona MPI::MPI_C)
