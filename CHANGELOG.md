# Changelog
All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Fixed
* Fixed crash when loading non-image HDU by URL ([#1365](https://github.com/CARTAvis/carta-backend/issues/1365)).
* Fix crash when parsing FITS header long value ([#1366](https://github.com/CARTAvis/carta-backend/issues/1366)).
* Fix incorrect parsing of SPECSYS value for ATCA FITS header ([#1375](https://github.com/CARTAvis/carta-backend/issues/1375)).
* Fix hdf5 image distortion after animation stops ([#1368](https://github.com/CARTAvis/carta-backend/issues/1368)).
* Fix save image bug which could cause directory overwrite or deletion ([#1377](https://github.com/CARTAvis/carta-backend/issues/1377)).

### Changed
* Move the loader cache to separate files ([#1021](https://github.com/CARTAvis/carta-backend/issues/1021)).
* Improve the code style in HTTP server ([#1260](https://github.com/CARTAvis/carta-backend/issues/1260)).
* Remove program settings equality operators ([#1001](https://github.com/CARTAvis/carta-backend/issues/1001)).
* Normalize the style of guard names in header files ([#1023](https://github.com/CARTAvis/carta-backend/issues/1023)).
* Improved file IDs for generated images ([#1224](https://github.com/CARTAvis/carta-frontend/issues/1224)).
* Store global settings in a singleton class ([#1302](https://github.com/CARTAvis/carta-backend/issues/1302)).
* Move Region code to RegionConverter for maintenance and performance ([#1347](https://github.com/CARTAvis/carta-backend/issues/1347)).
* Improve performance of region spatial profiles and PV image generation ([#1339](https://github.com/CARTAvis/carta-backend/issues/1339)).

## [4.1.0]

### Fixed
* Include casacore log messages in carta log ([#1169](https://github.com/CARTAvis/carta-backend/issues/1169)).
* Fixed the problem of opening old IRAM fits images ([#1312](https://github.com/CARTAvis/carta-backend/issues/1312)).
* Fixed scripting interface and symlink directory issues ([#1283](https://github.com/CARTAvis/carta-frontend/issues/1283), [#1284](https://github.com/CARTAvis/carta-frontend/issues/1284), [#1314](https://github.com/CARTAvis/carta-frontend/issues/1314)).
* Fixed incorrect std calculation when fitting images with nan values ([#1318](https://github.com/CARTAvis/carta-backend/issues/1318)).
* Fixed the hanging problem when deleting a region during the spectral profile process ([#1328](https://github.com/CARTAvis/carta-backend/issues/1328)).

### Changed
* Updated for compatibility with latest carta-casacore using CASA 6.6.0.

## [4.0.0]

### Changed
* Support animation playback with matched images in multi-panel view ([#1860](https://github.com/CARTAvis/carta-frontend/issues/1860)).
* Update the submodule uWebSockets ([#1297](https://github.com/CARTAvis/carta-backend/issues/1297)).

### Fixed
* Prevent the installation of pugixml library files ([#1261](https://github.com/CARTAvis/carta-backend/issues/1261)).
* Fixed spatial profile for polyline in widefield image ([#1258](https://github.com/CARTAvis/carta-backend/issues/1258)).
* Fixed regression failure of HDF5 PV image due to profile caching in the HDF5 loader ([#1259](https://github.com/CARTAvis/carta-backend/issues/1259)).
* Removed duplicate image histogram data sent to the frontend ([#1266](https://github.com/CARTAvis/carta-backend/issues/1266)).
* Fixed FITS header and data errors ([#1233](https://github.com/CARTAvis/carta-backend/issues/1233), [#1265](https://github.com/CARTAvis/carta-backend/issues/1265)).
* Fixed the problem of resuming LEL images ([#1226](https://github.com/CARTAvis/carta-backend/issues/1226)).
* Fixed the case-sensitive of reading BUNIT from a file header ([#1187](https://github.com/CARTAvis/carta-backend/issues/1187)).
* Fixed the crash when reading beam table with 64-bit floats ([#1166](https://github.com/CARTAvis/carta-backend/issues/1166)).
* Fixed region spectral profile from FITS gz image ([#1271](https://github.com/CARTAvis/carta-backend/issues/1271)).
* Fixed the lack of mask for LEL images ([#1291](https://github.com/CARTAvis/carta-backend/issues/1291)).
* Fixed file path to save generated image ([#1252](https://github.com/CARTAvis/carta-backend/issues/1252)).
* Fixed missing tiles issue ([#1282](https://github.com/CARTAvis/carta-backend/issues/1282)).
* Fixed the crash of loading JCMT-SCUBA2 FITS images ([#1301](https://github.com/CARTAvis/carta-backend/issues/1301)).
* Fixed updating the PV preview for a matched image ([#1304](https://github.com/CARTAvis/carta-backend/issues/1304)).

## [4.0.0-beta.1]

### Added
* Added a check of averaging width when calculating line/polyline spatial profiles or PV images ([#1174](https://github.com/CARTAvis/carta-backend/issues/1174)).
* Added support for fitting images with regions, fixed parameters, a background offset, and different solvers; added support for generating model and residual images, estimating progress, and cancelling tasks ([#150](https://github.com/CARTAvis/carta-backend/issues/150)).
* Added PV generator features for spectral range, reversed axes, and keeping previous image ([#1175](https://github.com/CARTAvis/carta-backend/issues/1175), [#1176](https://github.com/CARTAvis/carta-backend/issues/1176), [#1177](https://github.com/CARTAvis/carta-backend/issues/1177)).
* Added a debug config flag for disabling runtime config ([#1213](https://github.com/CARTAvis/carta-backend/issues/1213)).
* Added support to keep previously generated moment images ([#1202](https://github.com/CARTAvis/carta-backend/issues/1202)).
* Added pugixml as a third-party library with the option PUGIXML_COMPACT enabled ([#1217](https://github.com/CARTAvis/carta-backend/issues/1217)).
* Added automatically generated documentation with Doxygen ([#1215](https://github.com/CARTAvis/carta-backend/issues/1215)).
* Added support for loading swapped-axes image cubes ([#1178](https://github.com/CARTAvis/carta-backend/issues/1178)).
* Added support for annotation regions ([#340](https://github.com/CARTAvis/carta-backend/issues/340)).
* Added support for customizing histogram calculations ([#829](https://github.com/CARTAvis/carta-backend/issues/829)).
* Added support for PV preview ([#795](https://github.com/CARTAvis/carta-backend/issues/795)).

### Changed
* Removed CASA CRTF parser for performance and annotation region support ([#1219](https://github.com/CARTAvis/carta-backend/issues/1219)).
* Adjusted image fitting error calculation considering correlated noise; added integrated flux information in the fitting result ([#1277](https://github.com/CARTAvis/carta-backend/issues/1277)).

### Fixed
* Fixed issues with AIPS velocity axis by restoring previous casacore headers ([#1771](https://github.com/CARTAvis/carta-frontend/issues/1771)).
* Fixed error in regions when resuming session. ([#1210](https://github.com/CARTAvis/carta-backend/issues/1210)).
* Fixed crash when exporting matched region ([#1205](https://github.com/CARTAvis/carta-backend/issues/1205), [#1208](https://github.com/CARTAvis/carta-backend/issues/1208)).
* Fixed region import with space in region name ([#1188](https://github.com/CARTAvis/carta-backend/issues/1188)).
* Fixed cfitsio 4.2.0 fits_read_key abort ([#1231](https://github.com/CARTAvis/carta-backend/issues/1231)).
* Fixed failure loading CASA image due to FITS headers error ([#1239](https://github.com/CARTAvis/carta-backend/issues/1239)).
* Fixed incorrect PV image orientation if the cube has projection distortion ([#1244](https://github.com/CARTAvis/carta-backend/issues/1244)).
* Fixed crash following use of an incorrect session ID ([#1248](https://github.com/CARTAvis/carta-backend/issues/1248)).
* Fixed header angle formatting error with non-angle unit ([#1218](https://github.com/CARTAvis/carta-backend/issues/1218)).

## [3.0.0]

### Added
* Added support for image fitting with field of view ([#150](https://github.com/CARTAvis/carta-backend/issues/150)).
* List frequency and velocity in file info for single channel image ([#1152](https://github.com/CARTAvis/carta-backend/issues/1152)).

### Changed
* Enhanced image fitting performance by switching the solver from qr to cholesky ([#1114](https://github.com/CARTAvis/carta-backend/pull/1114)).
* Made HTTP server return a different error code for disabled features ([#1115](https://github.com/CARTAvis/carta-backend/issues/1115)).
* Removed Splatalogue interaction from backend codebase and removed dependency on libcurl ([#994](https://github.com/cartavis/carta-backend/issues/994)).
* Use wrappers to construct protocol buffer messages where possible ([#960](https://github.com/CARTAvis/carta-backend/issues/960)).
* Change the time zone in log messages from local to UTC ([#1151](https://github.com/CARTAvis/carta-backend/issues/1151)).
* Refactor the timer for performance measurements ([#1180](https://github.com/CARTAvis/carta-backend/issues/1180)).

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
* Fixed HTTP response codes when returning response to PUT requests ([#1157](https://github.com/CARTAvis/carta-backend/issues/1157)).
* Fixed the problem of one-pixel position offset for DS9 regions projections ([#1138](https://github.com/CARTAvis/carta-backend/issues/1138)).
* Fixed crash problems during moments ICD tests ([#1070](https://github.com/CARTAvis/carta-backend/issues/1070)).
* Fixed response when importing region file fails by catching exception ([#1160](https://github.com/CARTAvis/carta-backend/issues/1160)).
* Fixed the crash when trying to load an unsupported image file ([#1161](https://github.com/CARTAvis/carta-backend/issues/1161)).
* Fixed including directories in region file list ([#1159](https://github.com/CARTAvis/carta-backend/issues/1159)).
* Fixed issue where NaN data was read incorrectly from a compressed FITS .fz image ([#1143](https://github.com/CARTAvis/carta-backend/issues/1143)).

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
