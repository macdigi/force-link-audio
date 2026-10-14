# Force Link Audio — OpenMPC add-in

By Mac Digi / Rare Data. Built originally while connecting Akai Force with COMPA.
Streams the stereo main mix from a standalone Force to Ableton Link Audio receivers
on the same trusted LAN. This is an audio publisher, not a VST instrument or a USB
mode. It does not receive audio back into the Force. Pads and controls stay usable.

## Before installing

Save your current project. Installing or uninstalling restarts MPC unless you use
`-n`. Root SSH access is required; this package does not install a mod. Gen1 armhf
only. This adapter targets Force; other MPC hardware is unverified. A compatible
Link Audio receiver is required (ordinary Link tempo sync alone is not enough).

If the old MockbaMod ForceLinkAudio addon is installed, back up its config and
run its `manage.sh DISABLE` first. Do not run both versions. The new adapter refuses
to activate when the old tap is also in LD_PRELOAD. The old release remains available.

Use the unmodified OpenMPC `install.sh` included in this zip. It installs under
`/data/mpc-addins/force-link-audio`, preserves existing LD_PRELOAD entries and
provides the corresponding uninstaller. Read INSTALL.md for its systemd and
MockbaMod integration, including the shared boot-reload service.

## Enable

Edit `/data/mpc-addins/force-link-audio/force-link-audio.conf`:

    enabled=1
    port=9000
    peer=Akai Force
    channel=Force Main

Values are plain text, not shell commands: do not add quotation marks. Save the
project, then restart MPC using your normal mod/service controls. Find “Force Main”
in your Link Audio receiver. To disable, set `enabled=0` and restart MPC.

Enabling publishes the main mix to Link Audio peers on your local network without
a login. Use a trusted LAN. The internal UDP input is always bound to 127.0.0.1;
it cannot accept audio from another computer. No remote-control server is exposed.
Settings survive package upgrades. Back them up before uninstalling; the shared
uninstaller removes them. Log files: /tmp/forceStream.log and /tmp/forceLinkAudio.log.

The helper uses the system armhf ELF loader because the shared installer stores
payload files with mode 0644. It inherits no LD_PRELOAD libraries, has MPC as its
parent and exits when that parent exits. It does not start in shell/SSH processes.

## Limits and recovery

Stereo main mix only; receiver buffering and LAN conditions determine latency.
Clock drift is not continuously resampled. Restarting MPC interrupts playback.
If needed, disable this addin from SSH using its installed uninstall.sh with `-n`,
then restart MPC. Do not delete other addins or overwrite their preload entries.
See the repository README and OpenMPC docs/ADDINS.md for recovery details.

## Source and credits

https://github.com/macdigi/force-link-audio
https://raredata.net
https://getquota.app/compa

GPL-2.0-or-later. Ableton Link © Ableton AG, GPL-2.0-or-later; standalone Asio
carries its Boost Software License. Source build pins are in tools/build_addin.sh
and tools/fetch_dependencies.sh. OpenMPC packaging tools by sd88me and contributors;
MockbaMod by MockbaTheBorg and contributors; original LD_PRELOAD pattern credited
to Amit Talwar's customBufferSize. No Akai/inMusic/Ableton endorsement is implied.
