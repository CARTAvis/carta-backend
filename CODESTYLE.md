# Code Style
## Formatting

Code formatting is enforced by `clang-format`. The `reformat.sh` script found in the `scripts` directory can be used to reformat all source files in the project. Code formatting style is based on the [Google C++ style](https://google.github.io/styleguide/cppguide.html#Formatting), with the following changes:
* 4 spaces used instead of 2
* Column limit increased to 140 characters
* Left pointer alignment is enforced
* Minor changes to single-line code styles
* Ordering of include files: `casacore` and `carta-protobuf` includes are given their own include group after other external library includes

Details of the formatting style can be seen in `.clang-format`.

## Naming convention

Naming convention follows [Google C++ style](https://google.github.io/styleguide/cppguide.html#Naming), with the following changes:
* file names use PascalCase instead of underscore_case
* the `CARTA` namespace (from the auto-generated protocol buffer code) is uppercase, while all other namespaces are lower case
* private class members have a leading underscore, rather than trailing
* getter and setter functions are named like normal functions

To summarise these conventions:
* variable names and class data members use underscore_case, such as `table_name`, `icd_version`, `session_id`.
* private class members have a leading underscore, such as `_num_stokes`
* function and class names use PascalCase, such as `OpenFile()`, `FillHistogramData()`, `GetCount()`
* enumerators are named in UPPER_CASE, such as `REGISTER_VIEWER`, `REGION_REQUIREMENTS`.

Naming convention will be checked using `clang-tidy` (work in progress) 
