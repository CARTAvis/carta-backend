cmake_minimum_required(VERSION 3.5)
project(carta_backend)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
# Use the -march=native flags when building on the same architecture as deploying to get a slight performance
# increase when running CPU intensive tasks such as compression and down-sampling of data
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")

# use -DAuthServer:BOOL=ON to link mongodb and jsonccpp
option(AuthServer "AuthServer" OFF)

# Needed by clang-tidy and other clang tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

FIND_PACKAGE(HDF5)
FIND_PACKAGE(Protobuf REQUIRED)
FIND_PACKAGE(Threads)
INCLUDE_DIRECTORIES(${HDF5_INCLUDE_DIR})

ADD_SUBDIRECTORY(carta-protobuf)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR}/ImageData)
INCLUDE_DIRECTORIES(/usr/include/casacore)
INCLUDE_DIRECTORIES(/usr/local/include/casacode)

if (AuthServer)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_AUTH_SERVER_")
    INCLUDE_DIRECTORIES(/usr/include/libbson-1.0/)
    INCLUDE_DIRECTORIES(/usr/include/libmongoc-1.0/)
    set(LINK_LIBS
            jsoncpp
            mongoc-1.0
            bson-1.0)
    set(SOURCE_FILES
            DBConnect.cc)
endif (AuthServer)


set(LINK_LIBS
        ${LINK_LIBS}
        carta-protobuf
        ${PROTOBUF_LIBRARY}
        fmt
        uWS
        ssl
        crypto
        z
        zfp
        tbb
        casa_casa
        casa_coordinates
        casa_tables
        casa_images
        casa_lattices
        casa_fits
        casa_measures
        casa_mirlib
        casa_scimath
        casa_imageanalysis
        ${HDF5_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT})

set(SOURCE_FILES
        ${SOURCE_FILES}
        Main.cc
        Session.cc
        Frame.cc
        Compression.cc
        ImageData/Hdf5Attributes.cc
        ImageData/FileLoader.cc
        ImageData/CartaHdf5Image.cc
        FileInfoLoader.cc
        Region/Region.cc
        Region/RegionStats.cc
        Region/RegionProfiler.cc
        Region/Histogram.cc
        OnMessageTask.cc
        FileSettings.cc
        Util.cc
        FileListHandler.cc
        Tile.cc
        Ds9Parser.cc)

add_definitions(-DHAVE_HDF5)
add_executable(carta_backend ${SOURCE_FILES})
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(carta_backend uv ${LINK_LIBS})
endif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    target_link_libraries(carta_backend ${LINK_LIBS})
endif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")

# Tests
option(test "Build all tests." OFF)
if (test)
    enable_testing()
    find_package(GTest)
    include_directories(${GTEST_INCLUDE_DIRS} ${CMAKE_SOURCE_DIR})

    add_executable(testTileEncoding test/TestTileEncoding.cc)
    target_link_libraries(testTileEncoding gtest gtest_main Threads::Threads fmt)

    add_test(NAME TestTileEncoding COMMAND testTileEncoding)
endif (test)

# Install executable
install(TARGETS carta_backend DESTINATION bin)
