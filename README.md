# HDF5 Image Viewer

A simple web-based interface for viewing HDF5 images (using the IDIA custom schema) stored on a server. Unlike the conventional approach of rendering an image on the server and sending a rendered image to the client, the server sends a compressed subset of the data, and the client renders the image efficiently on the GPU using WebGL and GLSL shaders. While the data is compressed using the lossy ZFP algorithm, the compression artefacts are generally much less noticable than those cropping up from full-colour JPEG compression. While data sizes depend on compression quality used, sizes are [comparable](https://docs.google.com/spreadsheets/d/1lp1687TL0bYmbM3jGyjuPd9dYZnrAYGnLIQXWVpnmS0/edit?usp=sharing) with sizes of compressed JPEG images with a 95% quality setting, depending on the colour map used to generate the JPEG image. PNG compression is generally a factor of 2 larger than the ZFP compressed data.

## Outline
The server code is in the main directory. The header-only RapidJSON package is used for JSON parsing and construction, and is contained in the `rapidjson` folder. 

Frontend code is contained in the `frontend` directory, but will be migrated to a separate repository soon. WebAssembly is used for some client-side functions. The c source code for this (along with the ZFP library) is stored on the `webasm` folder. Compiling to WebAssembly requires the [emscripten compiler](http://kripken.github.io/emscripten-site/docs/getting_started/downloads.html)

## External dependencies

The server build depends on the following libraries: 
* [zfp](https://github.com/LLNL/zfp) for data compression. The same library is used on the client, after being compiled to WebAssembly. Build and install from git repo.
* [fmt](https://github.com/fmtlib/fmt) for python-style (and safe printf-style) string formating and printing. Debian package `libfmt3-dev`. 
* [RapidJSON](https://github.com/Tencent/rapidjson) for JSON support. Included in this repo under `rapidjson`, as the Debian package is out of date.
* [HighFive](https://github.com/BlueBrain/HighFive) as a high level interface to HDF5. Build and install from git repo.
* [HDF5](https://support.hdfgroup.org/HDF5/) for low level HDF5 support (required by HighFive). Debian package `libhdf5-dev`.
* [µWS](https://github.com/uNetworking/uWebSockets) for socket communication with client. Build and install from git repo.
* boost [uuid](www.boost.org/doc/libs/release/libs/uuid) and [multi-array](http://www.boost.org/doc/libs/release/libs/multi_array/) modules (Both easily installed through `apt`)

Use cmake to build. Client can be run using the `emrun` command included in the emscripten SDK. Currently, the server looks for HDF5 files in the `$HOME` directory. 

## Client interface

The client interface is extremely basic at this point, and purely used for testing the server backend features, and rendering performance.  Files can be loaded by clicking the "Load File" button. The compression level can be adjusted between 4 and 31. Values outside this range will result in an uncompressed dataset being sent from the server. 
The band slider selects which XY slice is loaded.  

The lowest value corresponds to the average image dataset. 
The different zoom presets are accesible through buttons, but you can also zoom in and out using the mouse scroll wheel, and can zoom to a specific region by holding CTRL and click-dragging your mouse to select a region.  

There are two ways to pan the image. Firstly, left clicking and dragging without pressing CTRL pans the image (with the new region being loaded when the mouse button is released). Secondly, middle-clicking on a point in the image will recenter the image on that point. Holding SHIFT while middle clicking will limit the panning to one axis only.  

Moving the mouse over the image will adjust the data cursor position, which will update the X and Y profile graphs beneath the image. Pressing SPACE will freeze the cursor in its current position, but this a pretty buggy feature at the moment. The histogram shows the distribution of pixel values for the current band.

The colour map can be changed through the dropdown menu. By default, the colour map is limited to the 99.5% percentile range, but a different percentile can be chosen using the dropdown menu. A custom max and min bound can be chosen by using the sliders.  

## Client-side and server-side functionality

As we are sending (compressed) raw data from the server to the client, the following do not require any data requests from the server, and are done purely on the client, resulting in a zero-latency experience for these features _(in comparison to alternative viewers such as CARTA or CyberSKA, which send compressed images rather than raw data)_:
* Changing colour maps
* Changing percentile range or custom min/max bounds
* Updating the cursor information text
* Updating the X and Y profile graphs

  