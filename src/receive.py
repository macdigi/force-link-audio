#!/usr/bin/env python3
"""
Catch the Force's audio stream and write it to a WAV so we can listen to it.

    python3 receive.py [seconds] [out.wav]

Listens on UDP 9000 for forceStream packets, decodes the header, converts
whatever format the Force is running to 16-bit PCM, and writes a WAV.
"""
import socket, struct, sys, wave, time

PORT = 9000
MAGIC = 0x46535452  # 'FSTR'
HDR = struct.Struct("<IIIHHI")  # magic, seq, rate, channels, format, frames

# ALSA format enum -> (bytes per sample, kind)
FMTS = {
    2:  (2, "s16"), 3:  (2, "s16be"),
    10: (4, "s32"), 11: (4, "s32be"),
    14: (4, "f32"), 15: (4, "f32be"),
}

def to_s16(raw, fmt):
    """Convert a payload to little-endian signed 16-bit."""
    width, kind = FMTS.get(fmt, (4, "f32"))
    out = bytearray()
    if kind == "s16":
        return raw
    n = len(raw) // width
    if kind == "f32":
        vals = struct.unpack("<%df" % n, raw[:n * 4])
        for v in vals:
            v = max(-1.0, min(1.0, v))
            out += struct.pack("<h", int(v * 32767))
    elif kind == "s32":
        vals = struct.unpack("<%di" % n, raw[:n * 4])
        for v in vals:
            out += struct.pack("<h", v >> 16)
    else:
        return raw
    return bytes(out)

def main():
    dur = float(sys.argv[1]) if len(sys.argv) > 1 else 10.0
    path = sys.argv[2] if len(sys.argv) > 2 else "force_capture.wav"

    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.setsockopt(socket.SOL_SOCKET, socket.SO_RCVBUF, 1 << 20)
    s.bind(("0.0.0.0", PORT))
    s.settimeout(2.0)
    print("listening on UDP %d for %.0fs ..." % (PORT, dur))

    frames = bytearray()
    rate = ch = fmt = None
    pkts = 0
    lost = 0
    last_seq = None
    t0 = time.time()

    while time.time() - t0 < dur:
        try:
            data, addr = s.recvfrom(65536)
        except socket.timeout:
            continue
        if len(data) < HDR.size:
            continue
        magic, seq, r, c, f, nfr = HDR.unpack(data[:HDR.size])
        if magic != MAGIC:
            continue
        if rate is None:
            rate, ch, fmt = r, c, f
            print("stream: %d Hz, %d ch, alsa fmt %d (from %s)" % (r, c, f, addr[0]))
        if last_seq is not None and seq != last_seq + 1:
            lost += seq - last_seq - 1
        last_seq = seq
        frames += to_s16(data[HDR.size:], f)
        pkts += 1

    s.close()
    if not pkts:
        print("NOTHING RECEIVED — the tap is not streaming.")
        return 1

    # Per-channel analysis BEFORE writing: on a 4ch stream we need to know
    # which pair carries the music and whether the frames line up.
    if ch > 1:
        per = []
        for c in range(ch):
            pk = 0
            for i in range(c * 2, min(len(frames), 2000000), ch * 2):
                pk = max(pk, abs(struct.unpack_from("<h", frames, i)[0]))
            per.append(pk)
        print("per-channel peaks:", " ".join("ch%d=%d" % (i, p) for i, p in enumerate(per)))

    with wave.open(path, "wb") as w:
        w.setnchannels(ch)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(bytes(frames))

    secs = len(frames) / (2.0 * ch * rate)
    peak = 0
    for i in range(0, min(len(frames), 400000), 2):
        v = struct.unpack_from("<h", frames, i)[0]
        peak = max(peak, abs(v))
    print("packets %d  lost %d  wrote %.2fs -> %s  peak %d/32767 (%s)"
          % (pkts, lost, secs, path, peak,
             "SILENT" if peak < 50 else "AUDIO PRESENT"))
    return 0

if __name__ == "__main__":
    sys.exit(main())
