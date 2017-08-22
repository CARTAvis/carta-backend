cmake_minimum_required(VERSION 3.8)
project(hdf5_mpi_test)

set(CMAKE_CXX_STANDARD 11)

find_package (HDF5)
INCLUDE_DIRECTORIES (${HDF5_INCLUDE_DIR})
set (LINK_LIBS ${LINK_LIBS} hdf5_serial hdf5_cpp)

set(SOURCE_FILES main.cpp)
add_executable(hdf5_mpi_test ${SOURCE_FILES})
target_link_libraries (hdf5_mpi_test ${LINK_LIBS})