# load package helper for generating cmake CONFIG packages
include (CMakePackageConfigHelpers)

# where to install files for "find_package"
set (mona-pkg "share/cmake/mona")

#
# library version set here (e.g. for shared libs).
#
set (MONA_VERSION_MAJOR 0)
set (MONA_VERSION_MINOR 2)
set (MONA_VERSION_PATCH 0)
set (mona-vers "${MONA_VERSION_MAJOR}.${MONA_VERSION_MINOR}")
set (MONA_VERSION "${mona-vers}.${MONA_VERSION_PATCH}")

add_library (mona
            ${CMAKE_CURRENT_SOURCE_DIR}/mona.c
            ${CMAKE_CURRENT_SOURCE_DIR}/mona-unexpected.c
            ${CMAKE_CURRENT_SOURCE_DIR}/mona-expected.c
            ${CMAKE_CURRENT_SOURCE_DIR}/mona-comm.c
            ${CMAKE_CURRENT_SOURCE_DIR}/mona-barrier.c
            ${CMAKE_CURRENT_SOURCE_DIR}/mona-bcast.c
            ${CMAKE_CURRENT_SOURCE_DIR}/mona-gather.c
            ${CMAKE_CURRENT_SOURCE_DIR}/mona-scatter.c
            ${CMAKE_CURRENT_SOURCE_DIR}/mona-allgather.c
            ${CMAKE_CURRENT_SOURCE_DIR}/mona-alltoall.c
            ${CMAKE_CURRENT_SOURCE_DIR}/mona-reduce.c
            ${CMAKE_CURRENT_SOURCE_DIR}/mona-allreduce.c
            ${CMAKE_CURRENT_SOURCE_DIR}/mona-sendrecv.c)
target_link_libraries (mona mercury PkgConfig::ARGOBOTS)
target_include_directories (mona PUBLIC $<INSTALL_INTERFACE:include>)

# local include's BEFORE, in case old incompatable .h files in prefix/include
target_include_directories (mona BEFORE PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>)

# for shared libs, establish the lib version
set_target_properties (mona
    PROPERTIES VERSION ${MONA_VERSION}
    SOVERSION ${MONA_VERSION_MAJOR})

set (MONA_TARGETS mona)

if (ENABLE_MPI_WRAPPERS)
    find_package (MPI REQUIRED)
    set (WRAP ${PROJECT_SOURCE_DIR}/src/wrap/wrap.py)
    include (${PROJECT_SOURCE_DIR}/src/wrap/WrapConfig.cmake)
    add_wrapped_file (mona-mpi-wrappers.cpp mona-mpi-wrappers.cpp.in)
    add_library (mona-mpi-wrappers mona-mpi-wrappers.cpp)
    target_link_libraries (mona-mpi-wrappers mona MPI::MPI_C)
    target_include_directories (mona BEFORE PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>)
    list (APPEND MONA_TARGETS mona-mpi-wrappers)
    set (DEST_DIR "${CMAKE_INSTALL_PREFIX}")
    configure_file ("mona-mpi.pc.in" "mona-mpi.pc" @ONLY)
endif ()

#
# installation stuff (packaging and install commands)
#
write_basic_package_version_file (
    "mona-config-version.cmake"
    VERSION ${MONA_VERSION}
    COMPATIBILITY AnyNewerVersion)

# generate our config file for find_package()
configure_file (mona-config.cmake.in mona-config.cmake @ONLY)

# some bits for the pkg-config file
set (DEST_DIR "${CMAKE_INSTALL_PREFIX}")
set (PRIVATE_LIBS "-lmona")
configure_file ("mona.pc.in" "mona.pc" @ONLY)

#
# "make install" rules
#
install (TARGETS ${MONA_TARGETS} EXPORT mona-targets
         ARCHIVE DESTINATION lib
         LIBRARY DESTINATION lib)
install (EXPORT mona-targets
         DESTINATION ${mona-pkg}
         FILE "mona-targets.cmake")
install (FILES "${CMAKE_CURRENT_BINARY_DIR}/mona-config.cmake"
               "${CMAKE_CURRENT_BINARY_DIR}/mona-config-version.cmake"
         DESTINATION ${mona-pkg} )
install (FILES "${CMAKE_CURRENT_SOURCE_DIR}/../include/mona.h"
               "${CMAKE_CURRENT_SOURCE_DIR}/../include/mona-coll.h"
         DESTINATION include)
install (FILES "${CMAKE_CURRENT_BINARY_DIR}/mona.pc"
         DESTINATION "lib/pkgconfig/")
if (ENABLE_MPI_WRAPPERS)
install (FILES "${CMAKE_CURRENT_SOURCE_DIR}/../include/mona-mpi.h"
         DESTINATION include)
 install (FILES "${CMAKE_CURRENT_BINARY_DIR}/mona-mpi.pc"
         DESTINATION "lib/pkgconfig/")
endif ()
