#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
: "${LINK:?Set LINK to Ableton Link revision 902aef95bf94af49746fdda5369b42cdcfa1e6d2 (with its submodules)}"
mkdir -p build/package
zig cc -target arm-linux-gnueabihf.2.31 -shared -fPIC -O2 -DFORCE_LINK_OPENMPC -o build/package/forceLinkAudioAddin.so src/forceStream.c -lpthread -ldl
zig c++ -target arm-linux-gnueabihf.2.31 -std=c++17 -O2 -I "$LINK/include" -I "$LINK/modules/asio-standalone/asio/include" -DASIO_STANDALONE -DLINK_PLATFORM_LINUX=1 -o build/package/forceLinkAudio src/forceLinkAudio.cpp -lpthread
cp packaging/addin.manifest packaging/force-link-audio.conf LICENSE packaging/THIRD-PARTY-NOTICES.txt build/package/
cp packaging/README.md build/package/README-addin.txt
