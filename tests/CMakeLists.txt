add_executable (test-init test-init.c munit/munit.c)
target_include_directories (test-init PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/munit
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries (test-init mona)

add_executable (test-send-recv test-send-recv.c munit/munit.c)
target_include_directories (test-send-recv PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/munit
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries (test-send-recv mona)

add_executable (test-send-recv-self test-send-recv-self.c munit/munit.c)
target_include_directories (test-send-recv-self PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/munit
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries (test-send-recv-self mona)

add_executable (test-isend-irecv test-isend-irecv.c munit/munit.c)
target_include_directories (test-isend-irecv PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/munit
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries (test-isend-irecv mona)

add_executable (test-isend-irecv-multi test-isend-irecv-multi.c munit/munit.c)
target_include_directories (test-isend-irecv-multi PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/munit
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries (test-isend-irecv-multi mona)

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

add_executable (test-put-get test-put-get.c munit/munit.c)
target_include_directories (test-put-get PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/munit
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries (test-put-get mona)

add_executable (test-na test-na.c munit/munit.c)
target_include_directories (test-na PUBLIC 
    ${CMAKE_CURRENT_SOURCE_DIR}/munit
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_link_libraries (test-na mona)


add_test (NAME TestInit COMMAND ./test-init)
add_test (NAME TestSendRecv COMMAND mpirun -np 2 ./test-send-recv)
add_test (NAME TestSendRecvSelf COMMAND mpirun -np 2 ./test-send-recv-self)
add_test (NAME TestISendIRecv COMMAND mpirun -np 2 ./test-isend-irecv)
add_test (NAME TestISendIRecvMulti COMMAND mpirun -np 2 ./test-isend-irecv-multi)
add_test (NAME TestSendRecvUnexpected COMMAND mpirun -np 2 ./test-send-recv-unexpected)
add_test (NAME TestSendRecvExpected COMMAND mpirun -np 2 ./test-send-recv-expected)
add_test (NAME TestPutGet COMMAND mpirun -np 2 ./test-put-get)
add_test (NAME TestNA COMMAND mpirun -np 2 ./test-na)
