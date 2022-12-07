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
