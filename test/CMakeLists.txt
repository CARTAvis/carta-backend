cmake_minimum_required(VERSION 3.10)
include(ExternalProject)
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
include_directories(${GTEST_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR})

find_package(HDF5 REQUIRED COMPONENTS CXX)
if (HDF5_FOUND)
    include_directories(${HDF5_INCLUDE_DIR})
    set(LINK_LIBS ${LINK_LIBS} ${HDF5_LIBRARIES})
else ()
    message(FATAL_ERROR "Could not find HDF5.")
endif ()

set(BINARY ${CMAKE_PROJECT_NAME}_tests)
set(TEST_SOURCES
        CommonTestUtilities.cc
        ImageData/TestFitsUtil.cc
        ImageData/TestZarrStore.cc
        ImageData/TestZarrUtil.cc
        TestBlockSmooth.cc
        TestContour.cc
        TestCursorSpatialProfiles.cc
        TestExprImage.cc
        TestFileInfo.cc
        TestFileList.cc
        TestFitsImage.cc
        TestFitsTable.cc
        TestHdf5Attributes.cc
        TestHdf5Image.cc
        TestHistogram.cc
        TestImageFitting.cc
        TestMain.cc
        TestMoment.cc
        TestNormalizedUnits.cc
        TestProgramSettings.cc
        TestPvGenerator.cc
        TestRegion.cc
        TestRegionHistogram.cc
        TestRegionImportExport.cc
        TestRegionMatched.cc
        TestRegionSpatialProfiles.cc
        TestRegionSpectralProfiles.cc
        TestRegionStats.cc
        TestRestApi.cc
        TestTileEncoding.cc
        TestVoTable.cc
        Util/TestUtilCasacore.cc
        Util/TestUtilFile.cc
        Util/TestUtilImage.cc
        Util/TestUtilMemory.cc
        Util/TestUtilString.cc)

# Add all the sources in the main project and remove Main.cc
foreach(src_file ${SOURCE_FILES})
    list(APPEND SOURCES ../${src_file})
endforeach()
list(REMOVE_ITEM SOURCES REMOVE_ITEM ../src/Main/Main.cc)

# Copy test data to build directory
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/data DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_executable(${BINARY} ${SOURCES} ${TEST_SOURCES})
add_test(NAME ${BINARY} COMMAND ${BINARY})
target_link_libraries(${BINARY}
        ${TEST_COMMON_LIBS}
        ${LINK_LIBS})
