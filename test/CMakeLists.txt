cmake_minimum_required(VERSION 3.5)
project(carta_backend_test)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Needed by clang-tidy and other clang tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Building tester options
option(testTileEncoding "Build testTileEncoding." OFF)
option(testTable "Build testTable." OFF)
option(testTimer "Build testTimer." OFF)
option(testImageMoments "Build testImageMoments." ON)

if (testTileEncoding)
    enable_testing()
    find_package(GTest)
    include_directories(${GTEST_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR})

    add_executable(testTileEncoding TestTileEncoding.cc)
    target_link_libraries(testTileEncoding gtest gtest_main Threads::Threads fmt)

    add_test(NAME TestTileEncoding COMMAND testTileEncoding)
endif ()

if (testTimer)
    enable_testing()
    find_package(GTest)
    include_directories(${GTEST_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR})

    add_executable(testTimer
            TestTimer.cc
            ../Timer/Timer.cc)
    target_link_libraries(testTimer gtest gtest_main Threads::Threads fmt)

    add_test(NAME TestTimer COMMAND testTimer)
endif ()

if (testTable)
    enable_testing()
    find_package(GTest)
    include_directories(${GTEST_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR} src)

    add_executable(test_votable
            TestVOTable.cc
            ../Timer/Timer.cc
            ../Table/Columns.cc
            ../Table/Table.cc
            ../Table/TableView.cc
            ../Table/TableController.cc)
    target_link_libraries(test_votable ${LINK_LIBS} gtest gtest_main)

    add_executable(test_fits
            TestFITS.cc
            ../Timer/Timer.cc
            ../Table/Columns.cc
            ../Table/Table.cc
            ../Table/TableView.cc
            ../Table/TableController.cc)
    target_link_libraries(test_fits ${LINK_LIBS} gtest gtest_main)

    add_test(NAME TestVOTable COMMAND test_votable)
    add_test(NAME TestFITS COMMAND test_fits)
endif()

if (testImageMoments)
    set(IMAGE_MOMENTS_LINK_LIBS ${IMAGE_MOMENTS_LINK_LIBS}
            ${OpenMP_CXX_LIBRARIES}
            ${CMAKE_THREAD_LIBS_INIT}
            ${PROTOBUF_LIBRARY}
            carta-protobuf
            casa_casa
            casa_coordinates
            casa_tables
            casa_images
            casa_lattices
            casa_fits
            casa_measures
            casa_mirlib
            casa_scimath
            casa_imageanalysis)
    set(IMAGE_MOMENTS_SOURCE_FILES ${IMAGE_MOMENTS_SOURCE_FILES}
            ${CMAKE_SOURCE_DIR}/Moment/MomentGenerator.cc
            ${CMAKE_SOURCE_DIR}/Analysis/CasacRegionManager.cc
            ${CMAKE_SOURCE_DIR}/FilesManager.cc)
    add_executable(testImageMoments TestImageMoments.cc ${IMAGE_MOMENTS_SOURCE_FILES})
    target_link_libraries(testImageMoments ${IMAGE_MOMENTS_LINK_LIBS})
    add_executable(testMomentGenerator TestMomentGenerator.cc ${IMAGE_MOMENTS_SOURCE_FILES})
    target_link_libraries(testMomentGenerator ${IMAGE_MOMENTS_LINK_LIBS})
endif ()