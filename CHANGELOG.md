# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
* Added support for image fitting with field of view ([#150](https://github.com/CARTAvis/carta-backend/issues/150)).
* List frequency and velocity in file info for single channel image ([#1152](https://github.com/CARTAvis/carta-backend/issues/1152)).

### Changed
* Enhanced image fitting performance by switching the solver from qr to cholesky ([#1114](https://github.com/CARTAvis/carta-backend/pull/1114)).
* Made HTTP server return a different error code for disabled features ([#1115](https://github.com/CARTAvis/carta-backend/issues/1115)).
* Removed Splatalogue interaction from backend codebase and removed dependency on libcurl ([#994](https://github.com/cartavis/carta-backend/issues/994)).
* Use wrappers to construct protocol buffer messages where possible ([#960](https://github.com/CARTAvis/carta-backend/issues/960)).

### Fixed
* Stopped calculating per-cube histogram unnecessarily when switching to a new Stokes value ([#1013](https://github.com/CARTAvis/carta-backend/issues/1013)).
* Ensured that HTTP server returns error codes correctly ([#1011](https://github.com/CARTAvis/carta-backend/issues/1011)).
* Fixed crash problems for compressed FITS files ([#999](https://github.com/CARTAvis/carta-backend/issues/999) and [#1014](https://github.com/CARTAvis/carta-backend/issues/1014)).
* Fixed the incorrect STD for images with large pixel values ([#1069](https://github.com/CARTAvis/carta-backend/issues/1069)).
* Fixed incorrect spectral profiles for computed stokes ([#1122](https://github.com/CARTAvis/carta-backend/issues/1122)). 
* Fixed the problem of recognizing FITS gzip files from ALMA Science Archive ([#1130](https://github.com/CARTAvis/carta-backend/issues/1130)).
* Fixed slow loading of FITS image with large number of HISTORY headers ([#1063](https://github.com/CARTAvis/carta-backend/issues/1063)).
* Fixed the DS9 import bug with region properties ([#1129](https://github.com/CARTAvis/carta-backend/issues/1129)).
* Fixed incorrect pixel number when fitting image with nan pixels ([#1128](https://github.com/CARTAvis/carta-backend/pull/1128)).
* Fixed errors on loading images via LEL ([#1144](https://github.com/CARTAvis/carta-backend/issues/1144)).
* Fixed the DS9 import bug with no header line ([#1064](https://github.com/CARTAvis/carta-backend/issues/1064)).
* Fixed incorrect matched region pixel count ([#1108](https://github.com/CARTAvis/carta-backend/issues/1108)).
* Fixed the getstat error on generated image ([#1148](https://github.com/CARTAvis/carta-backend/issues/1148)).
* Fixed file info hang when a CASA image is locked ([#578](https://github.com/CARTAvis/carta-backend/pull/578)).
* Fixed region export failure when no write permission ([#1133](https://github.com/CARTAvis/carta-backend/pull/1133)).

## [3.0.0-beta.3]

### Changed
* Removed gRPC service and moved scripting interface to HTTP server ([#1022](https://github.com/CARTAvis/carta-backend/pull/1022)).
* Added more fine-grained commandline flags to enable and disable functions of the HTTP server.
* Optimised performance of image data cache ([#967](https://github.com/CARTAvis/carta-backend/issues/967)).
* Added exit on timeout flag to Linux desktop launcher ([#989](https://github.com/CARTAvis/carta-backend/issues/989)).
* Separated stdout and stderr logging ([#869](https://github.com/CARTAvis/carta-backend/issues/869)).

### Added
* Added moment map generation information to the header of generated images ([#1024](https://github.com/CARTAvis/carta-backend/issues/1024)).
* Added support for creating CASA LEL images dynamically ([#655](https://github.com/CARTAvis/carta-backend/issues/665)).
* Added spatial and spectral range information to the file browser ([#845](https://github.com/CARTAvis/carta-backend/issues/845)).
* Added computed Stokes images and supported the analysis (profiles, contours, statistics, etc.) of them ([#433](https://github.com/CARTAvis/carta-backend/issues/433)).
* Added data type to file info and open complex image with amplitude expression ([#520](https://github.com/CARTAvis/carta-backend/issues/520)).
* Added ability to set a custom rest frequency for saving subimages. ([#918](https://github.com/CARTAvis/carta-backend/issues/918)).
* Added image fitter for multiple 2D Gaussian component fitting ([#150](https://github.com/CARTAvis/carta-backend/issues/150)).
* Added support for a custom carta-casacore script for updating casacore data in a local user directory ([#961](https://github.com/CARTAvis/carta-backend/issues/961)).
* Added support of vector field (polarization intensity/angle) calculations ([#1002](https://github.com/CARTAvis/carta-backend/issues/1002)).
* Added spatial profiles for line/polyline regions ([#796](https://github.com/CARTAvis/carta-backend/issues/796)).

### Fixed
* Fixed problem with backend hanging rather than exiting after all sessions have disconnected ([#988](https://github.com/CARTAvis/carta-backend/pull/988)).
* Fixed handling of NaN values in downsampled spatial profiles ([#987](https://github.com/CARTAvis/carta-backend/pull/987)).
* Removed file id repetition in generated moments or PV images ([#1003](https://github.com/CARTAvis/carta-backend/pull/1003)).
* Fixed linear coordinate conversion for matched images ([#982](https://github.com/CARTAvis/carta-backend/issues/982)).
* Fixed beam position angle unit displayed for CASA images ([#1025](https://github.com/CARTAvis/carta-backend/issues/1025)).
* Fixed crash when saving certain PV images ([#1009](https://github.com/CARTAvis/carta-backend/issues/1009)).
* Ensured that sessions are deleted correctly ([#1048](https://github.com/CARTAvis/carta-backend/pull/1048)).
* Ensured that sessions are correctly assigned unique IDs ([#1049](https://github.com/CARTAvis/carta-backend/pull/1049)).
* Corrected spatial range calculation to account for rotation ([#1050](https://github.com/CARTAvis/carta-backend/issues/1050)).
* Fixed a bug in Stokes animation when playing backwards ([#1053](https://github.com/CARTAvis/carta-backend/pull/1053)).
* Fixed inconsistent behaviour of top level folder setting ([#1089](https://github.com/CARTAvis/carta-backend/issues/1089)).
* Fixed CRTF export bug for labelpos ([#1012](https://github.com/CARTAvis/carta-backend/issues/1012)).
* Fixed DS9 import bug for region parameter with no unit ([#1101](https://github.com/CARTAvis/carta-backend/issues/1101)).
* Fixed offset in center of offset axis of generated PV image ([#1038](https://github.com/CARTAvis/carta-backend/issues/1038)).
* Fixed CARTA FITS image pixel mask for floating-point images.
* Fixed various memory leaks, and several memory errors uncovered by address sanitization.

## [3.0.0-beta.2]

### Changed
* Removed the dependency on Intel TBB library by replacing the TBB mutexes with ones based on standard c++ ones.
* Replaced shared image loader pointer in the session class with a cache for multiple loaders, to avoid concurrency bugs when loading multiple images simultaneously.

### Added
* Added ability to guess file type by extension, rather than content ([#1](https://github.com/CARTAvis/carta/issues/1)).
* Added support for circular/linear polarizations when generating hypercubes ([#942](https://github.com/CARTAvis/carta-backend/issues/942)).
* Added PV image generator for line regions ([#794](https://github.com/CARTAvis/carta-backend/issues/794)).

### Fixed

* Fixed hard crash when attempting to read files within a read-protected directory ([#945](https://github.com/CARTAvis/carta-backend/issues/945)).
* Fixed region histograms for moment images ([#906](https://github.com/CARTAvis/carta-backend/issues/906)).
* Fixed bug of duplicate histogram calculation ([#905](https://github.com/CARTAvis/carta-backend/pull/905)).
* Fixed issues when reading stokes information from the header ([#942](https://github.com/CARTAvis/carta-backend/issues/942)).
* Fixed restoring beam set from HISTORY ([#935](https://github.com/CARTAvis/carta-backend/issues/935)).
* Fixed FITS header bug for dropped keyword index ([#912](https://github.com/CARTAvis/carta-backend/issues/912)).
* Fixed DS9 export bug by increasing precision for degrees ([#953](https://github.com/CARTAvis/carta-backend/issues/953)).

## [3.0.0-beta.1b]

### Fixed

* Fixed control points record for polygon / line / polyline ([#891](https://github.com/CARTAvis/carta-backend/issues/891)).
* Fixed bug causing cube histogram to be generated even if it was available in an HDF5 file ([#899](https://github.com/CARTAvis/carta-backend/issues/899)).
* Fixed bug of mis:wq
sing cursor values for matched images ([#900](https://github.com/CARTAvis/carta-backend/issues/900)).

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
* Added support for LINE and POLYLINE regions.
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
