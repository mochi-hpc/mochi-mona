add_executable (test-init test-init.c munit/munit.c)
target_include_directories (test-init PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/munit
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries (test-init mona)

add_executable (test-send-recv-unexpected test-send-recv-unexpected.c munit/munit.c)
target_include_directories (test-send-recv-unexpected PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/munit
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries (test-send-recv-unexpected mona)

add_executable (test-send-recv-expected test-send-recv-expected.c munit/munit.c)
target_include_directories (test-send-recv-expected PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/munit
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries (test-send-recv-expected mona)


add_test (NAME TestInit COMMAND ./test-init)
add_test (NAME TestSendRecvUnexpected COMMAND mpirun -np 2 ./test-send-recv-unexpected)
add_test (NAME TestSendRecvExpected COMMAND mpirun -np 2 ./test-send-recv-expected)
