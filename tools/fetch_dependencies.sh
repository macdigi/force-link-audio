#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p .deps
[ -d .deps/link/.git ] || git clone --filter=blob:none --no-checkout https://github.com/Ableton/link.git .deps/link
git -C .deps/link checkout 902aef95bf94af49746fdda5369b42cdcfa1e6d2
git -C .deps/link submodule update --init --recursive
[ -d .deps/openmpc/.git ] || git clone --filter=blob:none --no-checkout https://github.com/sd88me/mpc-vst-plugins.git .deps/openmpc
git -C .deps/openmpc checkout e18a0a1da84515096d354f399dd4cd7f9c079fb5
