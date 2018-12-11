cmake_minimum_required(VERSION 3.5)
project(carta_backend)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")

FIND_PACKAGE(HDF5)
FIND_PACKAGE(Protobuf REQUIRED)
FIND_PACKAGE(Threads)
INCLUDE_DIRECTORIES(${HDF5_INCLUDE_DIR})

ADD_SUBDIRECTORY(carta-protobuf)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR}/ImageData)
set(LINK_LIBS ${LINK_LIBS} carta-protobuf ${PROTOBUF_LIBRARY} fmt uWS ssl crypto z zfp tbb casa_casa casa_coordinates casa_tables casa_images casa_lattices casa_fits casa_measures casa_scimath ${HDF5_LIBRARIES} ${CMAKE_THREAD_LIBS_INIT})
set(SOURCE_FILES
  main.cc
  Session.cc
  Frame.cc
  compression.cc
  ImageData/HDF5Attributes.cc
  ImageData/FileLoader.cc
  FileInfoLoader.cc
  Region/Region.cc
  Region/RegionStats.cc
  Region/RegionProfiler.cc
  Region/Histogram.cc
  OnMessageTask.cc
  AnimationQueue.cc
  FileSettings.cc
  util.cc)
add_definitions(-DHAVE_HDF5)
add_executable(carta_backend ${SOURCE_FILES})
target_link_libraries(carta_backend ${LINK_LIBS})

# Tests
option(test "Build all tests." OFF)
if(test)
  enable_testing()
  find_package(GTest)
  include_directories(${GTEST_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR})

  add_executable(testPriorityCtpl test/TestPriorityCtpl.cpp)
  target_link_libraries(testPriorityCtpl gtest gtest_main Threads::Threads)

  add_test(NAME TestPCtpl COMMAND testPriorityCtpl)
endif(test)
