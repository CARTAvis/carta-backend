# CARTA Image Viewer (Backend)

![code coverage](https://raw.githubusercontent.com/CARTAvis/carta-backend/refs/heads/gh-storage/badge-coverage.svg)

This is the backend component of the [Cube Analysis and Rendering Tool for Astronomy](https://cartavis.org/), a web-based application for viewing radio astronomy images in CASA, FITS, MIRIAD, and HDF5[^1] formats.

Rather than rendering each image and sending it to the frontend client (which is the conventional approach), the backend sends a compressed subset of the data, and the frontend renders the image efficiently on the GPU using WebGL and GLSL shaders.

Although the data is compressed with the lossy ZFP algorithm, the compression artefacts are generally much less noticeable than those produced by full-colour JPEG compression.

While data sizes depend on compression quality, sizes are [comparable](https://docs.google.com/spreadsheets/d/1lp1687TL0bYmbM3jGyjuPd9dYZnrAYGnLIQXWVpnmS0/edit?usp=sharing) with sizes of compressed JPEG images with a 95% quality setting (depending on the colour map used to generate the JPEG image). PNG compression is generally a factor of 2 larger than the ZFP-compressed data.

[^1] using the [custom IDIA schema](https://github.com/CARTAvis/fits2idia).

# Installation

If you are looking for releases of the CARTA application for desktop users, please refer to the [main website](https://cartavis.org/#download).

If you would like to set up CARTA in a multi-user environment, we recommend installing the [CARTA controller](https://carta-controller.readthedocs.io). We provide detailed instructions for a complete deployment of all components on supported platforms.

The rest of this document describes installation of **the backend component only** (for use with a separately installed frontend, or with the controller).

## Linux packages

We provide packages of the backend and all required dependencies for recent Ubuntu LTS releases and recent AlmaLinux releases. They should also work on equivalent distributions closely based on Ubuntu and on RHEL.

The packages install a launcher which allows CARTA to be started from the desktop environment's menu.

The beta package saves user configuration to `.carta-beta` rather than `.carta`.

### Ubuntu

```shell
sudo add-apt-repository ppa:cartavis-team/carta
sudo apt-get update

# install the latest beta version of the backend only
sudo apt-get install carta-backend-beta

# OR install the latest stable release version of the backend only
sudo apt-get install carta-backend
```

The beta and stable Ubuntu packages use the same install locations, and only one can be installed at a time.

### AlmaLinux (and equivalents)

```shell
sudo dnf install epel-release
sudo dnf install 'dnf-command(copr)'
sudo dnf copr enable cartavis/carta

# install the latest beta version of the backend only
sudo dnf install carta-backend-beta

# install the latest stable release version of the backend only
sudo dnf install carta-backend
```

The RPM beta package uses a custom install location in `/opt`, and can be installed in parallel with the stable package.

## Building the development version from source

### Obtaining the source

This repository includes several dependencies as submodules:
* [The protocol buffer definitions](carta-protobuf) for communication between the backend and frontend.
* [cxxopts](https://github.com/jarro2783/cxxopts) for parsing commandline options
* [nlohmann/json](https://github.com/nlohmann/json) for parsing JSON
* [pugixml](https://github.com/zeux/pugixml) for parsing XML
* [spdlog](https://github.com/gabime/spdlog) for logging
* [sse2neon](https://github.com/DLTcollab/sse2neon) for ARM support

Two submodules are not required for building the main source:
* [image-generator](https://github.com/idia-astro/image-generator) for creating FITS images (from the unit tests; soon to be deprecated)
* [Doxygen Awesome](https://github.com/jothepro/doxygen-awesome-css), a Doxygen stylesheet (for generating the developer documentation)

You can fetch the submodule contents when you clone the repository:
```shell
git clone --recurse-submodules https://github.com/CARTAvis/carta-backend.git
carta-backend
```

Alternatively, you can update them after cloning:
```shell
git clone https://github.com/CARTAvis/carta-backend.git
cd carta-backend
git submodule update --init --recursive
```

If you use `git pull` to update an existing checkout of this repository, make sure that you also use `git submodule update` to fetch the appropriate versions of the submodule code.

### External dependencies

The backend build depends on the following libraries (this is not an exhaustive list): 
* [casacore + casa imageanalysis](https://github.com/CARTAvis/carta-casacore). We maintain a custom version of this library.
* [zfp](https://github.com/LLNL/zfp) for data compression. The same library is used on the client, after being compiled to WebAssembly.[^1]
* [Zstd](https://github.com/facebook/zstd) for data compression.
* [protobuf](https://developers.google.com/protocol-buffers) for client-side communication using specific message formats.
* [HDF5](https://support.hdfgroup.org/HDF5/) C++ library for HDF5 support.
* [libuuid](https://linux.die.net/man/3/libuuid) for generating auth tokens (if not using external authentication).
* [cfitsio](https://heasarc.gsfc.nasa.gov/fitsio/) library for I/O with FITS format data files.
* [wcslib](https://www.gnu.org/software/gnuastro/manual/html_node/WCSLIB.html) library to handle world coordinate system.

We recommend using our [Dockerfiles](Dockerfiles) as a guideline for installing these dependencies on RPM-based and Debian-based distributions. We provide packaged versions of dependencies missing from officially supported distributions.

[^1]: If you build `zfp` from source, enable AVX extensions for the best performance.

### Build

We use `cmake` to configure the build.

```shell
mkdir build
cd build
cmake ..
make -j8
```

# Running the backend process

The packages provide a `carta` script which wraps the `carta_backend` executable. Both are available on the path. In the source build, the `carta_backend` executable can be found in the `build` directory.

Command-line arguments are in the format `--arg=value` or `--arg value`. By default, the backend will attempt to host frontend files from `../share/carta/frontend` (relative to the executable path). This can be changed with the `--frontend_folder` argument. Run `carta_backend --help` for a full list of options.

Most options can also be set permanently in a user configuration file (`~/.carta/backend.json`, or `~/.carta-beta/backend.json` for our beta packages) or a global configuration file (`/etc/carta/backend.json`). For example:
```json
{
    "$schema": "https://cartavis.github.io/schemas/preference_backend_schema_2.json",
    "verbosity": 5,
    "exit_timeout" : 0
}
```

# Developer documentation

Automatically generated Doxygen documentation can be found at [cartavis.org/carta-backend](https://cartavis.org/carta-backend/).

[![DOI](https://zenodo.org/badge/DOI/10.5281/zenodo.3377984.svg)](https://doi.org/10.5281/zenodo.3377984)
