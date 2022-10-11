cmake_minimum_required(VERSION 3.10)

project(carta_backend_test_perf)

# Parameter used in the test/CommonTestUtilities.cc file
add_definitions(-DCARTA_USER_FOLDER_PREFIX=".carta-unit-tests")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Needed by clang-tidy and other clang tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Include gtest basic functions
set(TEST_COMMON_LIBS gtest gtest_main)

find_package(GTest)
include_directories(${GTEST_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

# Include unit test utilities
include_directories(../test)

# Include HDF5 library
find_package(HDF5 REQUIRED COMPONENTS CXX)
if (HDF5_FOUND)
    include_directories(${HDF5_INCLUDE_DIR})
    set(LINK_LIBS ${LINK_LIBS} ${HDF5_LIBRARIES})
else ()
    message(FATAL_ERROR "Could not find HDF5.")
endif ()

# Add all the sources in the main project and remove Main.cc
foreach (src_file ${SOURCE_FILES})
    list(APPEND SOURCES ../${src_file})
endforeach ()
list(REMOVE_ITEM SOURCES REMOVE_ITEM ../src/Main/Main.cc)

# Add test utilities source files
set(TEST_UTILITIES
        ../test/CommonTestUtilities.cc
        AxesTransformer.cc
        TestFrame.cc)

# Set performance testing cases
set(TEST_PERF_SOURCES
        TestLoadingFile
        TestSliceData)

foreach (TEST_NAME IN LISTS TEST_PERF_SOURCES)
    add_executable(${TEST_NAME} ${TEST_NAME}.cc ${SOURCES} ${TEST_UTILITIES})
    target_link_libraries(${TEST_NAME} ${TEST_COMMON_LIBS} ${LINK_LIBS})
endforeach ()
