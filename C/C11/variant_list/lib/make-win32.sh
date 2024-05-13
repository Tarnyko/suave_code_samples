#!/bin/sh

NAME="variant_list"


echo "Make-ing Win32 files"
echo "-------------------"


echo "Make-ing static library..."
gcc -std=c11 -DSTATIC -c -o lib$NAME.o lib$NAME.c
ar -cvq lib$NAME.a lib$NAME.o > /dev/null

echo "Make-ing DLL and import library..."
gcc -std=c11 -DSHARED -shared -Wl,--out-implib,lib$NAME.dll.a -o lib$NAME.dll lib$NAME.c -lm


echo "Make-ing static executable..."
gcc -std=c11 -static -o $NAME-static.exe $NAME.c -L. -l$NAME -lm

echo "Make-ing shared executable..."
gcc -std=c11 -o $NAME-shared.exe $NAME.c -L. -l$NAME


echo "Cleaning intermediate files..."
rm -f lib$NAME.o


echo "Done."
