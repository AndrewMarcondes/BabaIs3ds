// Streams one looping Ogg Vorbis track through ndsp on a background thread (3DS only).
#include "music.h"
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

static void stop(void) {
    ndspChnWaveBufClear(0);
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

void music_exit(void) {
    if (!thread) return;
    quit = 1;
    LightEvent_Signal(&wake);
    threadJoin(thread, U64_MAX);
    threadFree(thread);
    thread = NULL;
    stop();
    ndspExit();
    linearFree(pcm);
}
