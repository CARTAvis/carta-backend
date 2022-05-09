
# CARTA Image Viewer (Backend)

![code coverage](https://img.shields.io/endpoint?style=plastic&url=https%3A%2F%2Fcarta.asiaa.sinica.edu.tw%2Fcoverage%2Fpercentage.json)

Backend process for simple web-based interface for viewing radio astronomy images in CASA, FITS, MIRIAD, and HDF5 formats (using the IDIA custom schema for HDF5). Unlike the conventional approach of rendering an image on the backend and sending a rendered image to the frontend client, the backend sends a compressed subset of the data, and the frontend renders the image efficiently on the GPU using WebGL and GLSL shaders. While the data is compressed using the lossy ZFP algorithm, the compression artefacts are generally much less noticeable than those cropping up from full-colour JPEG compression. While data sizes depend on compression quality used, sizes are [comparable](https://docs.google.com/spreadsheets/d/1lp1687TL0bYmbM3jGyjuPd9dYZnrAYGnLIQXWVpnmS0/edit?usp=sharing) with sizes of compressed JPEG images with a 95% quality setting, depending on the colour map used to generate the JPEG image. PNG compression is generally a factor of 2 larger than the ZFP compressed data.

## Ubuntu packages

We provide packages for Ubuntu 20.04 (Focal Fossa) and 18.04 (Bionic Beaver) in [a PPA](https://launchpad.net/~cartavis-team/+archive/ubuntu/carta) on Launchpad. All required dependencies are included in the PPA.

To add the PPA:

```shell
sudo add-apt-repository ppa:cartavis-team/carta
sudo apt-get update
```

To install the development version of the backend only (suitable for use with the CARTA controller):

```shell
sudo apt-get install carta-backend-beta
```

To install the development version of the backend and frontend (suitable for a desktop install):

```shell
sudo apt-get install carta-beta
```

The development package installs a launcher which allows CARTA to be started from the desktop environment's menu.

## Building from source

### Submodules

The protocol buffer definitions for communication between the backend and frontend, and for communication between the scripting interface and the backend. [µWebSockets](https://github.com/uNetworking/uWebSockets), which builds on [µSockets](https://github.com/uNetworking/uSockets), is used to communicate with the frontend. In order to get the right version of µWebSockets and its dependency µSockets, together with the other two submodules, a git initialisation command must be applied as follows:
```shell
git submodule update --init --recursive
```

If you use `git pull` to update an existing checkout of this repository, make sure that you also use `git submodule update` to fetch the appropriate versions of the submodule code.

### External dependencies

The backend build depends on the following libraries: 
* [casacore and casa imageanalysis](https://github.com/CARTAvis/carta-casacore); follow the build instructions.
* [zfp](https://github.com/LLNL/zfp) for data compression. The same library is used on the client, after being compiled to WebAssembly. Build and install from git repo. For best performance, build with AVX extensions.
* [Zstd](https://github.com/facebook/zstd) for data compression. Debian package `libzstd-dev`.
* [protobuf](https://developers.google.com/protocol-buffers) for client-side communication using specific message formats. Debian package `libprotobuf-dev` (> 3.0 required. Can use [PPA](https://launchpad.net/~maarten-fonville/+archive/ubuntu/protobuf) for earlier versions of Ubuntu). The Debian package `protobuf-compiler` may also be required.
* [HDF5](https://support.hdfgroup.org/HDF5/) C++ library for HDF5 support. Debian packages `libhdf5-dev` and `libhdf5-cpp-100`. By default, the serial version of the HDF5 library is targeted.
* [libcurl](https://curl.haxx.se/libcurl/) curl library for fetching data. Debian package `libcurl4-openssl-dev`.
* [libuuid](https://linux.die.net/man/3/libuuid) for generating auth tokens (if not using external authentication). Debian package `uuid-dev`.
* [pugixml](https://pugixml.org/) for parsing catalog data. Debian package: `libpugixml-dev`. On Ubuntu 16.04, build from source. On newer versions you can also build from source to save memory: use the `PUGIXML_COMPACT` and `PUGIXML_NO_XPATH` flags.
* [cfitsio](https://heasarc.gsfc.nasa.gov/fitsio/) library for I/O with FITS format data files. Debian package: `libcfitsio-dev`.
* [wcslib](https://www.gnu.org/software/gnuastro/manual/html_node/WCSLIB.html) library to handle world coordinate system. Debian package: `wcslib-dev`.

### Build

Use cmake to build:
```shell
mkdir build
cd build
cmake ..
make
```

For more detailed example commands for installing the dependencies and performing the build on specific Linux distributions, please refer to the provided [Dockerfiles](https://github.com/CARTAvis/carta-backend/tree/dev/Dockerfiles).

## Running the backend process

Command-line arguments are in the format `--arg=value` or `--arg value`. Run `carta_backend --help` for a list of options. By default, the backend will attempt to host frontend files from `../share/carta/frontend` (relative to the executable path). This can be changed with the `--frontend_folder` argument. Hosting of the frontend can be disabled with the `--no_http` argument. Token-based authentication can be disabled for debugging or development purposes with the `--debug_no_auth` argument.

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.3377984.svg)](https://doi.org/10.5281/zenodo.3377984)
