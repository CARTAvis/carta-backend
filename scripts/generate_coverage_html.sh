#!/bin/bash
echo "Please make sure you run all tests in debug mode with coverage first!"
mkdir -p coverage
gcovr -f src/ --html-details -j 8 -o coverage/index.html