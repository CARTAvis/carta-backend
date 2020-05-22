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
option(testTable "Build testTable." ON)
option(testTimer "Build testTimer." ON)
option(testVOTableController "Build testVOTableController." OFF)

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
            ../Table/Columns.cc
            ../Table/Table.cc
            ../Table/TableView.cc
            ../Table/TableController.cc)
    target_link_libraries(test_votable ${LINK_LIBS} gtest gtest_main)

    add_executable(test_fits
            TestFITS.cc
            ../Table/Columns.cc
            ../Table/Table.cc
            ../Table/TableView.cc
            ../Table/TableController.cc)
    target_link_libraries(test_fits ${LINK_LIBS} gtest gtest_main)

    add_test(NAME TestVOTable COMMAND test_votable)
    add_test(NAME TestFITS COMMAND test_fits)
endif()

if (testVOTableController)
    set(CATALOG_LINK_LIBS
            ${CATALOG_LINK_LIBS}
            ${LIBXML2_LIBRARIES}
            ${PROTOBUF_LIBRARY}
            carta-protobuf)
    set(CATALOG_SOURCE_FILES ${CATALOG_SOURCE_FILES}
            ${CMAKE_SOURCE_DIR}/Catalog/VOTableParser.cc
            ${CMAKE_SOURCE_DIR}/Catalog/VOTableCarrier.cc
            ${CMAKE_SOURCE_DIR}/Catalog/VOTableController.cc)
    add_executable(testVOTableController TestVOTableController.cc ${CATALOG_SOURCE_FILES})
    target_link_libraries(testVOTableController ${CATALOG_LINK_LIBS})
    add_executable(testVOTableParser TestVOTableParser.cc ${CATALOG_SOURCE_FILES})
    target_link_libraries(testVOTableParser ${CATALOG_LINK_LIBS})
endif ()
