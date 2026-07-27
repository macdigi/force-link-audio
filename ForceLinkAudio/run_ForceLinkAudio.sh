#!/bin/sh
############################################################
# ForceLinkAudio — autostart hook.
# Copy this file into the AddOns FOLDER ROOT to enable.
# MockbaMod's boot.sh runs every *.sh in AddOns/ at startup.
############################################################

mmPath=$(cat /dev/shm/.mmPath)
. $mmPath/MockbaMod/env.sh
APPDIR="$mmPath/AddOns/ForceLinkAudio"
LIB="$APPDIR/forceStream.so"

# boot.sh calls addon scripts with "kill" on shutdown/restart.
if [ "$1" = "kill" ]; then
    for p in $(ps 2>/dev/null | grep "[f]orceLinkAudio" | awk '{print $1}'); do
        kill -9 $p 2>/dev/null
    done
    exit 0
fi

# ── ARM THE TAP FIRST ──────────────────────────────────────
# boot.sh backgrounds this script (line ~140) and exports LD_PRELOAD a few
# lines later (~171), so this is a race we must win. Do it before anything
# else, and point at the SD path directly — the launcher remounts the card
# exec, and copying first would risk exporting a path that does not exist yet.
if [ -f "$mmLD_PRELOAD_VAR" ]; then
    FC=$(cat "$mmLD_PRELOAD_VAR" | tr " " "\n" | grep -v forceStream | tr "\n" " ")
    echo "$LIB $FC" > "$mmLD_PRELOAD_VAR"
else
    echo "$LIB" > "$mmLD_PRELOAD_VAR"
fi

# ── settings ───────────────────────────────────────────────
PORT=9000
PEER="Akai Force"
CHANNEL="Force Main"
[ -f "$APPDIR/config" ] && . "$APPDIR/config"

echo "127.0.0.1:$PORT" > /dev/shm/.forceStream

# ── start the publisher once the Force app is up ───────────
(
    while true; do
        ps 2>/dev/null | grep -q "{MPC Main Thread}" && break
        sleep 3
    done
    sleep 2
    "$APPDIR/forceLinkAudio" "$PORT" "$PEER" "$CHANNEL" \
        > /tmp/forceLinkAudio.log 2>&1
) &
