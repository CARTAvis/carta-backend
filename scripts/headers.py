#!/usr/bin/env python3

import sys
import os
import re
import argparse

HEADER = """/* This file is part of the CARTA Image Viewer: https://github.com/CARTAvis/carta-backend
   Copyright 2018, 2019, 2020 Academia Sinica Institute of Astronomy and Astrophysics (ASIAA),
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

def cpp_files(directory):
    for root, dirs, files in os.walk(directory):
        dirs[:] = [d for d in dirs if d != "build"]
        for basename in files:
            if re.search("\.(cc|h)$", basename):
                filename = os.path.join(root, basename)
                yield filename
                
def noprint(*args):
    pass

if __name__ == "__main__":    
    parser = argparse.ArgumentParser(description="Check or fix copyright and licence headers.")
    parser.add_argument('command', help="Command (check or fix).", choices=("check", "fix"))
    parser.add_argument('-d', '--directory', help="Location of the root directory (default is the current directory).", default=".")
    parser.add_argument('-q', '--quiet', help="Suppress output", action='store_true')

    args = parser.parse_args()
    
    out = noprint if args.quiet else print
    return_status = 0
    
    for filename in cpp_files(args.directory):
        with open(filename) as f:
            data = f.read()
            
            if re.search(re.escape(HEADER), data):
                pass
            
            elif re.search(FUZZY_HEADER_MATCH, data):
                out("Bad header found in", filename)
                
                if args.command == "fix":
                    out("Fixing...")
                    data = re.sub(FUZZY_HEADER_MATCH, HEADER, data)
                    with open(filename, "w") as f:
                        f.write(data)
                else:
                    return_status = 1
            else:
                out("No header found in", filename)
                
                if args.command == "fix":
                    out("Fixing...")
                    with open(filename, "w") as f:
                        f.write(HEADER)
                        f.write("\n")
                        f.write(data)
                else:
                    return_status = 1

    
    sys.exit(return_status)
