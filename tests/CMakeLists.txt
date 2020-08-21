add_executable (test-init test-init.c munit/munit.c)
target_include_directories (test-init PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/munit
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries (test-init mona)


add_test (NAME TestInit COMMAND ./test-init)
