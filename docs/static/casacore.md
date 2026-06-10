Carta-casacore upgrade checklist
================================

`carta-casacore` upgrades should be synchronised with CARTA releases, so that existing backend packages don't have to be rebuilt.

-# Create a `carta-casacore` PR and a corresponding `carta-backend` PR.
-# Update the `casa6` submodule and its nested `casa6/casatools/casacore` submodule.
-# Carefully compare the root `CMakeLists.txt` file to `casa6/casatools/casacore/CMakeLists.txt` and merge in any required changes (at least the project version and the `SOVERSION`). We have removed some irrelevant conditional sections, changed some default variable values, modified some paths because of the relative location of the file, and added elements such as the custom imageanalysis build. Apart from that, the files should be kept in sync as far as possible.
-# Check the `casa6` submodule's git tags to get the CASA version. Update the SOVERSION comment in `imageanalysis/CMakeLists.txt` and bump the `CASA_PROJECT_SOVERSION` if the minor version has changed. Other changes to this file should typically not be required, as sources and headers are found dynamically with a glob.
-# Test building `carta-casacore`, and make any required changes.
-# Test building the backend against the new `carta-casacore`, and make any required changes.
-# Merge the `carta-casacore` PR.
-# Build the `carta-casacore` packages and upload them to the testing repositories.
-# Update CI to use these packages.
-# Confirm that the backend PR passes tests with the updated CI.
-# Merge the backend PR.
-# Merge `dev` into all open PRs, and proceed with the release.
-# When the release is complete, make sure that CI is configured to use the preview repositories.
