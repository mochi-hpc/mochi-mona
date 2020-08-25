#
#  general cmake flags:
#    -DCMAKE_INSTALL_PREFIX=/usr/local     -- the prefix for installing
#    -DCMAKE_BUILD_TYPE=type               -- type can be Debug, Release, ...
#    -DCMAKE_PREFIX_PATH=/dir              -- external packages
#
#     note that CMAKE_PREFIX_PATH can be a list of directories:
#      -DCMAKE_PREFIX_PATH='/dir1;/dir2;/dir3'
#

cmake_minimum_required (VERSION 3.14)
project (mona C)
enable_testing ()

option(ENABLE_TESTS    "Build tests. May require CppUnit_ROOT" OFF)
option(ENABLE_EXAMPLES "Build examples" OFF)

add_definitions (-Wall -Wextra -g)

# link shared lib with full rpath
set (CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/lib")
set (CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

# setup cache variables for ccmake
if (NOT CMAKE_BUILD_TYPE)
    set (CMAKE_BUILD_TYPE Release
         CACHE STRING "Choose the type of build." FORCE)
    set_property (CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS 
                  "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif ()
set (CMAKE_PREFIX_PATH "" CACHE STRING "External dependencies path")
set (BUILD_SHARED_LIBS "OFF" CACHE BOOL "Build a shared library")

find_package (PkgConfig REQUIRED)
find_package (mercury REQUIRED)
pkg_check_modules (ARGOBOTS REQUIRED IMPORTED_TARGET argobots)

add_subdirectory (src)

if (ENABLE_TESTS)
    find_package (MPI REQUIRED)
    enable_testing()
    add_subdirectory (tests)
endif (ENABLE_TESTS)

if(${ENABLE_EXAMPLES})
  add_subdirectory (examples)
endif(${ENABLE_EXAMPLES})
