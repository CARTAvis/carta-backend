cmake_minimum_required(VERSION 3.5)
project(carta_backend_test)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

# Needed by clang-tidy and other clang tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Building tester options
option(testTileEncoding "Build testTileEncoding." OFF)
option(testVOTableController "Build testVOTableController." ON)

if (testTileEncoding)
    enable_testing()
    find_package(GTest)
    include_directories(${GTEST_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR})

    add_executable(testTileEncoding TestTileEncoding.cc)
    target_link_libraries(testTileEncoding gtest gtest_main Threads::Threads fmt)

    add_test(NAME TestTileEncoding COMMAND testTileEncoding)
endif ()

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
