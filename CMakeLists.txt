cmake_minimum_required(VERSION 3.8)
project(hdf5_mpi_test)

set(CMAKE_CXX_STANDARD 11)

find_package (HDF5)
find_package(HighFive)
INCLUDE_DIRECTORIES (${HDF5_INCLUDE_DIR})
INCLUDE_DIRECTORIES (${HighFive_INCLUDE_DIR})
set (LINK_LIBS ${LINK_LIBS} fmt hdf5_openmpi)

set(SOURCE_FILES main.cpp)
add_executable(hdf5_mpi_test ${SOURCE_FILES})
target_link_libraries (hdf5_mpi_test ${LINK_LIBS})