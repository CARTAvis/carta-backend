# !/bin/bash

# This is a utility script to check if any code needs to be reformatted.
# This does not modify the files.
# It should be run from the root directory of the repository.
# The exit status of the script should be 0 if no formatting is necessary, and 1 otherwise.
# If changes are necessary the script will print a diff -- please run reformat.sh to fix the code.
# The clang-format configuration is found in .clang-format

PARAMS=(\( -regex '.*\.\(cc\|h\)' -not -path './*/carta-protobuf/*' -not -path './*/carta-scripting-grpc/*' \))

find . "${PARAMS[@]}" -exec cat {} \; | diff -u <(find . "${PARAMS[@]}" -exec clang-format {} \;) -

exit
