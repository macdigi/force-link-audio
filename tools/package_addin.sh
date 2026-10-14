#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
TOOLS=${MPC_VST:-.deps/openmpc}
VERSION=${VERSION:-1.1.0}
python3 "$TOOLS/tools/release_addin.py" --dir build/package --version "$VERSION" --repo macdigi/force-link-audio --license GPL-2.0-or-later --about 'Publish the Force main mix to Link Audio peers on a trusted LAN; disabled until enabled in its settings.' --requires 'Force targeted; other MPC hardware unverified. Disable the legacy MockbaMod ForceLinkAudio addon before enabling this package.' -o dist
python3 "$TOOLS/tools/catalog_check.py" "dist/Force-Link-Audio-$VERSION-mpc-armv7.zip" --catalog --expect-id force-link-audio --expect-repo macdigi/force-link-audio
