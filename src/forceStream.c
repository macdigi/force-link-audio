/*
 * forceStream.so — LD_PRELOAD audio tap for the Akai Force's MPC application.
 *
 * WHY THIS EXISTS
 *   The Force's standalone engine (/usr/bin/MPC, a JUCE app) opens the ADA2
 *   codec with raw hw: device names and holds it exclusively, so its audio
 *   cannot be reached with an asound.conf plugin, an ALSA loopback (snd-aloop
 *   is not built into this kernel), or a second PCM open. The only seam is the
 *   one MockbaMod already uses for customBufferSize: LD_PRELOAD interposition
 *   inside the live process.
 *
 *   MPC calls snd_pcm_writei/writen (verified against its dynamic symbols — it
 *   does NOT use the mmap path), so a plain function interpose is enough.
 *
 * WHAT IT DOES
 *   Copies every audio buffer MPC plays into a lock-free ring, and a
 *   background thread streams it out over UDP. The Force keeps running fully
 *   standalone — pads, knobs and screen untouched — while its audio leaves
 *   over Ethernet.
 *
 * SAFETY CONTRACT
 *   - Never breaks audio: on ANY failure the call passes straight through to
 *     the real libasound function.
 *   - Audio-thread path does a bounds check and a memcpy. No allocation, no
 *     syscalls, no file I/O, no logging. Config reading, socket setup and
 *     thread creation all happen in a library constructor at load time; the
 *     only lock on the hot path is a pthread_once whose fast path is a relaxed
 *     load. When the tap claims a handle it raises a flag and the sender thread
 *     writes the log line.
 *   - Ring overflow drops data and bumps a counter. It never blocks MPC.
 *   - Disabled by default: with no config file present this is a no-op tap
 *     that only writes a one-line log so you can confirm it loaded.
 *
 * CONFIG
 *   /dev/shm/.forceStream containing "HOST:PORT" (e.g. "192.168.4.50:9000")
 *   enables streaming. Absent = log only.
 *
 * BUILD (on macOS, via zig):
 *   zig cc -target arm-linux-gnueabihf.2.39 -shared -fPIC -O2 \
 *       -o forceStream.so forceStream.c -lpthread
 */

#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdarg.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

/* Opaque ALSA types — we never dereference these, so no ALSA headers needed. */
typedef struct _snd_pcm snd_pcm_t;
typedef struct _snd_pcm_hw_params snd_pcm_hw_params_t;
typedef unsigned long snd_pcm_uframes_t;
typedef long snd_pcm_sframes_t;

#define CFG_PATH   "/dev/shm/.forceStream"
#define LOG_PATH   "/tmp/forceStream.log"
#define RING_BYTES (1u << 19)          /* 512 KiB ≈ 0.68 s of 48k/2ch/f32 */
#define PKT_FRAMES_MAX 256             /* keeps UDP payloads under ~1.2 KB  */
#define FS_MAGIC   0x46535452u         /* 'FSTR' */

/* ---- originals ---------------------------------------------------------- */
static snd_pcm_sframes_t (*orig_writei)(snd_pcm_t *, const void *, snd_pcm_uframes_t);
static snd_pcm_sframes_t (*orig_writen)(snd_pcm_t *, void **, snd_pcm_uframes_t);
static int (*orig_hw_params)(snd_pcm_t *, snd_pcm_hw_params_t *);

/* ALSA queries, resolved lazily; all optional. */
static int (*q_get_format)(const snd_pcm_hw_params_t *, int *);
static int (*q_get_channels)(const snd_pcm_hw_params_t *, unsigned int *);
static int (*q_get_rate)(const snd_pcm_hw_params_t *, unsigned int *, int *);
static const char *(*q_pcm_name)(snd_pcm_t *);

/* ---- state -------------------------------------------------------------- */
static pthread_once_t g_once = PTHREAD_ONCE_INIT;
static int   g_stream_on   = 0;        /* UDP sender armed                   */
static int   g_sock        = -1;
static struct sockaddr_in g_dst;

/* Format learned from the configured hw_params. Defaults are a safe guess
 * that gets corrected the moment MPC configures the device. */
static volatile unsigned g_rate     = 48000;
static volatile unsigned g_channels = 2;
static volatile int      g_format   = 14;   /* SND_PCM_FORMAT_FLOAT_LE */
static volatile unsigned g_frame_bytes = 8; /* channels * sample width */

/* Per-handle stream shape. MPC configures hw:1 as 4ch AND as 2ch; tapping both
 * into one ring interleaves two different frame sizes, which is heard as grain
 * and static. Each handle's shape is recorded here, and only ONE handle is ever
 * tapped (the first that actually writes audio). */
#define MAX_PCMS 8
static struct { void *pcm; unsigned frame_bytes, channels, rate; int format;
                volatile uint32_t peak;      /* running |max| of this handle   */
                volatile uint64_t frames;    /* frames written by this handle  */ }
    g_pcms[MAX_PCMS];
static volatile void *g_tap_pcm = NULL;   /* the one handle we copy from */

/* SPSC ring: audio thread writes head, sender thread reads tail.
 * head/tail are accessed with explicit release/acquire rather than plain
 * `volatile`. This is a weakly-ordered quad-core ARMv7 (RK3288): volatile stops
 * the COMPILER reordering but not the CPU, so without a release the sender can
 * observe a new head before the audio bytes it publishes have landed. The
 * window is small enough that it has never been heard, but a barrier costs one
 * dmb per ~128-frame buffer, which is free at this rate — cheaper than relying
 * on a timing argument. */
static unsigned char g_ring[RING_BYTES];
static uint32_t g_head = 0;
static uint32_t g_tail = 0;
static volatile uint64_t g_frames_seen = 0;
static volatile uint64_t g_drops = 0;
static volatile int g_announce_claim = 0;   /* set on RT thread, logged by sender */

static void fs_log(const char *fmt, ...)
{
    FILE *f = fopen(LOG_PATH, "a");
    if (!f) return;
    va_list ap;
    va_start(ap, fmt);
    vfprintf(f, fmt, ap);
    va_end(ap);
    fputc('\n', f);
    fclose(f);
}

static unsigned width_of(int fmt)
{
    switch (fmt) {
        case 0:  case 1:  return 1;   /* S8 / U8            */
        case 2:  case 3:  return 2;   /* S16_LE / S16_BE    */
        case 6:  case 7:  return 3;   /* S24_3LE / 3BE      */
        case 10: case 11: return 4;   /* S32_LE / S32_BE    */
        case 14: case 15: return 4;   /* FLOAT_LE / FLOAT_BE*/
        default:          return 4;
    }
}

/* ---- sender thread ------------------------------------------------------ */
struct fs_hdr {
    uint32_t magic;
    uint32_t seq;
    uint32_t rate;
    uint16_t channels;
    uint16_t format;
    uint32_t frames;
} __attribute__((packed));

static void *sender_main(void *unused)
{
    (void)unused;
    static unsigned char pkt[sizeof(struct fs_hdr) + PKT_FRAMES_MAX * 32];
    uint32_t seq = 0;
    unsigned idle_ticks = 0;
    for (;;) {
        if (g_announce_claim) {
            g_announce_claim = 0;
            fs_log("[forceStream] tapping: %u Hz, %u ch, fmt %d (%u B/frame)",
                   g_rate, g_channels, g_format, g_frame_bytes);
        }
        /* ACQUIRE: pairs with the tap's release so the bytes are visible. */
        uint32_t head = __atomic_load_n(&g_head, __ATOMIC_ACQUIRE);
        uint32_t tail = g_tail;                 /* we are the only writer of tail */
        uint32_t avail = (head - tail) & (RING_BYTES - 1);
        unsigned fb = g_frame_bytes ? g_frame_bytes : 8;
        uint32_t chunk = PKT_FRAMES_MAX * fb;
        if (chunk > sizeof(pkt) - sizeof(struct fs_hdr))
            chunk = ((sizeof(pkt) - sizeof(struct fs_hdr)) / fb) * fb;

        if (avail < fb) {                       /* nothing worth sending yet */
            struct timespec ts = {0, 2 * 1000 * 1000};
            nanosleep(&ts, NULL);
            if (++idle_ticks >= 2500) {         /* ~5 s of idling */
                idle_ticks = 0;
                int i;
                for (i = 0; i < MAX_PCMS; i++) {
                    if (!g_pcms[i].pcm) continue;
                    fs_log("[forceStream] handle %d: %u ch, %llu frames, peak %u (%s)",
                           i, g_pcms[i].channels,
                           (unsigned long long)g_pcms[i].frames, g_pcms[i].peak,
                           (g_pcms[i].pcm == g_tap_pcm) ? "TAPPED" : "idle");
                }
            }
            continue;
        }
        if (avail > chunk) avail = chunk;
        avail = (avail / fb) * fb;              /* whole frames only         */

        struct fs_hdr *h = (struct fs_hdr *)pkt;
        h->magic    = FS_MAGIC;
        h->seq      = seq++;
        h->rate     = g_rate;
        h->channels = (uint16_t)g_channels;
        h->format   = (uint16_t)g_format;
        h->frames   = avail / fb;

        /* copy out of the ring, handling wrap */
        uint32_t first = RING_BYTES - tail;
        if (first > avail) first = avail;
        memcpy(pkt + sizeof(*h), g_ring + tail, first);
        if (avail > first)
            memcpy(pkt + sizeof(*h) + first, g_ring, avail - first);
        /* RELEASE: the copies out must complete before the space is freed. */
        __atomic_store_n(&g_tail, (tail + avail) & (RING_BYTES - 1),
                         __ATOMIC_RELEASE);

        if (g_sock >= 0)
            sendto(g_sock, pkt, sizeof(*h) + avail, 0,
                   (struct sockaddr *)&g_dst, sizeof(g_dst));
    }
    return NULL;
}

/* ---- init --------------------------------------------------------------- */
/* Split deliberately. fs_setup() touches the filesystem, allocates a socket and
 * spawns a thread — none of which may happen on MPC's audio thread, so it runs
 * from a library constructor at load time. fs_resolve() is only dlsym and is
 * kept lazy, because at constructor time libasound may not be mapped yet; it
 * never logs. Doing all of this from the first snd_pcm_writei (as this file
 * used to) meant an fopen and a pthread_create on the RT thread at the instant
 * the first note played — a xrun-shaped hole in the audio, and a plain
 * violation of the SAFETY CONTRACT at the top of this file. */
static void fs_resolve(void)
{
    orig_writei    = dlsym(RTLD_NEXT, "snd_pcm_writei");
    orig_writen    = dlsym(RTLD_NEXT, "snd_pcm_writen");
    orig_hw_params = dlsym(RTLD_NEXT, "snd_pcm_hw_params");
    q_get_format   = dlsym(RTLD_NEXT, "snd_pcm_hw_params_get_format");
    q_get_channels = dlsym(RTLD_NEXT, "snd_pcm_hw_params_get_channels");
    q_get_rate     = dlsym(RTLD_NEXT, "snd_pcm_hw_params_get_rate");
    q_pcm_name     = dlsym(RTLD_NEXT, "snd_pcm_name");
}

#ifdef FORCE_LINK_OPENMPC
#include "addin_runtime.h"
#endif

static void fs_setup(void)
{
    fs_log("[forceStream] loaded into pid %d", (int)getpid());

#ifdef FORCE_LINK_OPENMPC
    if (!fl_prepare()) return;
    int port = fl_cfg.port;
    char buf[128] = "127.0.0.1";
#else
    /* Streaming is opt-in: no config file, no socket, no thread. */
    int fd = open(CFG_PATH, O_RDONLY);
    if (fd < 0) {
        fs_log("[forceStream] no %s — tap is log-only", CFG_PATH);
        return;
    }
    char buf[128] = {0};
    ssize_t n = read(fd, buf, sizeof(buf) - 1);
    close(fd);
    if (n <= 0) return;

    char *colon = strchr(buf, ':');
    if (!colon) { fs_log("[forceStream] bad config (want HOST:PORT)"); return; }
    *colon = 0;
    int port = atoi(colon + 1);
    char *nl = strchr(buf, '\n'); if (nl) *nl = 0;

#endif
    g_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (g_sock < 0) { fs_log("[forceStream] socket() failed"); return; }
    memset(&g_dst, 0, sizeof(g_dst));
    g_dst.sin_family = AF_INET;
    g_dst.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, buf, &g_dst.sin_addr) != 1) {
        fs_log("[forceStream] bad host '%s'", buf);
        close(g_sock); g_sock = -1; return;
    }

#ifdef FORCE_LINK_OPENMPC
    if (!fl_spawn()) { close(g_sock); g_sock = -1; return; }
#endif
    pthread_t t;
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&t, &attr, sender_main, NULL) == 0) {
        g_stream_on = 1;
        fs_log("[forceStream] streaming to %s:%d", buf, port);
    } else {
        fs_log("[forceStream] sender thread failed");
        close(g_sock); g_sock = -1;
    }
    pthread_attr_destroy(&attr);
}

/* Runs at library load, on whatever thread the loader uses — never the audio
 * thread. This is the only place that opens files or creates threads. */
__attribute__((constructor))
static void fs_ctor(void) { fs_setup(); }

/* Audio-thread safe: one pthread_once and, on the very first call only, dlsym.
 * No file I/O, no allocation, no logging. */
static inline void ensure_init(void) { pthread_once(&g_once, fs_resolve); }

/* ---- the audio-thread hot path ------------------------------------------ */
static inline void tap(const void *src, size_t bytes)
{
    if (!g_stream_on || !bytes) return;
    uint32_t head = g_head;                                  /* we are the only writer */
    uint32_t tail = __atomic_load_n(&g_tail, __ATOMIC_ACQUIRE);
    uint32_t space = (RING_BYTES - 1) - ((head - tail) & (RING_BYTES - 1));
    if (bytes > space) { g_drops++; return; }      /* never block the engine */
    uint32_t first = RING_BYTES - head;
    if (first > bytes) first = bytes;
    memcpy(g_ring + head, src, first);
    if (bytes > first)
        memcpy(g_ring, (const unsigned char *)src + first, bytes - first);
    /* RELEASE: the copies above must be visible before the new head is. */
    __atomic_store_n(&g_head, (head + (uint32_t)bytes) & (RING_BYTES - 1),
                     __ATOMIC_RELEASE);
}

/* ---- interposed symbols ------------------------------------------------- */
int snd_pcm_hw_params(snd_pcm_t *pcm, snd_pcm_hw_params_t *params)
{
    ensure_init();
    if (!orig_hw_params) return -1;
    int r = orig_hw_params(pcm, params);
    if (r == 0) {
        /* Learn the real stream shape straight from what MPC configured. */
        int fmt = g_format; unsigned ch = g_channels, rate = g_rate, dir = 0;
        if (q_get_format)   q_get_format(params, &fmt);
        if (q_get_channels) q_get_channels(params, &ch);
        if (q_get_rate)     q_get_rate(params, &rate, (int *)&dir);
        if (ch >= 1 && ch <= 32 && rate >= 8000 && rate <= 192000) {
            int slot = -1, i;
            for (i = 0; i < MAX_PCMS; i++) {
                if (g_pcms[i].pcm == (void *)pcm) { slot = i; break; }
                if (!g_pcms[i].pcm && slot < 0) slot = i;
            }
            if (slot >= 0) {
                g_pcms[slot].pcm = (void *)pcm;
                g_pcms[slot].channels = ch;
                g_pcms[slot].rate = rate;
                g_pcms[slot].format = fmt;
                g_pcms[slot].frame_bytes = ch * width_of(fmt);
            }
            /* A reconfigure of the handle we are tapping invalidates whatever is
             * still in the ring — it was captured at the old frame size. */
            if ((void *)pcm == g_tap_pcm)
                __atomic_store_n(&g_tail, __atomic_load_n(&g_head, __ATOMIC_ACQUIRE),
                                 __ATOMIC_RELEASE);
            fs_log("[forceStream] configured %s: %u Hz, %u ch, fmt %d (%u B/frame)%s",
                   (q_pcm_name && pcm) ? q_pcm_name(pcm) : "?",
                   rate, ch, fmt, ch * width_of(fmt),
                   ((void *)pcm == g_tap_pcm) ? "  [TAPPED]" : "");
        }
    }
    return r;
}

snd_pcm_sframes_t snd_pcm_writei(snd_pcm_t *pcm, const void *buffer,
                                 snd_pcm_uframes_t size)
{
    ensure_init();
    if (!orig_writei) return -1;
    snd_pcm_sframes_t r = orig_writei(pcm, buffer, size);
    if (r > 0) {
        int i, slot = -1;
        for (i = 0; i < MAX_PCMS; i++)
            if (g_pcms[i].pcm == (void *)pcm) { slot = i; break; }
        if (slot < 0) return r;                 /* shape unknown — never guess */

        /* Measure this buffer's peak (sparse sample — RT-cheap) so the choice
         * of handle is made on EVIDENCE, not on a guess about which one MPC
         * uses for the music. */
        {
            unsigned w = width_of(g_pcms[slot].format);
            const unsigned char *b = (const unsigned char *)buffer;
            size_t nbytes = (size_t)r * g_pcms[slot].frame_bytes;
            uint32_t pk = 0;
            if (w == 4) {
                size_t n32 = nbytes / 4, i32;
                for (i32 = 0; i32 < n32; i32 += 4) {       /* every 4th sample */
                    int32_t v; memcpy(&v, b + i32 * 4, 4);
                    uint32_t a = (uint32_t)(v < 0 ? -(int64_t)v : v);
                    if (a > pk) pk = a;
                }
            } else if (w == 2) {
                size_t n16 = nbytes / 2, i16;
                for (i16 = 0; i16 < n16; i16 += 4) {
                    int16_t v; memcpy(&v, b + i16 * 2, 2);
                    uint32_t a = (uint32_t)(v < 0 ? -(int32_t)v : v);
                    a <<= 16;                              /* normalise to 32b */
                    if (a > pk) pk = a;
                }
            }
            if (pk > g_pcms[slot].peak) g_pcms[slot].peak = pk;
            g_pcms[slot].frames += (uint64_t)r;
        }

        if (!g_tap_pcm) {
            /* MEASURED 2026-07-27: MPC opens two handles on hw:1 but only ONE
             * ever writes (4ch handle: 1.8M frames; 2ch handle: 0 frames). So
             * there is no "pick the right stream" problem — claim the first
             * handle that actually writes. The ORIGINAL bug was never two
             * streams mixing: hw_params ran 4ch THEN 2ch, leaving the global
             * frame size at 8 bytes while the real writes were 16, so every
             * buffer was copied at half its true length. Using this handle's
             * OWN frame size is the fix. */
            g_tap_pcm = (void *)pcm;
            g_channels = g_pcms[slot].channels;
            g_rate = g_pcms[slot].rate;
            g_format = g_pcms[slot].format;
            g_frame_bytes = g_pcms[slot].frame_bytes;
            /* Do NOT log here — this is the audio thread. Raise a flag and let
             * the sender thread, which wakes every 2 ms anyway, write the line. */
            g_announce_claim = 1;
        }
        if ((void *)pcm != g_tap_pcm) return r; /* a second stream — ignore it */

        g_frames_seen += (uint64_t)r;
        tap(buffer, (size_t)r * g_pcms[slot].frame_bytes);
    }
    return r;
}

snd_pcm_sframes_t snd_pcm_writen(snd_pcm_t *pcm, void **bufs,
                                 snd_pcm_uframes_t size)
{
    ensure_init();
    if (!orig_writen) return -1;
    /* Non-interleaved: pass through untouched. MPC uses writei in practice;
     * interleaving here would cost more than it is worth on the audio thread. */
    return orig_writen(pcm, bufs, size);
}

/* Diagnostics: `kill -USR2 <mpc pid>` is not hooked; instead the counters are
 * dumped when the library unloads, which happens on a clean MPC exit. */
__attribute__((destructor))
static void fs_fini(void)
{
    if (g_frames_seen)
        fs_log("[forceStream] saw %llu frames, %llu ring drops",
               (unsigned long long)g_frames_seen, (unsigned long long)g_drops);
}
