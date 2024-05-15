#!/bin/sh

NAME="variant_list"


echo "Make-ing UNIX files"
echo "-------------------"

gcc -std=c11 -fPIC ${CPPFLAGS} ${CFLAGS} -c -o lib$NAME.o lib$NAME.c


echo "Make-ing static library..."
ar -cvq lib$NAME.a lib$NAME.o > /dev/null

echo "Make-ing shared library..."
gcc -shared -Wl,-soname,lib$NAME.so.0 -o lib$NAME.so.0 lib$NAME.o -lm ${LDFLAGS}
ln -sf lib$NAME.so.0 lib$NAME.so


echo "Make-ing static executable..."
gcc -std=c11 -static -o $NAME-static $NAME.c -L. -l$NAME -lm ${LDFLAGS}

echo "Make-ing shared executable..."
gcc -std=c11 -o $NAME-shared $NAME.c -L. -l$NAME


echo "Cleaning intermediate files..."
rm -f lib$NAME.o


echo "Done."
