/*
 * forceLinkAudio — publishes the Akai Force's audio as an Ableton Link Audio
 * channel, so Compa (or anything else that speaks Link Audio) receives it as a
 * native network source while the Force keeps running fully standalone.
 *
 * ARCHITECTURE
 *   forceStream.so (LD_PRELOAD, inside /usr/bin/MPC)
 *        │  copies every audio buffer MPC plays
 *        │  UDP → 127.0.0.1:9000   (localhost, never touches the wire)
 *        ▼
 *   forceLinkAudio  (this program, separate process)
 *        │  converts S32/float → int16, feeds a LinkAudioSink
 *        ▼
 *   Ableton Link Audio session on the LAN  →  Compa
 *
 *   Keeping the Link stack OUT of the MPC process is deliberate: the shim stays
 *   tiny and real-time safe, and a fault in the Link/asio machinery can never
 *   take down the Force's audio engine.
 *
 * LICENSE NOTE
 *   Ableton Link is GPLv2-or-later. This program links it, so a public release
 *   of this daemon is GPL — which is exactly the intended path for an
 *   open-source MockbaMod fork. Proprietary use needs Ableton's commercial
 *   license.
 *
 * BUILD (macOS host, cross to the Force):
 *   see build_linkaudio.sh
 */

#include <ableton/LinkAudio.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/prctl.h>
#include <signal.h>
#endif

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kMagic = 0x46535452u;  // 'FSTR', matches forceStream.so
constexpr int kDefaultPort = 9000;
constexpr double kQuantum = 4.0;
// One Link buffer must hold a whole UDP packet: 256 frames * 2 ch, with room
// to spare if the shim's packet size is raised later.
constexpr size_t kMaxSamples = 4096;
// One commit = this many frames per channel. ~11.6 ms at 44.1 kHz — a normal
// audio-callback block, instead of one commit per arriving UDP packet.
constexpr size_t kCommitFrames = 1024;
// Which pair of the Force's 4 output channels is the main mix. 0/1 in testing;
// 2/3 is the other pair (the stems lead worth chasing next).
constexpr size_t kMainChanL = 0;
constexpr size_t kMainChanR = 1;
// The cursor is deliberately NOT slewed toward Link's clock every block.
//
// The Force's 44.1 kHz crystal genuinely differs from Link's clock — measured
// here at ~116 ppm — and the textbook fix is to consume one extra (or one
// fewer) frame now and then to close the gap. That was built and tested, and
// it is a bad trade for this application: correcting a clock difference means
// either resampling or splicing samples, and at 116 ppm the splice lands
// roughly every 200 ms, forever. Nudging the beat STAMP instead is worse still
// — Link derives a buffer's end from its own frame count, so any stamp
// adjustment opens a hole between consecutive buffers that a receiver reads as
// a dropout.
//
// 116 ppm is about one beat of drift every few hours. kResyncBeats catches it
// with a single discontinuity, which is far cheaper than continuous splicing.
// If drift ever needs true correction, do it with a real resampler, not by
// dropping frames and not by touching the timeline.
constexpr double kResyncBeats = 2.0;

#pragma pack(push, 1)
struct StreamHeader
{
  uint32_t magic;
  uint32_t seq;
  uint32_t rate;
  uint16_t channels;
  uint16_t format;  // ALSA format enum
  uint32_t frames;
};
#pragma pack(pop)

std::atomic<bool> gRun{true};

/*! Convert one payload to interleaved int16, which is what Link Audio carries.
 *  Returns the number of samples written. */
size_t toInt16(const uint8_t* src, size_t bytes, uint16_t format, int16_t* dst, size_t dstCap)
{
  switch (format)
  {
    case 2:  // SND_PCM_FORMAT_S16_LE — already right
    {
      const size_t n = std::min(bytes / 2, dstCap);
      std::memcpy(dst, src, n * 2);
      return n;
    }
    case 10:  // SND_PCM_FORMAT_S32_LE — the Force's native format
    {
      const size_t n = std::min(bytes / 4, dstCap);
      for (size_t i = 0; i < n; ++i)
      {
        int32_t v;
        std::memcpy(&v, src + i * 4, 4);
        dst[i] = static_cast<int16_t>(v >> 16);
      }
      return n;
    }
    case 14:  // SND_PCM_FORMAT_FLOAT_LE
    {
      const size_t n = std::min(bytes / 4, dstCap);
      for (size_t i = 0; i < n; ++i)
      {
        float f;
        std::memcpy(&f, src + i * 4, 4);
        f = f > 1.0f ? 1.0f : (f < -1.0f ? -1.0f : f);
        dst[i] = static_cast<int16_t>(f * 32767.0f);
      }
      return n;
    }
    default:
      return 0;
  }
}

}  // namespace

int main(int argc, char** argv)
{
#ifdef __linux__
  // The optional owner PID is used by the OpenMPC package. Stop when MPC exits.
  // Check after prctl too, to close the race where MPC exits before registration.
  if (argc > 4) {
    const auto owner = static_cast<pid_t>(std::atoi(argv[4]));
    if (owner <= 1 || prctl(PR_SET_PDEATHSIG, SIGTERM) != 0 || getppid() != owner)
      return 1;
  }
#endif
  const int port = (argc > 1) ? std::atoi(argv[1]) : kDefaultPort;
  const std::string peerName = (argc > 2) ? argv[2] : "Akai Force";
  const std::string chanName = (argc > 3) ? argv[3] : "Force Main";

  std::setvbuf(stdout, nullptr, _IOLBF, 0);
  std::printf("[forceLinkAudio] starting — udp :%d, peer '%s', channel '%s'\n",
              port, peerName.c_str(), chanName.c_str());

  // ---- Link session -------------------------------------------------------
  // The bridge is a PASSIVE peer: it joins to publish audio and to read the
  // session's beat timeline, and never proposes a tempo or a start/stop of its
  // own. Whatever Compa/Live already agreed on stays authoritative — a utility
  // that hijacks session tempo when it connects would be unusable live.
  ableton::LinkAudio link(120.0, peerName);
  link.enable(true);
  link.enableLinkAudio(true);
  link.enableStartStopSync(false);
  link.setPeerName(peerName);

  link.setNumPeersCallback([](std::size_t n) {
    std::printf("[forceLinkAudio] link peers: %zu\n", n);
  });

  ableton::LinkAudioSink sink(link, chanName, kMaxSamples);

  // ---- UDP from the shim --------------------------------------------------
  const int sock = ::socket(AF_INET, SOCK_DGRAM, 0);
  if (sock < 0)
  {
    std::fprintf(stderr, "[forceLinkAudio] socket() failed\n");
    return 1;
  }
  int rcvbuf = 1 << 20;
  ::setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcvbuf, sizeof(rcvbuf));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  // LOOPBACK, not INADDR_ANY. The shim only ever sends to 127.0.0.1, so there
  // is nothing to gain from a wildcard bind and something real to lose: on a
  // shared studio or venue network anyone who could reach the Force's IP could
  // send a datagram with an 'FSTR' header and have it published to every
  // subscriber as this instrument's output, or flood the queue to drown the
  // real audio. Capturing from another host would need to be an explicit
  // opt-in, never the default.
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
  {
    std::fprintf(stderr, "[forceLinkAudio] bind(127.0.0.1:%d) failed\n", port);
    return 1;
  }
  std::printf("[forceLinkAudio] listening on udp/127.0.0.1:%d\n", port);

  std::vector<uint8_t> pkt(65536);
  std::vector<int16_t> conv(kMaxSamples);
  std::vector<int16_t> stereo;
  stereo.reserve(kMaxSamples);
  // Audio waits HERE until Link has a buffer free. Dropping it instead was the
  // 86%-loss bug that made the stream sound like static.
  std::vector<int16_t> pending;
  pending.reserve(1 << 16);
  uint64_t packets = 0, committed = 0, dropped = 0, spilled = 0;
  uint32_t lastSeq = 0;
  bool haveSeq = false;
  // Monotonic beat cursor. Stamping each buffer with "the beat right now" is
  // wrong: the audio was rendered earlier and packets arrive in bursts, so the
  // receiver could not place it on the timeline and discarded it (Starve). The
  // cursor is advanced by exactly the audio duration of each block instead.
  //
  // It must ACCUMULATE, never be recomputed as anchor + cumulativeFrames*tempo.
  // That earlier form re-applied the live tempo to the whole history, so a
  // 0.1 BPM wobble ten minutes in moved every stamp by a full beat and 1 BPM
  // moved it by ten. Link renegotiates tempo continuously once there are
  // several peers, so the stream teleported on the beat grid, the receiver's
  // read window stopped covering it, and it starved — rendering silence and
  // resetting its read cursor, which is what the grain/buzz actually was.
  // Accumulating means a tempo change moves only FUTURE audio, as it should.
  double beatCursor = 0.0;      // beat at the first frame of the next block
  bool anchored = false;
  uint64_t resyncs = 0;         // hard re-anchors after a real discontinuity
  auto lastReport = std::chrono::steady_clock::now();

  while (gRun.load(std::memory_order_relaxed))
  {
    const ssize_t n = ::recv(sock, pkt.data(), pkt.size(), 0);
    if (n < static_cast<ssize_t>(sizeof(StreamHeader))) continue;

    StreamHeader h{};
    std::memcpy(&h, pkt.data(), sizeof(h));
    if (h.magic != kMagic) continue;

    if (haveSeq && h.seq != lastSeq + 1) dropped += (h.seq - lastSeq - 1);
    lastSeq = h.seq;
    haveSeq = true;
    ++packets;

    // Report BEFORE any early-continue below, so the log proves audio is
    // arriving even when nothing has subscribed to the channel yet.
    {
      const auto tnow = std::chrono::steady_clock::now();
      if (tnow - lastReport > std::chrono::seconds(5))
      {
        std::printf(
          "[forceLinkAudio] pkts %llu  committed %llu  gaps %llu  peers %zu  %u Hz %u ch\n",
          static_cast<unsigned long long>(packets),
          static_cast<unsigned long long>(committed),
          static_cast<unsigned long long>(dropped), link.numPeers(), h.rate, h.channels);
        // beatErr is the cursor's standing offset from Link's clock. It should
        // sit near zero and stay there; a value that walks in one direction
        // means the slew is not keeping up and the receiver will lose coverage.
        std::printf("[forceLinkAudio]   queued %zu samples, spilled %llu frames, "
                    "beatErr %+.4f, resyncs %llu, tempo %.2f\n",
                    pending.size(), static_cast<unsigned long long>(spilled),
                    link.captureAppSessionState().beatAtTime(
                      link.clock().micros(), kQuantum) - beatCursor,
                    static_cast<unsigned long long>(resyncs),
                    link.captureAppSessionState().tempo());
        lastReport = tnow;
      }
    }

    const uint8_t* payload = pkt.data() + sizeof(StreamHeader);
    const size_t payloadBytes = static_cast<size_t>(n) - sizeof(StreamHeader);
    const size_t samples = toInt16(payload, payloadBytes, h.format, conv.data(), conv.size());
    if (samples == 0) continue;

    // The Force's main device is FOUR channels (verified: the shim taps
    // "hw:1 44100 Hz, 4 ch"). Link Audio carries 1 or 2, so pull the main
    // stereo pair out of the interleaved frame. Reading 4ch data as if it were
    // stereo is what made the stream grainy — every other pair came from a
    // different output.
    const uint16_t srcCh = h.channels ? h.channels : 2;
    const uint16_t channels = (srcCh == 1) ? 1 : 2;
    if (samples < srcCh) continue;

    stereo.clear();
    if (srcCh <= 2)
    {
      stereo.assign(conv.begin(), conv.begin() + samples);
    }
    else
    {
      const size_t srcFrames = samples / srcCh;
      for (size_t f = 0; f < srcFrames; ++f)
      {
        stereo.push_back(conv[f * srcCh + kMainChanL]);
        stereo.push_back(conv[f * srcCh + kMainChanR]);
      }
    }
    pending.insert(pending.end(), stereo.begin(), stereo.end());

    // Bound the queue: if nothing is subscribed, keep the freshest ~0.5 s and
    // let the rest go rather than growing without limit.
    const size_t maxPending = static_cast<size_t>(h.rate) * channels / 2;
    if (pending.size() > maxPending)
    {
      const size_t excess = pending.size() - maxPending;
      pending.erase(pending.begin(), pending.begin() + excess);
      spilled += excess / channels;
      anchored = false;  // timeline broken by the spill — re-anchor
    }

    const auto state = link.captureAppSessionState();
    const double linkNow = state.beatAtTime(link.clock().micros(), kQuantum);
    if (!anchored)
    {
      // Start from LIVE audio, never from a backlog. Anything already queued
      // was rendered before this anchor, so committing it would advance the
      // cursor without wall time passing and leave a permanent lead — audio
      // stamped a beat early just plays a beat late at the receiver. Measured
      // as a rock-steady beatErr of -1.00 before this trim existed.
      //
      // TRIM to the freshest block rather than clear()ing. The spill path below
      // sets anchored=false, so an outright clear made every spill cost the
      // WHOLE 0.5 s queue instead of just its overflow — and silently, since
      // clear() bumped no counter. With no subscriber that turned into a
      // fill/spill/clear cycle running ~10x a second that the `spilled` figure
      // under-reported by three orders of magnitude.
      const size_t keep = kCommitFrames * channels;
      if (pending.size() > keep)
      {
        const size_t drop = pending.size() - keep;
        pending.erase(pending.begin(),
                      pending.begin() + static_cast<std::ptrdiff_t>(drop));
        spilled += drop / channels;
      }
      beatCursor = linkNow;
      anchored = true;
    }
    const double tempo = state.tempo() > 0.0 ? state.tempo() : 120.0;

    // The Force's 44.1 kHz crystal is not Link's clock. Left purely
    // free-running, the cursor separates from the receiver's read target
    // without bound until coverage is lost for good. So slew toward Link,
    // capped per block at kSlewFraction of its duration — 500 ppm is under a
    // hundredth of a semitone, inaudible, but far more than any real crystal
    // error (~100 ppm). If we are off by more than kResyncBeats it is not
    // drift but a genuine discontinuity, so snap instead of slewing for hours.
    if (std::fabs(linkNow - beatCursor) > kResyncBeats)
    {
      beatCursor = linkNow;
      ++resyncs;
    }

    // Commit in REGULAR fixed-size blocks, the way an audio callback would feed
    // Link. Committing per UDP packet meant ~344 tiny irregular buffers a
    // second, which the receiver had to stitch — audible as grain.
    const size_t blockSamples = kCommitFrames * channels;
    for (;;)
    {
      if (pending.size() < blockSamples) break;

      ableton::LinkAudioSink::BufferHandle buf(sink);
      if (!buf) break;  // no buffer free right now — audio stays in `pending`

      size_t take = std::min(blockSamples, buf.maxNumSamples);
      take = (take / channels) * channels;
      if (take == 0) break;
      const size_t takeFrames = take / channels;

      // This block spans exactly its own duration at the tempo in force NOW,
      // so consecutive stamps stay contiguous and a tempo change never
      // rewrites history. Accumulated in double: at ~43 blocks/sec the
      // rounding stays ~15 digits below the value for any realistic session.
      const double blockBeats =
        (static_cast<double>(takeFrames) / static_cast<double>(h.rate))
        * (tempo / 60.0);

      // Drift correction must NEVER be added to the beat stamp. Link derives a
      // buffer's endBeats as begin + (numFrames/rate)*(tempo/60) — the exact
      // same expression as blockBeats — so if the cursor advanced by anything
      // else, end(n) != begin(n+1) and a hole opens at every block boundary.
      // The receiver walks its slots testing `target >= begin && target < end`,
      // so a target landing in a hole matches NO slot: that is a starve, i.e. a
      // zero-filled callback plus a discarded read position. At 500 ppm that is
      // a tick every few seconds, forever, on a stream whose sender counters
      // all read perfectly healthy. So the timeline advances by exactly
      // blockBeats, and the clock difference is absorbed in the SAMPLE domain
      // instead — which is what a drift corrector actually does.
      const double beatsHere = beatCursor;

      std::memcpy(buf.samples, pending.data(), take * sizeof(int16_t));
      if (buf.commit(state, beatsHere, kQuantum, takeFrames, channels, h.rate))
      {
        ++committed;
        beatCursor += blockBeats;   // exactly contiguous: end(n) == begin(n+1)
        pending.erase(pending.begin(), pending.begin() + take);
      }
      else
      {
        break;  // commit refused — try again on the next packet
      }
    }

  }

  ::close(sock);
  return 0;
}
