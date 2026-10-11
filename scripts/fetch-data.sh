#!/bin/sh
#
#  Fetch the Johnny Castaway game data archive (scrantic_data.zip).
#
#  The archive holds the original 1992 Sierra/Dynamix resources (RESOURCE.MAP,
#  RESOURCE.001, the sound samples) plus the optional HD/cartoon artwork made for
#  the open-source engine. The original data is copyrighted abandonware and is
#  therefore not stored in this repository; it is downloaded from the engine
#  project's repository, pinned to one commit and verified by SHA-256.
#
#  Usage: sh scripts/fetch-data.sh [destination]   (default: data/scrantic_data.zip)
#
#  Already have your own copy? Put it at the destination path and this script
#  does nothing.

set -eu

DEST="${1:-data/scrantic_data.zip}"
URL="https://raw.githubusercontent.com/bigfnj/johnny-castaway-rebornHD/a0c2d032e75ac24145acce5c505cf3ac396b9427/assets/scrantic_data.zip"
SHA256="a87a1f52b85277d88129347654a508edf9ca1f82920a31e20b5b41216242f63d"

if [ -f "$DEST" ]; then
    echo "Using existing $DEST"
    exit 0
fi

mkdir -p "$(dirname "$DEST")"
TMP="$DEST.part"
echo "Downloading game data ..."
curl -fL --retry 3 -o "$TMP" "$URL"

if command -v shasum >/dev/null 2>&1; then
    ACTUAL=$(shasum -a 256 "$TMP" | cut -d' ' -f1)
else
    ACTUAL=$(sha256sum "$TMP" | cut -d' ' -f1)
fi

if [ "$ACTUAL" != "$SHA256" ]; then
    rm -f "$TMP"
    echo "Checksum mismatch for downloaded data archive:" >&2
    echo "  expected $SHA256" >&2
    echo "  got      $ACTUAL" >&2
    exit 1
fi

mv "$TMP" "$DEST"
echo "Saved $DEST"
