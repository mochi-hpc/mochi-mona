add_executable (example ${CMAKE_CURRENT_SOURCE_DIR}/example.c)
target_link_libraries (example mona)

add_executable (benchmark ${CMAKE_CURRENT_SOURCE_DIR}/benchmark.c)
target_link_libraries (benchmark mona)
