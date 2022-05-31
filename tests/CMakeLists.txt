function (add_munit_test)
    set (prefix MUNIT_TEST)
    set (flags MPI WRAPPER)
    set (singleValues NAME NUM_PROCS)
    set (multiValues SOURCES)

    include (CMakeParseArguments)
    cmake_parse_arguments (${prefix}
                          "${flags}"
                          "${singleValues}"
                          "${multiValues}"
                          ${ARGN})

  if (${MUNIT_TEST_WRAPPER})
    set (WRAPPER_LIB mona-mpi-wrappers)
  endif ()

  add_executable (${MUNIT_TEST_NAME} ${MUNIT_TEST_SOURCES} munit/munit.c)
  target_include_directories (${MUNIT_TEST_NAME} PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/munit
    ${CMAKE_CURRENT_SOURCE_DIR}/../include)
  target_link_libraries (${MUNIT_TEST_NAME} mona ${WRAPPER_LIB} MPI::MPI_C)
  if (${MUNIT_TEST_MPI})
    add_test (NAME ${MUNIT_TEST_NAME} COMMAND mpirun -np ${MUNIT_TEST_NUM_PROCS} ${CMAKE_CURRENT_BINARY_DIR}/${MUNIT_TEST_NAME} --no-fork)
  else ()
    add_test (NAME ${MUNIT_TEST_NAME} COMMAND ${CMAKE_CURRENT_BINARY_DIR}/${MUNIT_TEST_NAME} --no-fork)
  endif ()
endfunction ()

# Base API tests
add_munit_test (NAME TestNA MPI NUM_PROCS 2 SOURCES test-na.c)
add_munit_test (NAME TestInit SOURCES test-init.c)
add_munit_test (NAME TestUSendURecv MPI NUM_PROCS 2 SOURCES test-usend-urecv.c)
add_munit_test (NAME TestSendRecv MPI NUM_PROCS 2 SOURCES test-send-recv.c)
add_munit_test (NAME TestUISendUIRecv MPI NUM_PROCS 2 SOURCES test-uisend-uirecv.c)
add_munit_test (NAME TestISendIRecv MPI NUM_PROCS 2 SOURCES test-isend-irecv.c)
add_munit_test (NAME TestUISendUIRecvMulti MPI NUM_PROCS 2 SOURCES test-uisend-uirecv-multi.c)
add_munit_test (NAME TestISendIRecvMulti MPI NUM_PROCS 2 SOURCES test-isend-irecv-multi.c)
#add_munit_test (NAME TestSendRecvSelf MPI NUM_PROCS 2 SOURCES test-send-recv-self.c)
add_munit_test (NAME TestSendRecvUnexpected MPI NUM_PROCS 2 SOURCES test-send-recv-unexpected.c)
add_munit_test (NAME TestSendRecvExpected MPI NUM_PROCS 2 SOURCES test-send-recv-expected.c)
add_munit_test (NAME TestPutGet MPI NUM_PROCS 2 SOURCES test-put-get.c)

# Collective API tests
add_munit_test (NAME TestSendRecvColl MPI NUM_PROCS 2 SOURCES test-send-recv-coll.c)
add_munit_test (NAME TestBarrier MPI NUM_PROCS 5 SOURCES test-barrier.c)
add_munit_test (NAME TestBcast MPI NUM_PROCS 5 SOURCES test-bcast.c)
add_munit_test (NAME TestGather MPI NUM_PROCS 5 SOURCES test-gather.c)
add_munit_test (NAME TestGatherv MPI NUM_PROCS 5 SOURCES test-gatherv.c)
add_munit_test (NAME TestAllGather MPI NUM_PROCS 5 SOURCES test-allgather.c)
add_munit_test (NAME TestScatter MPI NUM_PROCS 5 SOURCES test-scatter.c)
add_munit_test (NAME TestScatterV MPI NUM_PROCS 5 SOURCES test-scatterv.c)
#add_munit_test (NAME TestAlltoAll MPI NUM_PROCS 5 SOURCES test-alltoall.c)
add_munit_test (NAME TestReduce MPI NUM_PROCS 5 SOURCES test-reduce.c)
add_munit_test (NAME TestAllReduce MPI NUM_PROCS 5 SOURCES test-allreduce.c)
add_munit_test (NAME TestWaitAny MPI NUM_PROCS 5 SOURCES test-waitany.c)

# MPI wrappers tests
if (${ENABLE_MPI_WRAPPERS})
  add_munit_test (NAME TestMPISendRecvColl MPI WRAPPER NUM_PROCS 2 SOURCES test-mpi-send-recv-coll.c)
  add_munit_test (NAME TestMPIBarrier MPI WRAPPER NUM_PROCS 5 SOURCES test-mpi-barrier.c)
  add_munit_test (NAME TestMPIBcast MPI WRAPPER NUM_PROCS 5 SOURCES test-mpi-bcast.c)
  add_munit_test (NAME TestMPIGather MPI WRAPPER NUM_PROCS 5 SOURCES test-mpi-gather.c)
  add_munit_test (NAME TestMPIGatherv MPI WRAPPER NUM_PROCS 5 SOURCES test-mpi-gatherv.c)
  add_munit_test (NAME TestMPIAllGather MPI WRAPPER NUM_PROCS 5 SOURCES test-mpi-allgather.c)
  add_munit_test (NAME TestMPIScatter MPI WRAPPER NUM_PROCS 5 SOURCES test-mpi-scatter.c)
  add_munit_test (NAME TestMPIScatterV MPI WRAPPER NUM_PROCS 5 SOURCES test-mpi-scatterv.c)
  #add_munit_test (NAME TestMPIAlltoAll MPI WRAPPER NUM_PROCS 5 SOURCES test-mpi-alltoall.c)
  #add_munit_test (NAME TestMPIReduce MPI WRAPPER NUM_PROCS 5 SOURCES test-mpi-reduce.c)
  #add_munit_test (NAME TestMPIAllReduce MPI WRAPPER NUM_PROCS 5 SOURCES test-mpi-allreduce.c)
  #add_munit_test (NAME TestMPIWaitAny MPI WRAPPER NUM_PROCS 5 SOURCES test-mpi-waitany.c)
endif ()
