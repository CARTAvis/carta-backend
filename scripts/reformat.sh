# !/bin/bash

# This is a utility script for reformatting all .h and .cc files recursively.
# It should be run from the root directory of the repository.
# The clang-format configuration is found in .clang-format

find . -regex ".*\.\(cc\|h\)" -not -path "./build/*" | xargs clang-format -i
