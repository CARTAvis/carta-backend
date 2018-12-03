# CARTA Image Viewer (Server)
Server for simple web-based interface for viewing radio astronomy images in CASA, FITS, MIRIAD, and HDF5 formats (using the IDIA custom schema for HDF5). Unlike the conventional approach of rendering an image on the server and sending a rendered image to the client, the server sends a compressed subset of the data, and the client renders the image efficiently on the GPU using WebGL and GLSL shaders. While the data is compressed using the lossy ZFP algorithm, the compression artefacts are generally much less noticable than those cropping up from full-colour JPEG compression. While data sizes depend on compression quality used, sizes are [comparable](https://docs.google.com/spreadsheets/d/1lp1687TL0bYmbM3jGyjuPd9dYZnrAYGnLIQXWVpnmS0/edit?usp=sharing) with sizes of compressed JPEG images with a 95% quality setting, depending on the colour map used to generate the JPEG image. PNG compression is generally a factor of 2 larger than the ZFP compressed data.

The protocol buffer definitions for communication between the server and client reside in a git submodule that must be initialised as follows:
```
cd carta-protobuf
git submodule init
git submodule update
git checkout master
```

Use cmake to build:
```
mkdir build
cd build
cmake ..
make
```

Command-line arguments are in the format arg=value.  Available arguments include:
```
--help       List version and arguments
debug        Debug level, default 0
verbose      Verbose logging, default False
permissions  Use a permissions file to determine directory access, default False
port         Set server port, default 3002
threads      Set thread pool count, default 4
folder       Set folder for data files, default current directory
```

## External dependencies
The server build depends on the following libraries: 
* [casacore](https://github.com/casacore/casacore) for CASA image libraries. Build and install from git repo.  casacore requires casa data (https://open-bitbucket.nrao.edu/scm/casa/casa-data.git); follow the sparse checkout instructions.
* [zfp](https://github.com/LLNL/zfp) for data compression. The same library is used on the client, after being compiled to WebAssembly. Build and install from git repo. For best performance, build with AVX extensions. 
* [fmt](https://github.com/fmtlib/fmt) for python-style (and safe printf-style) string formatting and printing. Debian package `libfmt3-dev`. 
* [protobuf](https://developers.google.com/protocol-buffers) for client-side communication using specific message formats. Debian package `libprotobuf-dev` (> 3.0 required. Can use [PPA](https://launchpad.net/~maarten-fonville/+archive/ubuntu/protobuf) for earlier versions of Ubuntu) 
* [HDF5](https://support.hdfgroup.org/HDF5/) C++ library for HDF5 support. Debian packages `libhdf5-dev` and `libhdf5-cpp-100`. By default, the serial version of the HDF5 library is targeted.
* [µWS](https://github.com/uNetworking/uWebSockets) for socket communication with client. Build and install from git repo.
* [tbb](https://www.threadingbuildingblocks.org/download) Threading Building Blocks for task parallelization.

[![Build Status](http://acdc0.asiaa.sinica.edu.tw:47565/job/nrao-carta-backend/badge/icon)](http://acdc0.asiaa.sinica.edu.tw:47565/job/nrao-carta-backend) 
