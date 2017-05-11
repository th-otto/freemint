#!/bin/sh

TMP="$1"
OUT="$2"

sed -e "s/\"cur\"/\"${SHORT_ID}\"/;" sys/buildinfo/version.h > sys/buildinfo/version.h.tmp && mv sys/buildinfo/version.h.tmp sys/buildinfo/version.h
cd ..

cd "${PROJECT}" && make && cd ..

mkdir -p "${TMP}"
mkdir -p "${OUT}"

"./${PROJECT}/.travis/freemint.org/freemint.build" "${OUT}"
