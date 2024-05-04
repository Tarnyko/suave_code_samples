#!/bin/sh

g++ -g backtrace.cpp -o backtrace-cpp-win64.exe -ldbghelp

# (see: https://github.com/rainers/cv2pdb/releases)
./cv2pdb64.exe  backtrace-cpp-win64.exe  backtrace-cpp-win64_pdb.exe backtrace-cpp-win64_pdb.pdb
