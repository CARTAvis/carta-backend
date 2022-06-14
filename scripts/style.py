#!/usr/bin/env python3

import sys
import os
import re
import argparse
import subprocess


class Test:
    TESTS = {}
    EXCLUDE_FROM_ALL = False

    EXTENSIONS = ("cc", "h", "tcc")
    FILE_REGEX = fr".*\.({'|'.join(EXTENSIONS)})$"

    quiet = False
    out = print

    def __init_subclass__(cls, **kwargs):
        name = cls.__name__.lower()
        if name not in Test.TESTS:
            Test.TESTS[name] = cls

    @classmethod
    def get_tests(cls, testname):
        if testname == "all":
            return tuple(testclass for testclass in cls.TESTS.values() if not testclass.EXCLUDE_FROM_ALL)
        return (cls.TESTS[testname],)

    @classmethod
    def cpp_files(cls, directory):
        for root, dirs, files in os.walk(directory):
            dirs[:] = [d for d in dirs]
            for basename in files:
                if re.match(cls.FILE_REGEX, basename):
                    filename = os.path.join(root, basename)
                    yield filename

    @classmethod
    def check(cls, directories):
        raise NotImplementedError()

    @classmethod
    def fix(cls, directories):
        raise NotImplementedError()


class Header(Test):
    HEADER = """/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021, 2022 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/
"""

    FUZZY_HEADER_MATCH = r"""/\* This file (.*)
   Copyright (.*)
(   (.*)
)*   SPDX-License-Identifier: (.*)
\*/
"""

    @classmethod
    def check(cls, directories):
        status = 0

        for directory in directories:
            for filename in cls.cpp_files(directory):
                with open(filename) as f:
                    data = f.read()

                if re.search(re.escape(cls.HEADER), data):
                    pass
                elif re.search(cls.FUZZY_HEADER_MATCH, data):
                    cls.out("Bad header found in", filename)
                    status = 1
                else:
                    cls.out("No header found in", filename)
                    status = 1

        return status

    @classmethod
    def fix(cls, directories):
        for directory in directories:
            for filename in cls.cpp_files(directory):
                with open(filename) as f:
                    data = f.read()

                if re.search(re.escape(cls.HEADER), data):
                    pass

                elif re.search(cls.FUZZY_HEADER_MATCH, data):
                    cls.out("Bad header found in", filename)
                    cls.out("Fixing...")
                    data = re.sub(cls.FUZZY_HEADER_MATCH, cls.HEADER, data)
                    with open(filename, "w") as f:
                        f.write(data)

                else:
                    cls.out("No header found in", filename)
                    cls.out("Fixing...")
                    with open(filename, "w") as f:
                        f.write(cls.HEADER)
                        f.write("\n")
                        f.write(data)

        return 0


class Format(Test):
    executable = "clang-format"

    @classmethod
    def check(cls, directories):
        status = 0

        for directory in directories:
            for filename in cls.cpp_files(directory):
                process = subprocess.run(fr"diff {filename} <( {cls.executable} {filename} )", shell=True, executable="/bin/bash", capture_output=True)

                if process.returncode:
                    cls.out("Bad format found in", filename)
                    status = 1

                if process.stdout and not cls.quiet:
                    print(process.stdout.decode())

                if process.stderr:
                    print(process.stderr.decode())

        return status

    @classmethod
    def fix(cls, directories):
        for directory in directories:
            for filename in cls.cpp_files(directory):
                process = subprocess.run(fr"{cls.executable} -i {filename}", shell=True, executable="/bin/bash", capture_output=True)

                if process.stdout and not cls.quiet:
                    print(process.stdout.decode())

                if process.stderr:
                    print(process.stderr.decode())

        return 0


class Newline(Test):
    @classmethod
    def check(cls, directories):
        status = 0

        for directory in directories:
            for filename in cls.cpp_files(directory):
                with open(filename) as f:
                    data = f.read()

                if data[-1] == "\n":
                    pass
                else:
                    cls.out("Missing newline at end of", filename)
                    status = 1

        return status

    @classmethod
    def fix(cls, directories):
        for directory in directories:
            for filename in cls.cpp_files(directory):
                with open(filename) as f:
                    data = f.read()

                if data[-1] == "\n":
                    pass
                else:
                    cls.out("Missing newline at end of", filename)
                    cls.out("Fixing...")
                    with open(filename, "w") as f:
                        f.write(data)
                        f.write("\n")

        return 0


class Style(Test):
    EXCLUDE_FROM_ALL = True

    @classmethod
    def check(cls, directories):
        status = 0

        tidy_regex = f"\"$PWD/({'|'.join(directories)})/.*\""
        process = subprocess.run(fr"run-clang-tidy -quiet -extra-arg-before=-fno-caret-diagnostics -p build -header-filter={tidy_regex} {tidy_regex}", shell=True, executable="/bin/bash", capture_output=True)

        if process.stdout and not cls.quiet:
            print(process.stdout.decode())

        if process.stderr:
            print(process.stderr.decode())

        status = process.returncode | bool(process.stdout)

        return status

    @classmethod
    def fix(cls, directories):
        if not cls.quiet:
            print("Automatic style fixes are not yet implemented.")
        return 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Check or fix copyright and licence headers, code format, missing newlines at the ends of files, or code style.")
    parser.add_argument('test', help="Test to perform (or all tests; style is currently excluded from all).", choices=(*Test.TESTS.keys(), "all"))
    parser.add_argument('command', help="Command to perform.", choices=("check", "fix"))
    parser.add_argument('-q', '--quiet', help="Suppress output", action='store_true')
    parser.add_argument('--clang-format', help="Path to custom clang-format executable.", default=Format.executable)
    args = parser.parse_args()

    if args.quiet:
        Test.quiet = True
        Test.out = lambda *args: None

    Format.executable = args.clang_format

    directories = ("src", "test")
    status = 0

    for test in Test.get_tests(args.test):
        if args.command == "check":
            status |= test.check(directories)
        elif args.command == "fix":
            status |= test.fix(directories)

    sys.exit(status)
