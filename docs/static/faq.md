Developer FAQ and tips
======================

Building and running unit tests
-------------------------------

Enabling tests in the build:

```shell
cd build
cmake -Dtest=ON ..
make -j4
```

Running all the tests (from the `build` directory):

```shell
./test/carta_backend_tests
```

Running only some tests:

```shell
./test/carta_backend_tests --gtest_filter=SuiteName.TestName
./test/carta_backend_tests --gtest_filter=SuiteName.*
./test/carta_backend_tests --gtest_filter=-SuiteToExclude.*
```

Re-running the same test multiple times:

```shell
./test/carta_backend_tests --gtest_filter=SuiteName.TestName --gtest_repeat=100
```

See `./test/carta_backend_tests --help` for a list of commandline parameters that you can pass to the test executable, or [the GoogleTest documentation](https://google.github.io/googletest/) for more information about the GoogleTest library.

Building with ASAN flags
------------------------

This allows the backend executable or the test executable to be run with additional [AddressSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizer) checks. We enable this in our CI. Our configuration suppresses warnings from some external dependencies, like casa and casacore.

To build, pass these additional flags when you invoke cmake:

```shell
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS='-O0 -g -fsanitize=address -fno-omit-frame-pointer' -DCMAKE_EXE_LINKER_FLAGS='-fsanitize=address'
```

ASAN configuration is stored in the `debug` directory in the root of the repository. You need to provide paths into this directory to set the appropriate shell variables when invoking the executable.

Invocation example (assuming that you want to run the backend and are in the `build` subdirectory):

```shell
ASAN_OPTIONS=suppressions=../debug/asan/myasan.supp LSAN_OPTIONS=suppressions=../debug/asan/myasan-leaks.supp ASAN_SYMBOLIZER_PATH=llvm-symbolizer ./carta_backend
```

Checking and fixing code format
-------------------------------

To check the code format (from the repository root):

```shell
./scripts/style.py all check
```

To fix code format issues:

```shell
./scripts/style.py all fix
```

See `./scripts/style.py --help` for a list of options (e.g. selecting specific checks).

Currently the script can check: the code format (using `clang-format`), the copyright header, and the newline at the end ofthe file. A more extensive code style check (using `clang-tidy`) is planned but not yet fully implemented.
