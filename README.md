# HDF5 Image Viewer (Server)
Server for simple web-based interface for viewing HDF5 images (using the IDIA custom schema) stored on a server. Unlike the conventional approach of rendering an image on the server and sending a rendered image to the client, the server sends a compressed subset of the data, and the client renders the image efficiently on the GPU using WebGL and GLSL shaders. While the data is compressed using the lossy ZFP algorithm, the compression artefacts are generally much less noticable than those cropping up from full-colour JPEG compression. While data sizes depend on compression quality used, sizes are [comparable](https://docs.google.com/spreadsheets/d/1lp1687TL0bYmbM3jGyjuPd9dYZnrAYGnLIQXWVpnmS0/edit?usp=sharing) with sizes of compressed JPEG images with a 95% quality setting, depending on the colour map used to generate the JPEG image. PNG compression is generally a factor of 2 larger than the ZFP compressed data.

## External dependencies
The server build depends on the following libraries: 
* [zfp](https://github.com/LLNL/zfp) for data compression. The same library is used on the client, after being compiled to WebAssembly. Build and install from git repo.
* [fmt](https://github.com/fmtlib/fmt) for python-style (and safe printf-style) string formating and printing. Debian package `libfmt3-dev`. 
* [RapidJSON](https://github.com/Tencent/rapidjson) for JSON support. Included in this repo under `rapidjson`, as the Debian package is out of date.
* [HighFive](https://github.com/BlueBrain/HighFive) as a high level interface to HDF5. Build and install from git repo.
* [HDF5](https://support.hdfgroup.org/HDF5/) for low level HDF5 support (required by HighFive). Debian package `libhdf5-dev`.
* [µWS](https://github.com/uNetworking/uWebSockets) for socket communication with client. Build and install from git repo.
* boost [uuid](http://www.boost.org/doc/libs/release/libs/uuid) and [multi-array](http://www.boost.org/doc/libs/release/libs/multi_array/) modules (Both easily installed through `apt`)

Use cmake to build. Currently, the server looks for HDF5 files in the `$HOME` directory. 
