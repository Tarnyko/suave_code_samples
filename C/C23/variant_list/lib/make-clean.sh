#!/bin/sh

NAME="variant_list"

echo "Clean-ing common files..."
for FILE in \
    lib$NAME.o \
    lib$NAME.a; do
    rm -f ${FILE}
done

echo "Clean-ing UNIX files..."
for FILE in \
    lib$NAME.so \
    lib$NAME.so.? \
    $NAME-static \
    $NAME-shared; do
    rm -f ${FILE}
done

echo "Clean-ing Win32 files..."
for FILE in \
    lib$NAME.dll.a \
    lib$NAME.dll \
    $NAME-static.exe \
    $NAME-shared.exe; do
    rm -f ${FILE}
done

echo "Done."
