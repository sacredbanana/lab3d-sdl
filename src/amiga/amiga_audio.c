/*
 * audio.device output for the Amiga port of LAB3D.
 *
 * The shared mixer (AudioCallback() in subs.c) produces interleaved signed
 * 16 bit samples, which is what SDL asked for.  Paula wants signed 8 bit,
 * one stream per hardware channel, so this module pulls a block from the
 * mixer, drops it to 8 bits, splits it across the left and right channels and
 * keeps two buffers per channel in flight so playback never runs dry.
 *
 * There is no audio thread: amiga_audio_service() is called from
 * PL_PumpClock(), which the game already reaches from every wait loop.  The
 * buffers are sized so that one is always playing while the other is being
 * refilled.
 */

#include "amiga/amiga_sys.h"

#include "lab3d.h"
#include "amiga/amiga_video.h"

#define NBUF      2         /* buffers in flight per channel */
#define NCHAN     2         /* Paula channels we use: one left, one right */

/* Paula's colour clock, used to turn a sample rate into a period. */
#define CLOCK_PAL   3546895L
#define CLOCK_NTSC  3579545L

static struct MsgPort  *audioport;
static struct IOAudio  *audioreq;               /* the one that opened it */
static struct IOAudio  *chanreq[NCHAN][NBUF];
static BYTE            *chanbuf[NCHAN][NBUF];
static int              pending[NCHAN][NBUF];

static K_INT16         *mixbuf;                 /* interleaved 16 bit block */
static int              blocksamples;           /* frames per buffer       */
static int              outchannels;            /* 1 = mono, 2 = stereo    */
static int              audio_open;
static int              audio_paused = 1;
static int              nextbuf;

static UBYTE allocmask[] = { 0x03, 0x05, 0x0a, 0x0c };

/* ------------------------------------------------------------------- setup */

static void free_everything(void) {
    int c, b;

    for (c = 0; c < NCHAN; c++)
        for (b = 0; b < NBUF; b++) {
            if (chanreq[c][b] && chanreq[c][b] != audioreq) {
                FreeVec(chanreq[c][b]);
                chanreq[c][b] = NULL;
            }
            if (chanbuf[c][b]) {
                FreeVec(chanbuf[c][b]);
                chanbuf[c][b] = NULL;
            }
        }

    if (audioreq) {
        CloseDevice((struct IORequest *)audioreq);
        FreeVec(audioreq);
        audioreq = NULL;
    }
    if (audioport) {
        DeleteMsgPort(audioport);
        audioport = NULL;
    }
    if (mixbuf) {
        FreeVec(mixbuf);
        mixbuf = NULL;
    }
    audio_open = 0;
}

int PL_OpenAudio(int freq, int chans, int samples) {
    int c, b;
    ULONG colourclock;
    UWORD period;

    amiga_audio_close();

    outchannels  = (chans >= 2) ? 2 : 1;
    blocksamples = samples > 0 ? samples : 512;
    if (blocksamples < 256) blocksamples = 256;

    /*
     * Cap the mixing rate by what the CPU can keep up with.  The shared mixer
     * asks for 44100 Hz whenever Adlib music is enabled, and emulating an
     * OPL2 in software at that rate would eat an 020 alive; Paula tops out
     * around 28kHz in any case.  Music can also simply be turned off in the
     * setup menu, which removes the synthesis cost entirely.
     */
    {
        UWORD attn = SysBase->AttnFlags;
        int cap;

        if (attn & AFF_68060)      cap = 28000;
        else if (attn & AFF_68040) cap = 22050;
        else                       cap = 11025;

        if (freq > cap) freq = cap;
    }
    if (freq < 4000)  freq = 4000;
    if (freq > 28000) freq = 28000;

    colourclock = (GfxBase->DisplayFlags & PAL) ? CLOCK_PAL : CLOCK_NTSC;
    period = (UWORD)(colourclock / freq);
    if (period < 124) period = 124;
    freq = (int)(colourclock / period);       /* the rate we will actually get */

    audioport = CreateMsgPort();
    if (!audioport) goto fail;

    audioreq = AllocVec(sizeof(struct IOAudio), MEMF_PUBLIC | MEMF_CLEAR);
    if (!audioreq) goto fail;

    audioreq->ioa_Request.io_Message.mn_ReplyPort = audioport;
    audioreq->ioa_Request.io_Message.mn_Node.ln_Pri = 0;
    audioreq->ioa_Data   = allocmask;
    audioreq->ioa_Length = sizeof(allocmask);

    if (OpenDevice("audio.device", 0, (struct IORequest *)audioreq, 0) != 0) {
        fprintf(stderr, "Could not allocate two audio channels.\n");
        FreeVec(audioreq);
        audioreq = NULL;
        goto fail;
    }

    mixbuf = AllocVec((size_t)blocksamples * outchannels * sizeof(K_INT16),
                      MEMF_ANY | MEMF_CLEAR);
    if (!mixbuf) goto fail;

    for (c = 0; c < NCHAN; c++)
        for (b = 0; b < NBUF; b++) {
            struct IOAudio *r = AllocVec(sizeof(struct IOAudio),
                                         MEMF_PUBLIC | MEMF_CLEAR);
            if (!r) goto fail;

            *r = *audioreq;
            r->ioa_Request.io_Message.mn_ReplyPort = audioport;
            r->ioa_Request.io_Command = CMD_WRITE;
            r->ioa_Request.io_Flags   = ADIOF_PERVOL;
            /* io_Unit is a channel mask; the device gave us two channels and
               we drive them independently. */
            r->ioa_Request.io_Unit = (struct Unit *)
                ((ULONG)audioreq->ioa_Request.io_Unit &
                 (c == 0 ? 0x09 : 0x06));
            if (!(ULONG)r->ioa_Request.io_Unit)
                r->ioa_Request.io_Unit = audioreq->ioa_Request.io_Unit;
            r->ioa_Period = period;
            r->ioa_Volume = 64;
            r->ioa_Cycles = 1;
            chanreq[c][b] = r;

            chanbuf[c][b] = AllocVec(blocksamples, MEMF_CHIP | MEMF_CLEAR);
            if (!chanbuf[c][b]) goto fail;

            r->ioa_Data   = (UBYTE *)chanbuf[c][b];
            r->ioa_Length = blocksamples;
            pending[c][b] = 0;
        }

    audio_open   = 1;
    audio_paused = 1;
    nextbuf      = 0;

    fprintf(stderr, "Audio: %d Hz %s through audio.device "
                    "(%d sample buffers).\n",
            freq, outchannels == 2 ? "stereo" : "mono", blocksamples);

    return freq;

fail:
    fprintf(stderr, "Audio unavailable; continuing without sound.\n");
    free_everything();
    return freq;
}

void amiga_audio_close(void) {
    int c, b;

    if (audio_open) {
        /* Stop anything still playing and collect the replies. */
        for (c = 0; c < NCHAN; c++)
            for (b = 0; b < NBUF; b++)
                if (pending[c][b] && chanreq[c][b]) {
                    AbortIO((struct IORequest *)chanreq[c][b]);
                    WaitIO((struct IORequest *)chanreq[c][b]);
                    pending[c][b] = 0;
                }
    }
    free_everything();
}

void PL_CloseAudio(void) {
    amiga_audio_close();
}

void PL_PauseAudio(int paused) {
    audio_paused = paused;
    if (!paused && audio_open) {
        /* Prime both buffers so playback starts immediately. */
        amiga_audio_service();
        amiga_audio_service();
    }
}

/* -------------------------------------------------------------- the mixer */

/* Pull one block from the shared mixer and convert it into the two Paula
   buffers.  The mixer writes signed 16 bit; Paula plays signed 8 bit. */
static void fill_block(int b) {
    int i;
    const K_INT16 *s = mixbuf;
    BYTE *l = chanbuf[0][b];
    BYTE *r = chanbuf[1][b];

    memset(mixbuf, 0, (size_t)blocksamples * outchannels * sizeof(K_INT16));
    AudioCallback(NULL, (unsigned char *)mixbuf,
                  blocksamples * outchannels * (int)sizeof(K_INT16));

    if (outchannels == 2) {
        for (i = 0; i < blocksamples; i++) {
            *l++ = (BYTE)(*s++ >> 8);
            *r++ = (BYTE)(*s++ >> 8);
        }
    } else {
        for (i = 0; i < blocksamples; i++) {
            BYTE v = (BYTE)(*s++ >> 8);
            *l++ = v;
            *r++ = v;
        }
    }
}

void amiga_audio_service(void) {
    struct IOAudio *done;
    int c, b;

    if (!audio_open) return;

    /* Reap anything that has finished. */
    while ((done = (struct IOAudio *)GetMsg(audioport)) != NULL)
        for (c = 0; c < NCHAN; c++)
            for (b = 0; b < NBUF; b++)
                if (chanreq[c][b] == done)
                    pending[c][b] = 0;

    if (audio_paused) return;

    /* Refill and re-queue every buffer that is free.  Both channels of a
       buffer pair are started together so they stay in step. */
    for (b = 0; b < NBUF; b++) {
        if (pending[0][b] || pending[1][b])
            continue;

        fill_block(b);

        for (c = 0; c < NCHAN; c++) {
            chanreq[c][b]->ioa_Data   = (UBYTE *)chanbuf[c][b];
            chanreq[c][b]->ioa_Length = blocksamples;
            chanreq[c][b]->ioa_Cycles = 1;
            chanreq[c][b]->ioa_Request.io_Command = CMD_WRITE;
            chanreq[c][b]->ioa_Request.io_Flags   = ADIOF_PERVOL;
            BeginIO((struct IORequest *)chanreq[c][b]);
            pending[c][b] = 1;
        }
    }
}

/* ------------------------------------------------------------- resampling */

/* Nearest-neighbour rate conversion for the 8 bit unsigned sound effects.
   The effects are short and this runs once per effect, not per frame, so a
   simple resampler is the right trade. */
int PL_ResampleU8(const unsigned char *in, int inlen, int infreq,
                  unsigned char *out, int outmax, int outfreq) {
    long step, pos;
    int n, i;

    if (inlen <= 0 || infreq <= 0 || outfreq <= 0)
        return 0;

    if (infreq == outfreq) {
        n = (inlen < outmax) ? inlen : outmax;
        memcpy(out, in, n);
        return n;
    }

    n = (int)(((long)inlen * outfreq) / infreq);
    if (n > outmax) n = outmax;

    step = ((long)infreq << 16) / outfreq;
    pos  = 0;

    for (i = 0; i < n; i++) {
        long idx = pos >> 16;
        out[i] = (idx < inlen) ? in[idx] : 128;
        pos += step;
    }
    return n;
}
