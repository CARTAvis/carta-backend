cmake_minimum_required(VERSION 3.8)
project(carta_backend)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)

# Enable OpenMP if package is found
find_package(OpenMP)
if(OPENMP_FOUND)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} ${OpenMP_C_FLAGS}")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
else()
    message(WARNING "OpenMP not found!")
endif()

# Use the -march=native flags when building on the same architecture as deploying to get a slight performance
# increase when running CPU intensive tasks such as compression and down-sampling of data. If targeting AVX-capable
# processes only, set EnableAvx to ON
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
option(EnableAvx "Enable AVX codepaths instead of SSE4" OFF)

if (EnableAvx)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4")
endif()

# use -DAuthServer:BOOL=ON to link mongodb and jsonccpp
option(AuthServer "AuthServer" OFF)

# Needed by clang-tidy and other clang tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

FIND_PACKAGE(HDF5)
FIND_PACKAGE(Protobuf REQUIRED)
FIND_PACKAGE(Threads)
FIND_PACKAGE(LibXml2 REQUIRED)
INCLUDE_DIRECTORIES(${HDF5_INCLUDE_DIR})
INCLUDE_DIRECTORIES(${LIBXML2_INCLUDE_DIR})

ADD_SUBDIRECTORY(carta-protobuf)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})
INCLUDE_DIRECTORIES(/usr/include/casacore)
INCLUDE_DIRECTORIES(/usr/local/include/casacode)

if (AuthServer)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_AUTH_SERVER_")
    INCLUDE_DIRECTORIES(/usr/include/libbson-1.0/)
    INCLUDE_DIRECTORIES(/usr/include/libmongoc-1.0/)
    set(LINK_LIBS
            jsoncpp
            mongoc-1.0
            bson-1.0
            json-c)
    set(SOURCE_FILES
            DBConnect.cc)
endif (AuthServer)

if (DisableContourCompression)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_CONTOUR_COMPRESSION_")
endif(DisableContourCompression)


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
        zstd
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
        ${OpenMP_CXX_LIBRARIES}
        ${HDF5_LIBRARIES}
        ${LIBXML2_LIBRARIES}
        ${CMAKE_THREAD_LIBS_INIT})

set(SOURCE_FILES
        ${SOURCE_FILES}
        Main.cc
        Session.cc
        Frame.cc
        DataStream/Compression.cc
        DataStream/Contouring.cc
        DataStream/Smoothing.cc
        DataStream/Tile.cc
        ImageData/Hdf5Attributes.cc
        ImageData/FileLoader.cc
        ImageData/Hdf5Loader.cc
        ImageData/CartaHdf5Image.cc
        ImageData/CartaMiriadImage.cc
        FileList/FileExtInfoLoader.cc
        FileList/FileInfoLoader.cc
        FileList/FileListHandler.cc
        FileList/FitsHduList.cc
        Region/RegionHandler.cc
        Region/RegionImportExport.cc
        Region/CrtfImportExport.cc
        Region/Ds9ImportExport.cc
        Region/Region.cc
        Catalog/VOTableParser.cc
        Catalog/VOTableCarrier.cc
        Catalog/VOTableController.cc
        OnMessageTask.cc
        FileSettings.cc
        ImageStats/StatsCalculator.cc
        ImageStats/Histogram.cc
        Util.cc)

add_definitions(-DHAVE_HDF5)
add_executable(carta_backend ${SOURCE_FILES})
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    target_link_libraries(carta_backend uv ${LINK_LIBS})
endif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    target_link_libraries(carta_backend ${LINK_LIBS})
endif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")

# Tests
option(test "Build tests." OFF)
if (test)
    add_subdirectory(test)
endif (test)
