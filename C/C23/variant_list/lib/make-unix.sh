#!/bin/sh

NAME="variant_list"

if [ -z "$CC" ]; then
    CC=gcc
fi
if [ -z "$AR" ]; then
    AR=ar
fi


echo "Make-ing UNIX files"
echo "-------------------"

${CC} -std=c23 -Wall -fPIC ${CPPFLAGS} ${CFLAGS} -c -o lib$NAME.o lib$NAME.c


echo "Make-ing static library..."
${AR} -cvq lib$NAME.a lib$NAME.o > /dev/null

echo "Make-ing shared library..."
${CC} -shared -Wl,-soname,lib$NAME.so.0 -o lib$NAME.so.0 lib$NAME.o -lm ${LDFLAGS}
ln -sf lib$NAME.so.0 lib$NAME.so


echo "Make-ing static executable..."
${CC} -std=c23 -Wall -static -o $NAME-static $NAME.c -L. -l$NAME -lm ${LDFLAGS}

echo "Make-ing shared executable..."
${CC} -std=c23 -Wall -o $NAME-shared $NAME.c -L. -l$NAME


echo "Cleaning intermediate files..."
rm -f lib$NAME.o


echo "Done."
