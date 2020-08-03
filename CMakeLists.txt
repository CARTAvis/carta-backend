cmake_minimum_required(VERSION 3.10)
project(carta_backend)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)

# Enable OpenMP if package is found
find_package(OpenMP)
if (OPENMP_CXX_FOUND)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
else ()
    message(WARNING "OpenMP not found!")
endif ()

# Enable parallel GCC algorithms (needed until there is relable support for C++17 from compilers)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_GLIBCXX_PARALLEL")


# Use the -march=native flags when building on the same architecture as deploying to get a slight performance
# increase when running CPU intensive tasks such as compression and down-sampling of data. If targeting AVX-capable
# processes only, set EnableAvx to ON
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -march=native")
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -march=native")
option(EnableAvx "Enable AVX codepaths instead of SSE4" OFF)

if (EnableAvx)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mavx")
else ()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4")
endif ()

# use -DAuthServer:BOOL=ON to link mongodb and jsonccpp
option(AuthServer "AuthServer" OFF)

# Needed by clang-tidy and other clang tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

FIND_PACKAGE(HDF5)
FIND_PACKAGE(Protobuf REQUIRED)
FIND_PACKAGE(Threads)
INCLUDE_DIRECTORIES(${HDF5_INCLUDE_DIR})
INCLUDE_DIRECTORIES(${LIBXML2_INCLUDE_DIR})

ADD_SUBDIRECTORY(carta-protobuf)
ADD_SUBDIRECTORY(carta-scripting-grpc)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})
INCLUDE_DIRECTORIES(/usr/include/casacore /usr/include/casacode)
INCLUDE_DIRECTORIES(/usr/local/include/casacore /usr/local/include/casacode)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    INCLUDE_DIRECTORIES(/usr/local/include)
    INCLUDE_DIRECTORIES(/usr/local/opt/openssl/include)
    INCLUDE_DIRECTORIES(/usr/local/opt/libomp/include)
    LINK_DIRECTORIES(/usr/local/lib /usr/local/opt)
endif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")

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

FIND_PACKAGE(gRPC QUIET)
if (gRPC_FOUND)
    set(LINK_LIBS
            ${LINK_LIBS}
            gRPC::grpc++
            )
    message(STATUS "gRPC found using FIND_PACKAGE")
else ()
    message(STATUS "gRPC not found using FIND_PACKAGE, using pkg-config instead")
    FIND_PACKAGE(PkgConfig REQUIRED)
    PKG_SEARCH_MODULE(GRPC REQUIRED grpc)
    if (GRPC_FOUND)
        message(STATUS "gRPC found using pkg-config")
        set(LINK_LIBS
                ${LINK_LIBS}
                grpc
                grpc++
                )
    else ()
        message(FATAL_ERROR "Could not import gRPC library")
    endif ()
endif ()

if (DisableContourCompression)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_DISABLE_CONTOUR_COMPRESSION_")
endif (DisableContourCompression)

option(UseBoostFilesystem "UseBoostFilesystem" OFF)

if (UseBoostFilesystem)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_BOOST_FILESYSTEM_")
    find_package(Boost 1.56 REQUIRED COMPONENTS filesystem)
    set(LINK_LIBS ${LINK_LIBS} Boost::filesystem)
elseif (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    set(LINK_LIBS ${LINK_LIBS} stdc++fs)
endif ()


set(LINK_LIBS
        ${LINK_LIBS}
        carta-protobuf
        carta-scripting-grpc
        ${PROTOBUF_LIBRARY}
        fmt
        uWS
        ssl
        crypto
        z
        zfp
        zstd
        tbb
        cfitsio
        pugixml
        curl
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
        FileList/FileExtInfoLoader.cc
        FileList/FileInfoLoader.cc
        FileList/FileListHandler.cc
        FileList/FitsHduList.cc
        GrpcServer/CartaGrpcService.cc
        ImageData/Hdf5Attributes.cc
        ImageData/FileLoader.cc
        ImageData/Hdf5Loader.cc
        ImageData/CartaHdf5Image.cc
        ImageData/CartaMiriadImage.cc
        Region/RegionHandler.cc
        Region/RegionImportExport.cc
        Region/CrtfImportExport.cc
        Region/Ds9ImportExport.cc
        Region/Region.cc
        ImageStats/StatsCalculator.cc
        ImageStats/Histogram.cc
        SpectralLine/SpectralLineCrawler.cc
        Table/Columns.cc
        Table/Table.cc
        Table/TableView.cc
        Table/TableController.cc
        Timer/Timer.cc
        OnMessageTask.cc
        FileSettings.cc
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
endif ()
