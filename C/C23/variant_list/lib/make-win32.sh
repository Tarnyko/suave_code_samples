#!/bin/sh

NAME="variant_list"

if [ -z "$CC" ]; then
    CC=gcc
fi
if [ -z "$AR" ]; then
    AR=ar
fi


echo "Make-ing Win32 files"
echo "-------------------"


echo "Make-ing static library..."
${CC} -std=c23 -Wall -DSTATIC ${CPPFLAGS} ${CFLAGS} -c -o lib$NAME.o lib$NAME.c
${AR} -cvq lib$NAME.a lib$NAME.o > /dev/null

echo "Make-ing DLL and import library..."
${CC} -std=c23 -Wall -DSHARED -shared ${CPPFLAGS} ${CFLAGS} -Wl,--out-implib,lib$NAME.dll.a -o lib$NAME.dll lib$NAME.c -lm ${LDFLAGS}


echo "Make-ing static executable..."
${CC} -std=c23 -Wall -static ${CPPFLAGS} ${CFLAGS} -o $NAME-static.exe $NAME.c -L. -l$NAME -lm ${LDFLAGS}

echo "Make-ing shared executable..."
${CC} -std=c23 -Wall ${CPPFLAGS} ${CFLAGS} -o $NAME-shared.exe $NAME.c -L. -l$NAME


echo "Cleaning intermediate files..."
rm -f lib$NAME.o


echo "Done."
