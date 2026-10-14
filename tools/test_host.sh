#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
mkdir -p build/tests
cc -o build/tests/config_test tests/config_test.c -lpthread
build/tests/config_test
git diff --check
