#!/bin/sh

gcc -g backtrace.c -o backtrace-win64.exe -ldbghelp

# (see: https://github.com/rainers/cv2pdb/releases)
./cv2pdb64.exe  backtrace-win64.exe  backtrace-win64_pdb.exe backtrace-win64_pdb.pdb
