# Enable address sanitizers for debugging:

Build carta-backend with:

```
cmake .. \
-DCMAKE_BUILD_TYPE=Debug \
-Dtest=ON \
-DCMAKE_CXX_FLAGS="-O0 -g -fsanitize=address -fno-omit-frame-pointer" \
-DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address"
```

Run with the provided suppression file to remove errors triggered from external libraries:

```
ASAN_OPTIONS=suppressions=$HOME/carta-developer/carta-backend/debug/asan/myasan.supp ASAN_SYMBOLIZER_PATH=/usr/local/bin/llvm-symbolizer ./carta_backend_tests
```

If there is a problem, execution stops, and it means that there is a real problem, as described in the [ASAN usage documentation](https://clang.llvm.org/docs/AddressSanitizer.html#usage):

>If a bug is detected, the program will print an error message to stderr and exit with a non-zero exit code. AddressSanitizer exits on the first detected error. This is by design:
>
>- This approach allows AddressSanitizer to produce faster and smaller generated code (both by ~5%).
> - Fixing bugs becomes unavoidable. AddressSanitizer does not produce false alarms. Once a memory corruption occurs, the program is in an inconsistent state, which could lead to confusing results and potentially misleading subsequent reports.

# References:
- https://github.com/google/sanitizers
- https://clang.llvm.org/docs/AddressSanitizer.html

# Notes:

On macOS systems the following warning appears always:
```
carta_backend_tests(27465,0x112872600) malloc: nano zone abandoned due to inability to preallocate reserved vm space.
```
It seems to be harmless, and is explained here: https://stackoverflow.com/a/70209891/2201117. One way to suppress the message is running with the `MallocNanoZone` environment variable set to zero:

```
MallocNanoZone=0 ./carta_backend_tests
```
