#!/bin/sh

SELF=$(basename "$0")

openssl req -x509 -newkey rsa:2048 -keyout ${SELF}-private.pem -out ${SELF}-public.pem -nodes -days 365

for i in *.c; do
    [ -f "$i" ] || break
    cp -n ${SELF}-private.pem ${i%.*}-private.pem
    cp -n ${SELF}-public.pem  ${i%.*}-public.pem
done

rm -f ${SELF}-private.pem ${SELF}-public.pem

echo -e "\n Keys generated.\n"
