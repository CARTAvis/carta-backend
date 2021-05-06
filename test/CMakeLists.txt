cmake_minimum_required(VERSION 3.10)
project(carta_backend_test)

add_definitions(-DCARTA_USER_FOLDER_PREFIX=".carta-unit-tests")

# Performance tests should not be run by default, since they need to be run in isolation
option(performance_tests "Build performance tests." OFF)
if (performance_tests)
    add_definitions(-DCOMPILE_PERFORMANCE_TESTS)
endif ()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Needed by clang-tidy and other clang tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(TEST_COMMON_LIBS gtest gtest_main)

enable_testing()
find_package(GTest)
include_directories(${GTEST_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR})

set(BINARY ${CMAKE_PROJECT_NAME}_tests)
set(TEST_SOURCES
        TestBlockSmooth.cc
        TestFitsTable.cc
        TestFitsImage.cc
        TestHistogram.cc
        TestMain.cc
        TestMoment.cc
        TestProgramSettings.cc
        TestTileEncoding.cc
        TestTimer.cc
        TestUtil.cc
        TestRestApi.cc
        TestVoTable.cc)

# Add all the sources in the main project and remove Main.cc
foreach(src_file ${SOURCE_FILES})
    list(APPEND SOURCES ../${src_file})
endforeach()
list(REMOVE_ITEM SOURCES REMOVE_ITEM ../src/Main.cc)

# Copy test data to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_executable(${BINARY} ${SOURCES} ${TEST_SOURCES})
add_test(NAME ${BINARY} COMMAND ${BINARY})
target_link_libraries(${BINARY}
        ${TEST_COMMON_LIBS}
        ${LINK_LIBS})

