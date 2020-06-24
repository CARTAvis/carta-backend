# !/bin/bash

# This is a utility script for running clang-tidy on all files to check for style issues.
# This does not modify the files.
# It should be run from the root directory of the repository.
# The clang-tidy configuration is found in .clang-tidy

find . -regex ".*\.\(cc\|h\)" -not -path "./*/carta-protobuf/*" -not -path "./*/carta-scripting-grpc/*" | xargs run-clang-tidy -p build
