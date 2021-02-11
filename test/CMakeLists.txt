cmake_minimum_required(VERSION 3.5)
project(carta_backend_test)

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
file(GLOB_RECURSE TEST_SOURCES LIST_DIRECTORIES false *.h *.cc *.tcc)
set(SOURCES
        ../src/Session.cc
        ../src/Frame.cc
        ../src/Logger/Logger.cc
        ../src/DataStream/Compression.cc
        ../src/DataStream/Contouring.cc
        ../src/DataStream/Smoothing.cc
        ../src/DataStream/Tile.cc
        ../src/FileList/FileExtInfoLoader.cc
        ../src/FileList/FileInfoLoader.cc
        ../src/FileList/FileListHandler.cc
        ../src/FileList/FitsHduList.cc
        ../src/GrpcServer/CartaGrpcService.cc
        ../src/ImageData/Hdf5Attributes.cc
        ../src/ImageData/FileLoader.cc
        ../src/ImageData/Hdf5Loader.cc
        ../src/ImageData/CartaHdf5Image.cc
        ../src/ImageData/CartaMiriadImage.cc
        ../src/Region/RegionHandler.cc
        ../src/Region/RegionImportExport.cc
        ../src/Region/CrtfImportExport.cc
        ../src/Region/Ds9ImportExport.cc
        ../src/Region/Region.cc
        ../src/ImageStats/StatsCalculator.cc
        ../src/ImageStats/Histogram.cc
        ../src/SpectralLine/SpectralLineCrawler.cc
        ../src/Table/Columns.cc
        ../src/Table/Table.cc
        ../src/Table/TableView.cc
        ../src/Table/TableController.cc
        ../src/Moment/MomentGenerator.cc
        ../src/Timer/Timer.cc
        ../src/SessionManager/ProgramSettings.cc
        ../src/OnMessageTask.cc
        ../src/FileSettings.cc
        ../src/Util.cc
        ../src/Threading.cc
        ../src/SimpleFrontendServer/SimpleFrontendServer.cc)

add_executable(${BINARY} ${SOURCES} ${TEST_SOURCES})
add_test(NAME ${BINARY} COMMAND ${BINARY})
target_link_libraries(${BINARY}
        ${TEST_COMMON_LIBS}
        ${LINK_LIBS})