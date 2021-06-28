#!/usr/bin/env python3

import sys
import os
import re
import argparse

class Test:
    TESTS = {}
    
    def __init_subclass__(cls, **kwargs):
        name = cls.__name__.lower()
        if name not in Test.TESTS:
            Test.TESTS[name] = cls
    
    @classmethod
    def get_tests(cls, testname):
        if testname == "all":
            return cls.TESTS.values()
        return (cls.TESTS[testname],)
    
    @staticmethod
    def cpp_files(directory):
        for root, dirs, files in os.walk(directory):
            dirs[:] = [d for d in dirs]
            for basename in files:
                if re.search("\.(cc|h|tcc)$", basename):
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

class Format(Test):
    @staticmethod
    def check(directories, quiet):
        raise NotImplementedError()
    
    @staticmethod
    def fix(directories, quiet):
        raise NotImplementedError()

class Style(Test):
    @staticmethod
    def check(directories, quiet):
        raise NotImplementedError()
    
    @staticmethod
    def fix(directories, quiet):
        raise NotImplementedError()

if __name__ == "__main__":    
    parser = argparse.ArgumentParser(description="Check or fix code format, style, or copyright and licence headers.")
    parser.add_argument('test', help="Test to perform (header, format, style or all)", choices=(*Test.TESTS.keys(), "all"))
    parser.add_argument('command', help="Command (check or fix).", choices=("check", "fix"))
    parser.add_argument('-d', '--directory', help="Location of the root directory; can be used multiple times. Defaults to the `src' and `test' subdirectories in the current directory.", default=[], action="append")
    parser.add_argument('-q', '--quiet', help="Suppress output", action='store_true')
    args = parser.parse_args()
    
    # We have to do it like this because the append action doesn't clear the default list
    if not args.directory:
        args.directory.extend(["src", "test"])
        
    status = 0
    
    for test in Test.get_tests(args.test):
        if args.command == "check":
            status |= test.check(set(args.directory), args.quiet)
        elif args.command == "fix":
            test.fix(set(args.directory), args.quiet)
                
    sys.exit(status)
