# HDF5 Image Viewer (Server)
Server for simple web-based interface for viewing HDF5 images (using the IDIA custom schema) stored on a server. Unlike the conventional approach of rendering an image on the server and sending a rendered image to the client, the server sends a compressed subset of the data, and the client renders the image efficiently on the GPU using WebGL and GLSL shaders. While the data is compressed using the lossy ZFP algorithm, the compression artefacts are generally much less noticable than those cropping up from full-colour JPEG compression. While data sizes depend on compression quality used, sizes are [comparable](https://docs.google.com/spreadsheets/d/1lp1687TL0bYmbM3jGyjuPd9dYZnrAYGnLIQXWVpnmS0/edit?usp=sharing) with sizes of compressed JPEG images with a 95% quality setting, depending on the colour map used to generate the JPEG image. PNG compression is generally a factor of 2 larger than the ZFP compressed data.

## External dependencies
The server build depends on the following libraries: 
* [zfp](https://github.com/LLNL/zfp) for data compression. The same library is used on the client, after being compiled to WebAssembly. Build and install from git repo. For best performance, build with AVX extensions. 
* [fmt](https://github.com/fmtlib/fmt) for python-style (and safe printf-style) string formating and printing. Debian package `libfmt3-dev`. 
* [protobuf](https://developers.google.com/protocol-buffers) for client-side communication using specific message formats. Debian package `libprotobuf-dev` (> 3.0 required. Can use [PPA](https://launchpad.net/~maarten-fonville/+archive/ubuntu/protobuf) for earlier versions of Ubuntu) 
* [HDF5](https://support.hdfgroup.org/HDF5/) C++ library for HDF5 support. Debian packages `libhdf5-dev` and `libhdf5-cpp-100`. By default, the serial version of the HDF5 library is targeted.
* [µWS](https://github.com/uNetworking/uWebSockets) for socket communication with client. Build and install from git repo.
* boost [filesystem](https://www.boost.org/doc/libs/release/libs/filesystem), [uuid](http://www.boost.org/doc/libs/release/libs/uuid) and [program_options](http://www.boost.org/doc/libs/release/libs/program_options) modules (All easily installed through `apt`)

Use cmake to build. Currently, the server looks for HDF5 files in the `$HOME` directory. 
