#!/usr/bin/env python3

import sys
import os
import re
import argparse
import subprocess

class Test:
    TESTS = {}
    PYTHON_FILE_REGEX = r".*\.(cc|h|tcc)$"
    SHELL_FILE_REGEX = r".*\.\(cc\|h\|tcc\)$"
    EXCLUDE_FROM_ALL = False
    
    def __init_subclass__(cls, **kwargs):
        name = cls.__name__.lower()
        if name not in Test.TESTS:
            Test.TESTS[name] = cls
    
    @classmethod
    def get_tests(cls, testname):
        if testname == "all":
            return tuple(test for test in cls.TESTS.values() if not test.EXCLUDE_FROM_ALL)
        return (cls.TESTS[testname],)
    
    @staticmethod
    def cpp_files(directory):
        for root, dirs, files in os.walk(directory):
            dirs[:] = [d for d in dirs]
            for basename in files:
                if re.match(Test.PYTHON_FILE_REGEX, basename):
                    filename = os.path.join(root, basename)
                    yield filename
    
    @staticmethod
    def check(directories, quiet):
        raise NotImplementedError()
    
    @staticmethod
    def fix(directories, quiet):
        raise NotImplementedError()

class Header(Test):
    HEADER = """/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020, 2021 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
   Associated Universities, Inc. (AUI) and the Inter-University Institute for Data Intensive Astronomy (IDIA)
   SPDX-License-Identifier: GPL-3.0-or-later
*/
"""

    FUZZY_HEADER_MATCH = """/\* This file (.*)
   Copyright (.*)
(   (.*)
)*   SPDX-License-Identifier: (.*)
\*/
"""
    
    @staticmethod
    def check(directories, quiet):
        status = 0
        out = (lambda *args: None) if quiet else print
        
        for directory in directories:
            for filename in Test.cpp_files(directory):
                with open(filename) as f:
                    data = f.read()
                    
                if re.search(re.escape(Header.HEADER), data):
                    pass
                elif re.search(Header.FUZZY_HEADER_MATCH, data):
                    out("Bad header found in", filename)
                    status = 1
                else:
                    out("No header found in", filename)
                    status = 1
        
        return status
    
    @staticmethod
    def fix(directories, quiet):
        out = (lambda *args: None) if quiet else print
        
        for directory in directories:
            for filename in Test.cpp_files(directory):
                with open(filename) as f:
                    data = f.read()
                    
                if re.search(re.escape(Header.HEADER), data):
                    pass
                
                elif re.search(Header.FUZZY_HEADER_MATCH, data):
                    out("Bad header found in", filename)
                    out("Fixing...")
                    data = re.sub(Header.FUZZY_HEADER_MATCH, Header.HEADER, data)
                    with open(filename, "w") as f:
                        f.write(data)

                else:
                    out("No header found in", filename)
                    out("Fixing...")
                    with open(filename, "w") as f:
                        f.write(Header.HEADER)
                        f.write("\n")
                        f.write(data)
        
        return 0

class Format(Test):
    @staticmethod
    def check(directories, quiet):        
        find_files = fr"find {' '.join(directories)} -regex '{Test.SHELL_FILE_REGEX}'"
        process = subprocess.run(fr"{find_files} -exec clang-format {{}} \; | diff -u <({find_files} -exec cat {{}} \;) -", shell=True, executable="/bin/bash", capture_output=True)
        
        if process.stdout and not quiet:
            print(process.stdout.decode())
        
        if process.stderr:
            print(process.stderr.decode())
        
        return process.returncode
    
    @staticmethod
    def fix(directories, quiet):
        find_files = fr"find {' '.join(directories)} -regex '{Test.SHELL_FILE_REGEX}'"
        process = subprocess.run(fr"{find_files} | xargs clang-format -i", shell=True, executable="/bin/bash", capture_output=True)
                
        if process.stdout and not quiet:
            print(process.stdout.decode())
        
        if process.stderr:
            print(process.stderr.decode())
        
        return process.returncode

class Newline(Test):
    @staticmethod
    def check(directories, quiet):
        status = 0
        out = (lambda *args: None) if quiet else print
        
        for directory in directories:
            for filename in Test.cpp_files(directory):
                with open(filename) as f:
                    data = f.read()
                    
                if data[-1] == "\n":
                    pass
                else:
                    out("Missing newline at end of", filename)
                    status = 1
        
        return status
    
    @staticmethod
    def fix(directories, quiet):
        out = (lambda *args: None) if quiet else print
        
        for directory in directories:
            for filename in Test.cpp_files(directory):
                with open(filename) as f:
                    data = f.read()
                    
                if data[-1] == "\n":
                    pass
                else:
                    out("Missing newline at end of", filename)
                    out("Fixing...")
                    with open(filename, "w") as f:
                        f.write(data)
                        f.write("\n")
        
        return 0

class Style(Test):
    EXCLUDE_FROM_ALL = True
    
    @staticmethod
    def check(directories, quiet):
        status = 0
        
        tidy_regex = f"\"$PWD/({'|'.join(directories)})/.*\""
        process = subprocess.run(fr"run-clang-tidy -quiet -extra-arg-before=-fno-caret-diagnostics -p build -header-filter={tidy_regex} {tidy_regex}", shell=True, executable="/bin/bash", capture_output=True)
                
        if process.stdout and not quiet:
            print(process.stdout.decode())
        
        if process.stderr:
            print(process.stderr.decode())
        
        status = process.returncode | bool(process.stdout)
        
        return status
    
    @staticmethod
    def fix(directories, quiet):
        if not quiet:
            print("Automatic style fixes are not yet implemented.")
        return 1

if __name__ == "__main__":    
    parser = argparse.ArgumentParser(description="Check or fix copyright and licence headers, code format, missing newlines at the ends of files, or code style.")
    parser.add_argument('test', help=f"Test to perform (or all tests; style is currently excluded from all).", choices=(*Test.TESTS.keys(), "all"))
    parser.add_argument('command', help="Command to perform.", choices=("check", "fix"))
    parser.add_argument('-q', '--quiet', help="Suppress output", action='store_true')
    args = parser.parse_args()
    
    # TODO custom clang-format executable
    
    directories = ("src", "test")
    status = 0
    
    for test in Test.get_tests(args.test):
        if args.command == "check":
            status |= test.check(set(directories), args.quiet)
        elif args.command == "fix":
            status |= test.fix(set(directories), args.quiet)
                
    sys.exit(status)
