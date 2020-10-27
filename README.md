# CARTA Image Viewer (Server)
Server for simple web-based interface for viewing radio astronomy images in CASA, FITS, MIRIAD, and HDF5 formats (using the IDIA custom schema for HDF5). Unlike the conventional approach of rendering an image on the server and sending a rendered image to the client, the server sends a compressed subset of the data, and the client renders the image efficiently on the GPU using WebGL and GLSL shaders. While the data is compressed using the lossy ZFP algorithm, the compression artefacts are generally much less noticable than those cropping up from full-colour JPEG compression. While data sizes depend on compression quality used, sizes are [comparable](https://docs.google.com/spreadsheets/d/1lp1687TL0bYmbM3jGyjuPd9dYZnrAYGnLIQXWVpnmS0/edit?usp=sharing) with sizes of compressed JPEG images with a 95% quality setting, depending on the colour map used to generate the JPEG image. PNG compression is generally a factor of 2 larger than the ZFP compressed data.

## Building from source

### Submodules

The protocol buffer definitions for communication between the server and client, and for communication between the scripting interface and the server, reside in two git submodules that must be initialised as follows:
```
git submodule init
git submodule update
```

If you use `git pull` to update an existing checkout of this repository, make sure that you also use `git submodule update` to fetch the appropriate versions of the submodule code.

### External dependencies

The server build depends on the following libraries: 
* [casacore and casa imageanalysis](https://github.com/CARTAvis/carta-casacore); follow the build instructions.
* [zfp](https://github.com/LLNL/zfp) for data compression. The same library is used on the client, after being compiled to WebAssembly. Build and install from git repo. For best performance, build with AVX extensions.
* [Zstd](https://github.com/facebook/zstd) for data compression. Debian package `libzstd-dev`.
* [fmt](https://github.com/fmtlib/fmt) for python-style (and safe printf-style) string formatting and printing. Debian package `libfmt-dev`. On Ubuntu 16.04, build from source.
* [protobuf](https://developers.google.com/protocol-buffers) for client-side communication using specific message formats. Debian package `libprotobuf-dev` (> 3.0 required. Can use [PPA](https://launchpad.net/~maarten-fonville/+archive/ubuntu/protobuf) for earlier versions of Ubuntu). The Debian package `protobuf-compiler` may also be required.
* [HDF5](https://support.hdfgroup.org/HDF5/) C++ library for HDF5 support. Debian packages `libhdf5-dev` and `libhdf5-cpp-100`. By default, the serial version of the HDF5 library is targeted.
* [µWS](https://github.com/uNetworking/uWebSockets) for socket communication with client. Build and install from git repo.([Recommended: v0.14.8](https://github.com/uNetworking/uWebSockets/releases/tag/v0.14.8))
* [tbb](https://www.threadingbuildingblocks.org/download) Threading Building Blocks for task parallelization. Debian package `libtbb-dev`.
* [libcurl](https://curl.haxx.se/libcurl/) curl library for fetching data. Debian package `libcurl4-openssl-dev`.
* [gRPC](https://grpc.io/) for the scripting interface. Debian packages: `libprotobuf-dev protobuf-compiler libgrpc++-dev libgrpc-dev protobuf-compiler-grpc googletest`. On Ubuntu 16.04 or 18.04, use [a PPA](https://launchpad.net/~webispy/+archive/ubuntu/grpc).
* [pugixml](https://pugixml.org/) for parsing catalog data. Debian package: `libpugixml-dev`. On Ubuntu 16.04, build from source. On newer versions you can also build from source to save memory: use the `PUGIXML_COMPACT` and `PUGIXML_NO_XPATH` flags.

### Build

Use cmake to build:
```
mkdir build
cd build
cmake ..
make
```

For more detailed example commands for installing the dependencies and performing the build on specific Linux distributions, please refer to the provided [Dockerfiles](https://github.com/CARTAvis/carta-backend/tree/dev/Dockerfiles).

## Running the server

Command-line arguments are in the format arg=value.  Available arguments include:
```
--help          List version and arguments
debug           Debug level, default 0
verbose         Verbose logging, default False
perflog         Performance logging, default False
port            Set server port, default 3002
grpc_port       Set gRPC port for scripting
threads         Set thread pool count, default 4
base            Set folder for data files, default current directory
root            Set top-level folder for data files, default /
exit_after      Number of seconds to stay alive if no clients connect
```

[![Build Status](http://acdc0.asiaa.sinica.edu.tw:47565/job/carta-backend/badge/icon)](http://acdc0.asiaa.sinica.edu.tw:47565/job/carta-backend) 

[![Build Status](https://travis-ci.org/CARTAvis/carta-backend.svg?branch=master)](https://travis-ci.org/CARTAvis/carta-backend)

[![CircleCI](https://circleci.com/gh/CARTAvis/carta-backend.svg?style=svg)](https://circleci.com/gh/CARTAvis/carta-backend)

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.3377984.svg)](https://doi.org/10.5281/zenodo.3377984)
