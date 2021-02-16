# !/bin/bash

# This is a utility script for reformatting all .h and .cc files recursively.
# It should be run from the root directory of the repository.
# The clang-format configuration is found in .clang-format
# To use a specific clang-format version, pass the appropriate executable as a parameter.

cformat=${1:-clang-format}

find src test -regex ".*\.\(cc\|h\|tcc\)" | xargs $cformat -i
