#!/bin/sh
# Run only against the copied test harness in /tmp/fla-openmpc-test.
set -eu
cd /tmp/fla-openmpc-test
LIB="$PWD/forceLinkAudioAddin.so"
run_case() {
 enabled=$1
 printf 'enabled=%s\nport=19000\npeer=Force Link Audio Test\nchannel=Lifecycle Test\n' "$enabled" > force-link-audio.conf
 LD_PRELOAD="$LIB" ./MPC > harness.log 2>&1 & parent=$!
 sleep 2
 kill -0 "$parent"
 children=$(ps -o pid,ppid,args | awk -v owner="$parent" '$2 == owner {print $1}')
 if [ "$enabled" = 0 ]; then [ -z "$children" ]; echo 'PASS disabled: no publisher child'
 else
  [ -n "$children" ]; echo "PASS enabled: publisher child $children"
  for child in $children; do kill -0 "$child"; done
 fi
 kill "$parent"; wait "$parent" 2>/dev/null || true
 sleep 1
 for child in $children; do [ ! -d /proc/$child ] || [ "$(awk '/^State:/{print $2}' /proc/$child/status)" = Z ]; done
 echo 'PASS parent exit: no running orphan'
}
chmod 755 MPC
chmod 644 forceLinkAudio
run_case 0
run_case 1
