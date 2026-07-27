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

#include <atomic>
#include <chrono>
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
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<uint16_t>(port));
  if (::bind(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
  {
    std::fprintf(stderr, "[forceLinkAudio] bind(:%d) failed\n", port);
    return 1;
  }
  std::printf("[forceLinkAudio] listening on udp/%d\n", port);

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
  // cursor is anchored once, then advanced by exactly the audio duration.
  double beatAnchor = 0.0;      // beat at the first committed frame
  uint64_t framesSent = 0;      // total frames committed since the anchor
  bool anchored = false;
  auto lastReport = std::chrono::steady_clock::now();

  FILE* dumpFile = nullptr;
  uint64_t dumpPackets = 0;
  if (::access("/dev/shm/.forceDump", F_OK) == 0)
  {
    dumpFile = std::fopen("/tmp/forcedump.raw", "wb");
    std::printf("[forceLinkAudio] DIAGNOSTIC dump -> /tmp/forcedump.raw\n");
  }

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
        std::printf("[forceLinkAudio]   queued %zu samples, spilled %llu frames\n",
                    pending.size(), static_cast<unsigned long long>(spilled));
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
    if (false && dumpFile && samples)
    {
      // Dump ALL source channels so per-channel levels can be measured — the
      // main mix is not necessarily on the first pair.
      std::fwrite(conv.data(), sizeof(int16_t), samples, dumpFile);
      if (++dumpPackets % 200 == 0) std::fflush(dumpFile);
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

    // DIAGNOSTIC: the daemon's own stereo output, captured BEFORE Link sees
    // it and without needing a subscriber — isolates my conversion/extraction
    // from Link transport and from Compa's receiver.
    if (dumpFile && pending.size() >= 2048)
    {
      std::fwrite(pending.data(), sizeof(int16_t), 2048, dumpFile);
      if (++dumpPackets % 50 == 0) std::fflush(dumpFile);
      if (committed == 0) pending.erase(pending.begin(), pending.begin() + 2048);
    }

    const auto state = link.captureAppSessionState();
    if (!anchored)
    {
      beatAnchor = state.beatAtTime(link.clock().micros(), kQuantum);
      framesSent = 0;
      anchored = true;
    }
    const double tempo = state.tempo() > 0.0 ? state.tempo() : 120.0;

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

      // Beat position derived from the CUMULATIVE frame count, never
      // accumulated per block. Adding a small delta each commit let float
      // error creep in, so consecutive blocks were not perfectly contiguous
      // and the receiver stitched each 512-frame boundary — a repeating
      // discontinuity at ~86 Hz, heard as a buzz.
      const double beatsHere =
        beatAnchor + (static_cast<double>(framesSent) / static_cast<double>(h.rate))
                     * (tempo / 60.0);

      std::memcpy(buf.samples, pending.data(), take * sizeof(int16_t));
      if (buf.commit(state, beatsHere, kQuantum, takeFrames, channels, h.rate))
      {
        ++committed;
        framesSent += takeFrames;
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
