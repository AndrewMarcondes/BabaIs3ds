// Audio (3DS only): one looping Ogg Vorbis music track streamed on ndsp channel 0 by a background thread,
// plus short sound effects decoded whole into memory and played on channels 1..SFX_CHANNELS.
#include "audio.h"
#include <3ds.h>
#include <stdio.h>
#include <string.h>
#include <tremor/ivorbisfile.h>

#define NBUF 4
#define FRAMES 4096 // per buffer

static OggVorbis_File vf;
static int playing;    // vf is open
static int channels;
static ndspWaveBuf wbuf[NBUF];
static s16 *pcm;
static Thread thread;
static LightEvent wake;
static LightLock lock;
static volatile int quit;
static char want[256], cur[256];

#define SFX_CHANNELS 6
#define SFX_CACHE 128
#define SFX_BUDGET (6 * 1024 * 1024) // decoded bytes kept before the cache is emptied (one more sound can exceed it)

typedef struct {
    char path[160];
    s16 *pcm; // linear memory, NULL if the file is missing or undecodable
    u32 frames;
    int channels, rate;
} Sfx;
static Sfx sfx[SFX_CACHE];
static int nsfx, next_sfx;
static size_t sfx_bytes;
static ndspWaveBuf sfx_buf[SFX_CHANNELS];

// Decodes the next chunk into b and queues it; loops at the end of the track.
static void fill(ndspWaveBuf *b) {
    int need = FRAMES * channels * 2, got = 0, loops = 0, bitstream;
    while (got < need) {
        long r = ov_read(&vf, (char *)b->data_pcm16 + got, need - got, &bitstream);
        if (r > 0) got += r;
        else if (r == 0 && loops++ < 1) ov_pcm_seek(&vf, 0);
        else break; // decode error or empty track
    }
    if (!got) return;
    b->nsamples = got / (channels * 2);
    DSP_FlushDataCache(b->data_pcm16, got);
    ndspChnWaveBufAdd(0, b);
}

// ndspChnWaveBufClear takes effect at the next DSP frame (~5ms); wait before reusing or freeing the memory.
static void wait_idle(int first, int last) {
    for (int tries = 0; tries < 20; tries++) {
        int busy = 0;
        for (int c = first; c <= last; c++) busy |= ndspChnIsPlaying(c);
        if (!busy) return;
        svcSleepThread(1000000); // 1ms
    }
}

static void stop(void) {
    ndspChnWaveBufClear(0);
    wait_idle(0, 0);
    if (playing) ov_clear(&vf); // also closes the FILE
    playing = 0;
}

static void start(const char *path) {
    stop();
    FILE *f = fopen(path, "rb");
    if (!f) return;
    if (ov_open(f, &vf, NULL, 0)) { fclose(f); return; }
    vorbis_info *vi = ov_info(&vf, -1);
    channels = vi->channels > 1 ? 2 : 1;
    playing = 1;
    ndspChnReset(0);
    ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
    ndspChnSetRate(0, vi->rate);
    ndspChnSetFormat(0, channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    for (int i = 0; i < NBUF; i++) fill(&wbuf[i]);
}

static void run(void *arg) {
    (void)arg;
    while (!quit) {
        LightLock_Lock(&lock);
        if (strcmp(want, cur)) {
            snprintf(cur, sizeof cur, "%s", want);
            if (cur[0]) start(cur); else stop();
        } else if (playing)
            for (int i = 0; i < NBUF; i++)
                if (wbuf[i].status == NDSP_WBUF_DONE) fill(&wbuf[i]);
        LightLock_Unlock(&lock);
        LightEvent_Wait(&wake);
    }
}

static void on_frame(void *arg) { (void)arg; LightEvent_Signal(&wake); }

int music_init(void) {
    if (R_FAILED(ndspInit())) return -1; // real hardware needs sdmc:/3ds/dspfirm.cdc (dump it with DSP1)
    pcm = linearAlloc(NBUF * FRAMES * 2 * sizeof(s16));
    if (!pcm) { ndspExit(); return -1; }
    memset(wbuf, 0, sizeof wbuf);
    for (int i = 0; i < NBUF; i++) wbuf[i].data_pcm16 = pcm + i * FRAMES * 2;
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    LightEvent_Init(&wake, RESET_ONESHOT);
    LightLock_Init(&lock);
    ndspSetCallback(on_frame, NULL);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    thread = threadCreate(run, NULL, 32 * 1024, prio - 1, -2, false); // above the game loop so audio never starves
    if (!thread) { linearFree(pcm); ndspExit(); return -1; }
    return 0;
}

void music_play(const char *path) {
    if (!thread) return;
    LightLock_Lock(&lock);
    snprintf(want, sizeof want, "%s", path ? path : "");
    LightLock_Unlock(&lock);
    LightEvent_Signal(&wake);
}

static void sfx_clear(void) {
    for (int c = 1; c <= SFX_CHANNELS; c++) ndspChnWaveBufClear(c);
    wait_idle(1, SFX_CHANNELS);
    for (int i = 0; i < nsfx; i++) if (sfx[i].pcm) linearFree(sfx[i].pcm);
    nsfx = 0;
    sfx_bytes = 0;
}

// Decodes a whole .ogg into linear memory (cached by path; missing files are cached too).
static Sfx *sfx_load(const char *path) {
    for (int i = 0; i < nsfx; i++)
        if (!strcmp(sfx[i].path, path)) return &sfx[i];
    if (nsfx == SFX_CACHE || sfx_bytes > SFX_BUDGET) sfx_clear();
    Sfx *e = &sfx[nsfx++];
    memset(e, 0, sizeof *e);
    snprintf(e->path, sizeof e->path, "%s", path);
    FILE *f = fopen(path, "rb");
    if (!f) return e;
    OggVorbis_File v;
    if (ov_open(f, &v, NULL, 0)) { fclose(f); return e; }
    vorbis_info *vi = ov_info(&v, -1);
    ogg_int64_t total = ov_pcm_total(&v, -1);
    e->channels = vi->channels > 1 ? 2 : 1;
    e->rate = vi->rate;
    size_t bytes = total > 0 ? (size_t)total * e->channels * 2 : 0;
    if (bytes && bytes < SFX_BUDGET && (e->pcm = linearAlloc(bytes))) {
        size_t got = 0;
        int bitstream;
        long r;
        while (got < bytes && (r = ov_read(&v, (char *)e->pcm + got, bytes - got, &bitstream)) > 0) got += r;
        e->frames = got / (e->channels * 2);
        DSP_FlushDataCache(e->pcm, got);
        sfx_bytes += bytes;
    }
    ov_clear(&v);
    if (!e->pcm && bytes) nsfx--; // out of memory: don't remember it as silent, try again next time
    return e;
}

void sfx_play(const char *path) {
    if (!thread) return;
    Sfx *e = sfx_load(path);
    if (!e->pcm || !e->frames) return;
    int c = 1 + next_sfx;
    ndspWaveBuf *b = &sfx_buf[next_sfx];
    next_sfx = (next_sfx + 1) % SFX_CHANNELS; // round-robin: the oldest effect gets cut if all are busy
    ndspChnWaveBufClear(c);
    ndspChnReset(c);
    ndspChnSetInterp(c, NDSP_INTERP_LINEAR);
    ndspChnSetRate(c, e->rate);
    ndspChnSetFormat(c, e->channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    memset(b, 0, sizeof *b);
    b->data_pcm16 = e->pcm;
    b->nsamples = e->frames;
    ndspChnWaveBufAdd(c, b);
}

void music_exit(void) {
    if (!thread) return;
    quit = 1;
    LightEvent_Signal(&wake);
    threadJoin(thread, U64_MAX);
    threadFree(thread);
    thread = NULL;
    stop();
    sfx_clear();
    ndspExit();
    linearFree(pcm);
}
