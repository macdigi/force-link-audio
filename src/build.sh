#!/bin/sh
# Cross-compile the addon for the Akai Force (armv7l, glibc 2.39).
#
#   brew install zig          # zig is used purely as a cross-compiler
#
# LINK points at an Ableton Link checkout that includes Link Audio.
set -e
LINK=${LINK:-../../link}
OUT=${OUT:-../ForceLinkAudio}

echo "building forceStream.so ..."
zig cc -target arm-linux-gnueabihf.2.39 -shared -fPIC -O2 \
    -o "$OUT/forceStream.so" forceStream.c

echo "building forceLinkAudio ..."
zig c++ -target arm-linux-gnueabihf.2.39 -std=c++17 -O2 \
    -I "$LINK/include" -I "$LINK/modules/asio-standalone/asio/include" \
    -DASIO_STANDALONE -DLINK_PLATFORM_LINUX=1 \
    -o "$OUT/forceLinkAudio" forceLinkAudio.cpp -lpthread

echo "done -> $OUT"
