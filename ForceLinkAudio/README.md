# Force Link Audio

Stream your **Akai Force's audio over the network** as an Ableton Link Audio
channel — while the Force stays fully standalone. Pads lit, screen alive, no
Computer Mode, no cable to the receiving device.

Any Link Audio receiver picks it up: Ableton Live, Compa on iPad/iPhone, or
another Force running this addon.

## Why this exists

The Force can already send audio over USB — but only in Computer Mode, which
hands its internal USB hub (and therefore its entire control surface) to the
host. Your pads, knobs and knob screens go dark. That is a hardware fact, not a
software choice: the control surface sits on a downstream port of the same hub
that gets switched, so pads and USB-to-host cannot coexist.

The network has no such constraint. This addon taps the audio the Force is
already playing and publishes it over Ethernet or Wi-Fi, leaving the instrument
completely untouched.

## Install

**Via the addon manager** (SSH into the Force, run `addon-manager` or `adm`) if
it appears in your catalog.

**Manually:**

1. Copy the `ForceLinkAudio` folder to `<SD>/AddOns/` on your MockbaMod card.
2. SSH in and run:
   ```
   cd /media/662522/AddOns/ForceLinkAudio
   sh install.sh
   ```
   (`install.sh` restores the exec bits that zip strips, and enables autostart.)
3. Reboot the Force.

That's it. The Force now publishes a Link Audio channel on every boot.

## Settings

Edit `AddOns/ForceLinkAudio/config` on a computer, then reboot:

```sh
PEER="Akai Force"      # name receivers see for this device
CHANNEL="Force Main"   # name of the published channel
PORT=9000              # localhost port only; change if 9000 is taken
```

## Enable / disable / remove

```
sh manage.sh            # status
sh manage.sh DISABLE    # stop, leave installed
sh manage.sh ENABLE     # turn back on
sh manage.sh UNINSTALL  # remove completely
```

Nothing is written outside the SD card and `/dev/shm`, so removing the card —
or just disabling — returns the Force to stock behaviour.

## How it works

```
/usr/bin/MPC ──LD_PRELOAD──▶ forceStream.so ──UDP 127.0.0.1──▶ forceLinkAudio
              (taps snd_pcm_writei)                            (LinkAudioSink)
                                                                     │
                                                  Link Audio on the LAN ▼
```

`forceStream.so` interposes ALSA's `snd_pcm_writei` inside the Force
application and copies each buffer into a lock-free ring; a background thread
sends it to localhost. `forceLinkAudio` is a separate process that converts to
int16 and publishes it as a Link Audio channel.

Keeping the Link stack **out** of the Force application is deliberate — the tap
stays tiny and real-time safe, and a fault in the network code can never take
down the audio engine. The tap passes straight through to the real ALSA
function on any error, and drops data rather than ever blocking playback.

The LD_PRELOAD approach is the one MockbaMod already uses for
`customBufferSize` (Amit Talwar) — this addon follows that pattern.

## Notes and limits

- **Stereo main mix only.** The Force exposes a 4-channel device to ALSA, but
  channels 3/4 are silent — there are no per-track stems available this way.
- **Audio quality is transparent.** Verified against the Force's own headphone
  output through Ableton Live's receiver.
- **Latency depends on the receiver**, not on this addon. Link Audio receivers
  buffer to stay beat-aligned; some buffer generously.
- Tested on MockbaMod with Force firmware 3.x, kernel 6.6.95, armv7l.

## Build from source

Requires [zig](https://ziglang.org) (used purely as a cross-compiler) and the
Ableton Link SDK with Link Audio.

```sh
zig cc  -target arm-linux-gnueabihf.2.39 -shared -fPIC -O2 \
        -o forceStream.so forceStream.c

zig c++ -target arm-linux-gnueabihf.2.39 -std=c++17 -O2 \
        -I <link>/include -I <link>/modules/asio-standalone/asio/include \
        -DASIO_STANDALONE -DLINK_PLATFORM_LINUX=1 \
        -o forceLinkAudio forceLinkAudio.cpp -lpthread
```

## License

GPLv2-or-later. This program links Ableton Link, which is dual-licensed
GPLv2+/commercial; distributing this addon under the GPL is the open-source
branch of that license. Ableton Link is © Ableton AG.

Not affiliated with or endorsed by Akai Professional, inMusic, or Ableton AG.
