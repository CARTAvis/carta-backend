# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [3.0.0-beta.1]

### Added

* Added unit test helper functions for generating FITS and HDF5 test image files.
* Introduced generic JSON object read/write/clearing for the REST API.
* Unit tests for filling spatial profiles.
* Added support for building on aarch64 Linux.
* Added directory info to file lists.
* Added support for using mipmapped datasets in HDF5 files to load downsampled data.
* Added a tile cache for loading full-resolution data from chunked HDF5 files.
* Added mip and range options to spatial profile requests.
* Added support for for LINE and POLYLINE regions.
* Added support for fits.gz images using zlib.
* Added ping pong test for spectral line query.
* Added support for boolean columns in tables.
* Added support for Stokes in statistics and histogram widgets.
* Added spatial profile support to point regions.

### Changed

* Browser argument no longer marked as experimental.
* Developer code style scripts consolidated into one.
* Improved custom browser option so that the child process behaves consistently.
* The scripting interface can request a subset of a called action's return value, avoiding possible serialization problems.
* Changed "stokes" to "polarizations" in file info.

### Fixed

* Upgraded to uWebSockets v19.2.0 to fix garbled ICD messages ([#848](https://github.com/CARTAvis/carta-backend/issues/848)).
* Fixed issues with FITS headers processed by casacore ([#460](https://github.com/CARTAvis/carta-backend/issues/460)) by parsing the FITS header or HDF5 attributes directly.
* Added missing setting keyword for the starting folder ([#857](https://github.com/CARTAvis/carta-backend/issues/857)).
* Fixed a crash ([#774](https://github.com/CARTAvis/carta-backend/issues/774)) by adding a mutex lock.
* Improve the speed of HDF5 header parsing ([#861](https://github.com/CARTAvis/carta-backend/issues/861)) by using H5Aiterate2 to iterate over HDF5 attributes.
* Added a fallback legacy heuristic for detecting that HDF5 attributes should be interpreted as boolean.
* Fixed a caching bug affecting images which were changed on disk ([#579](https://github.com/CARTAvis/carta-backend/issues/579)).
* Fixed a wrapper script issue causing an invalid frontend URL to be generated on Linux without network access.

### Security

* Use a constant-time string comparison for checking token equality.
* Added a security token to the gRPC interface.
