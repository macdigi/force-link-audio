#!/bin/sh
# ForceLinkAudio AddOn Manager (MockbaMod convention).
#   sh manage.sh ENABLE | DISABLE | UNINSTALL

appname=ForceLinkAudio
appTitle="Force Link Audio"
appDir=ForceLinkAudio

mmPath=$(cat /dev/shm/.mmPath)
. $mmPath/MockbaMod/env.sh

runDir="$mmPath/AddOns"
installroot="$runDir/$appDir"
runScript="$runDir/run_$appname.sh"
mode=$1

echo "
***********************************************************
*   $appTitle AddOn Manager for MockbaMod
***********************************************************
"

STOP() {
    for p in $(ps 2>/dev/null | grep "[f]orceLinkAudio" | awk '{print $1}'); do
        kill -9 $p 2>/dev/null
    done
    # drop the tap from the preload list so MPC restarts clean
    if [ -f "$mmLD_PRELOAD_VAR" ]; then
        cat "$mmLD_PRELOAD_VAR" | tr " " "\n" | grep -v forceStream | tr "\n" " " > /tmp/.p
        mv /tmp/.p "$mmLD_PRELOAD_VAR"
    fi
    rm -f /dev/shm/.forceStream
}

if [ "$mode" = "UNINSTALL" ]; then
    STOP
    rm -f "$runScript" 2>/dev/null
    rm -rf "$installroot" 2>/dev/null
    echo "<<<< $appTitle uninstalled. Restarting the Force app."
    respawn
    exit 0
fi

if [ "$mode" = "DISABLE" ]; then
    STOP
    rm -f "$runScript" 2>/dev/null
    echo "$appTitle disabled. Restarting the Force app."
    respawn
    exit 0
fi

if [ "$mode" = "ENABLE" ]; then
    cp "$installroot/run_$appname.sh" "$runScript" 2>/dev/null
    chmod 755 "$runScript" 2>/dev/null
    echo "$appTitle enabled. Restarting the Force app."
    respawn
    exit 0
fi

echo "Usage: sh manage.sh ENABLE | DISABLE | UNINSTALL"
echo
echo "Status:"
[ -f "$runScript" ] && echo "  autostart: ENABLED" || echo "  autostart: disabled"
ps 2>/dev/null | grep -q "[f]orceLinkAudio" && echo "  publisher: RUNNING" || echo "  publisher: stopped"
