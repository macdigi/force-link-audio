#!/bin/sh
# Installer for the MockbaMod addon-manager. The manager unzips the package and
# runs ./install.sh, so this places the folder and enables autostart.
# Also safe to run by hand after a manual copy.

mmPath=$(cat /dev/shm/.mmPath)
. $mmPath/MockbaMod/env.sh

appname=ForceLinkAudio
src=$(cd "$(dirname "$0")" && pwd)
dest="$mmPath/AddOns/$appname"

echo "Installing Force Link Audio ..."

# If we were unzipped somewhere else, move into place.
if [ "$src" != "$dest" ]; then
    mkdir -p "$dest"
    cp -f "$src"/forceStream.so "$src"/forceLinkAudio "$src"/manage.sh \
          "$src"/run_$appname.sh "$src"/VERSION "$dest"/ 2>/dev/null
    # never clobber a config the user has already edited
    [ -f "$dest/config" ] || cp -f "$src"/config "$dest"/ 2>/dev/null
fi

# zip does not preserve the exec bit — restore it every time.
chmod 755 "$dest"/forceLinkAudio "$dest"/forceStream.so \
          "$dest"/manage.sh "$dest"/run_$appname.sh 2>/dev/null

# enable autostart (boot.sh globs *.sh in the AddOns root)
cp -f "$dest/run_$appname.sh" "$mmPath/AddOns/run_$appname.sh"
chmod 755 "$mmPath/AddOns/run_$appname.sh"

echo "Installed and enabled."
echo "Your Force will publish its audio as a Link Audio channel after a reboot."
echo "Settings: AddOns/$appname/config   ·   Disable: sh manage.sh DISABLE"
