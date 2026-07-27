# Force Link Audio

**Stream your Akai Force's audio over the network as an Ableton Link Audio
channel, while the Force stays fully standalone.** Pads lit, screen alive, no
Computer Mode, nothing flashed.

Any Link Audio receiver picks it up: Ableton Live, an iPad, or another machine
on the same network.

A MockbaMod addon. Free and GPL.

---

## Why

The Force can already send audio over USB, but only in Computer Mode, and that
costs you the instrument. I went looking for a way around it and found out
exactly why it works that way.

The Force's internal USB hub is a **Microchip USB5537B**. Computer Mode
switches that hub's *upstream* from the Force's own CPU over to the computer.
Your control surface sits on a downstream port of that same hub, so when the
computer takes the hub, it takes your pads, knobs and knob screens with it.

That is board topology, not a software lockout. You can confirm it from a shell:
flipping `USB_MUX_CTRL` (GPIO 249, which no kernel driver claims) makes a USB
gadget enumerate on the host from standalone. It works. And every control on
the Force goes dark except the touchscreen, which survives only because it is
I2C rather than USB.

So USB is a dead end for this. **The network is not.** Hence this addon.

## What it does

```
/usr/bin/MPC ──LD_PRELOAD──▶ forceStream.so ──UDP 127.0.0.1──▶ forceLinkAudio
              (taps snd_pcm_writei)                            (LinkAudioSink)
                                                                     │
                                                  Link Audio on the LAN ▼
```

`forceStream.so` interposes ALSA's `snd_pcm_writei` inside the Force
application and copies each audio buffer into a lock-free ring. A background
thread sends it to localhost. `forceLinkAudio` is a **separate process** that
converts to int16 and publishes it as a Link Audio channel.

Keeping the Link stack out of the Force application is deliberate: the tap
stays small and real-time safe, and a fault in the network code can never take
down the audio engine. The tap passes straight through to the real ALSA
function on any error, and drops data rather than ever blocking playback.

The LD_PRELOAD approach is the one MockbaMod already uses for
`customBufferSize` by Amit Talwar. Credit to him for the pattern.

## Install

1. Copy the `ForceLinkAudio` folder to `<SD>/AddOns/` on your MockbaMod card.
2. SSH in and run:
   ```sh
   cd /media/662522/AddOns/ForceLinkAudio
   sh install.sh
   ```
   (`install.sh` restores the exec bits that zip strips, and enables autostart.)
3. Reboot.

The Force publishes a Link Audio channel on every boot from then on.

Or grab the zip from [Releases](../../releases).

## Settings

`AddOns/ForceLinkAudio/config`, edited on a computer, then reboot:

```sh
PEER="Akai Force"      # name receivers see for this device
CHANNEL="Force Main"   # name of the published channel
PORT=9000              # localhost port only
```

## Enable / disable / remove

```sh
sh manage.sh            # status
sh manage.sh DISABLE    # stop, stay installed
sh manage.sh ENABLE     # turn back on
sh manage.sh UNINSTALL  # remove
```

Nothing is written outside the SD card and `/dev/shm`. Pull the card, or
disable, and the Force is stock again.

## Known limits

- **Stereo main mix only.** The Force presents a 4-channel device to ALSA but
  channels 3/4 are silent, so there are no per-track stems available this way.
- **Latency is the receiver's,** not this addon's. Link Audio receivers buffer
  to stay beat-aligned and some buffer generously. Measured transparent through
  Ableton Live's receiver against the Force's own headphone output.
- Tested on MockbaMod, Force firmware 3.x, kernel 6.6.95, armv7l. **Only tested
  on one machine so far** — reports welcome.

## Build

Needs [zig](https://ziglang.org) as a cross-compiler and an Ableton Link
checkout that includes Link Audio.

```sh
cd src
LINK=/path/to/link ./build.sh
```

`src/receive.py` is a plain UDP receiver that writes a WAV, useful for testing
the tap with Link out of the picture.

## License

GPLv2-or-later. This links Ableton Link, which is dual-licensed GPLv2+ /
commercial; this addon ships under the open-source branch. Ableton Link is
© Ableton AG.

Not affiliated with or endorsed by Akai Professional, inMusic, or Ableton AG.
Use at your own risk.
