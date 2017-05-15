#!/bin/sh

# Use as ". build.sh"

TMP="$1"
OUT="$2"

if [ -z "$OLD_BUILD" -o "$OLD_BUILD" -eq 0 ]
then
	export PUBLISH_PATH="${PUBLISH_PATH}/new"

	# TODO: when all the tools (mintloader etc) are added to Helmut's branch
else
	export PUBLISH_PATH="${PUBLISH_PATH}/old"

	mkdir -p "${OUT}"
	"./.travis/freemint.org/freemint.build" "${OUT}"
fi
