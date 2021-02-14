cmake_minimum_required(VERSION 3.10)
project(carta_backend)

include_directories(src)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_INCLUDE_DIRECTORIES_BEFORE ON)

set(CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/install)

include(uWebSockets)

# Enable OpenMP if package is found
find_package(OpenMP)
if (OPENMP_CXX_FOUND)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
else ()
    message(WARNING "OpenMP not found!")
endif ()

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

# Needed by clang-tidy and other clang tools
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

FIND_PACKAGE(HDF5)
FIND_PACKAGE(Protobuf REQUIRED)
FIND_PACKAGE(Threads)
FIND_PACKAGE(spdlog REQUIRED)
INCLUDE_DIRECTORIES(${HDF5_INCLUDE_DIR})

ADD_SUBDIRECTORY(carta-protobuf)
ADD_SUBDIRECTORY(carta-scripting-grpc)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR})
INCLUDE_DIRECTORIES(/usr/include/casacore /usr/include/casacode)
INCLUDE_DIRECTORIES(/usr/local/include/casacore /usr/local/include/casacode)

if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    SET(CMAKE_C_ARCHIVE_CREATE "<CMAKE_AR> Scr <TARGET> <LINK_FLAGS> <OBJECTS>")
    SET(CMAKE_CXX_ARCHIVE_CREATE "<CMAKE_AR> Scr <TARGET> <LINK_FLAGS> <OBJECTS>")
    SET(CMAKE_C_ARCHIVE_FINISH "<CMAKE_RANLIB> -no_warning_for_no_symbols -c <TARGET>")
    SET(CMAKE_CXX_ARCHIVE_FINISH "<CMAKE_RANLIB> -no_warning_for_no_symbols -c <TARGET>")
    INCLUDE_DIRECTORIES(/usr/local/include)
    INCLUDE_DIRECTORIES(/usr/local/opt/openssl/include)
    INCLUDE_DIRECTORIES(/usr/local/opt/libomp/include)
    LINK_DIRECTORIES(/usr/local/lib /usr/local/opt)
endif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")

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

# Include uWebSockets headers and build the uSockets lib
install_uWebSockets()

set(LINK_LIBS
        ${LINK_LIBS}
        carta-protobuf
        carta-scripting-grpc
        ${PROTOBUF_LIBRARY}
        fmt
        uSockets
        uuid
        z
        zfp
        zstd
        tbb
        cfitsio
        pugixml
        curl
        spdlog
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
        src/Main.cc
        src/Session.cc
        src/Frame.cc
        src/Logger/Logger.cc
        src/DataStream/Compression.cc
        src/DataStream/Contouring.cc
        src/DataStream/Smoothing.cc
        src/DataStream/Tile.cc
        src/FileList/FileExtInfoLoader.cc
        src/FileList/FileInfoLoader.cc
        src/FileList/FileListHandler.cc
        src/FileList/FitsHduList.cc
        src/GrpcServer/CartaGrpcService.cc
        src/ImageData/Hdf5Attributes.cc
        src/ImageData/FileLoader.cc
        src/ImageData/Hdf5Loader.cc
        src/ImageData/CartaHdf5Image.cc
        src/ImageData/CartaMiriadImage.cc
        src/Region/RegionHandler.cc
        src/Region/RegionImportExport.cc
        src/Region/CrtfImportExport.cc
        src/Region/Ds9ImportExport.cc
        src/Region/Region.cc
        src/ImageStats/StatsCalculator.cc
        src/ImageStats/Histogram.cc
        src/SpectralLine/SpectralLineCrawler.cc
        src/Table/Columns.cc
        src/Table/Table.cc
        src/Table/TableView.cc
        src/Table/TableController.cc
        src/Moment/MomentGenerator.cc
        src/Timer/Timer.cc
        src/OnMessageTask.cc
        src/FileSettings.cc
        src/Util.cc
        src/Threading.cc
        src/SimpleFrontendServer/SimpleFrontendServer.cc)

add_definitions(-DHAVE_HDF5)
add_executable(carta_backend ${SOURCE_FILES})
if (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
    list(REMOVE_ITEM LINK_LIBS uuid)
    target_link_libraries(carta_backend uv ${LINK_LIBS})
endif (${CMAKE_SYSTEM_NAME} MATCHES "Darwin")
if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    target_link_libraries(carta_backend ${LINK_LIBS})
endif (${CMAKE_SYSTEM_NAME} MATCHES "Linux")

install(TARGETS carta_backend
    RUNTIME DESTINATION bin)

# Tests
option(test "Build tests." OFF)
if (test)
    add_subdirectory(test)
endif ()
