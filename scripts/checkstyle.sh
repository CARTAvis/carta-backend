# !/bin/bash

# This is a utility script for running clang-tidy on all files to check for style issues.
# This does not modify the files.
# It should be run from the root directory of the repository.
# The clang-tidy configuration is found in .clang-tidy

find src test -regex ".*\.\(cc\|h\|tcc\)" | xargs run-clang-tidy -p build
