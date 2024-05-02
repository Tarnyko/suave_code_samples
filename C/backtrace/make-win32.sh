#!/bin/sh

gcc -g backtrace.c -o backtrace-x64.exe -ldbghelp

# (see: https://github.com/rainers/cv2pdb/releases)
./cv2pdb64.exe  backtrace-x64.exe  backtrace-x64_pdb.exe backtrace-x64_pdb.pdb
