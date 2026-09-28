/*
 * Sound output for the Amiga port of LAB3D.
 *
 * Two back ends are available and the player picks between them in the setup
 * menu ("Sound output"):
 *
 *   Paula - audio.device.  Signed 8 bit, two hardware channels, chip RAM
 *           buffers, available on every Amiga.
 *   AHI   - ahi.device.    Signed 16 bit through whatever the player has set
 *           up in AHI preferences, so a sound card gets full 16 bit output
 *           and the actual output resolution depends on the AHI driver.
 *
 * Both are fed the same way.  The shared mixer (AudioCallback() in subs.c)
 * produces interleaved signed 16 bit samples, which is what SDL asked for and
 * what AHI wants; the Paula back end drops that to 8 bits and splits it
 * across a left and a right channel.  Two buffers are kept in flight so
 * playback never runs dry.
 *
 * There is no audio thread: amiga_audio_service() is called from
 * PL_PumpClock(), which the game already reaches from every wait loop.  The
 * buffers are sized so that one is always playing while the other is being
 * refilled.
 */

#include "amiga/amiga_sys.h"

#include "lab3d.h"
#include "amiga/amiga_video.h"
#include "amiga/amiga_audio.h"

#define NBUF      2         /* buffers in flight */
#define NCHAN     2         /* Paula channels we use: one left, one right */

/* Paula's colour clock, used to turn a sample rate into a period. */
#define CLOCK_PAL   3546895L
#define CLOCK_NTSC  3579545L

/* Which back end is running. */
#define BACKEND_NONE  0
#define BACKEND_PAULA 1
#define BACKEND_AHI   2

/*
 * Setting written to lab3d.cfg as Amiga/audio.
 *
 * Automatic asks for AHI on an 040 or better and Paula below that.  Anybody
 * with a sound card on a slower machine can select AHI explicitly.
 */
int amiga_cfg_audio = AMIGA_AUDIO_AUTO;

static int backend;
static int blocksamples;            /* frames per buffer       */
static int outchannels;             /* 1 = mono, 2 = stereo    */
static int audio_paused = 1;

/* ------------------------------------------------------------ Paula state */

/* Paula plays 8 bit, so its back end needs somewhere to take the mixer's
   16 bit output before shifting it down.  AHI is handed 16 bit directly and
   has no use for this. */
static K_INT16 *mixbuf;             /* interleaved 16 bit block */

static struct MsgPort  *audioport;
static struct IOAudio  *audioreq;               /* the one that opened it */
static struct IOAudio  *chanreq[NCHAN][NBUF];
static BYTE            *chanbuf[NCHAN][NBUF];
static int              pending[NCHAN][NBUF];

static UBYTE allocmask[] = { 0x03, 0x05, 0x0a, 0x0c };

/* -------------------------------------------------------------- AHI state */

static struct MsgPort    *ahiport;
static struct AHIRequest *ahireq[NBUF];         /* ahireq[0] opened it */
static struct AHIRequest *ahilink;              /* newest request in flight */
static K_INT16           *ahibuf[NBUF];
static int                ahipending[NBUF];
static int                ahifreq;
static int                ahiopen;

/* ------------------------------------------------------------------- Paula */

static void paula_free(void) {
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
            pending[c][b] = 0;
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
}

/* Returns the rate actually obtained, or 0 if the channels could not be had. */
static int paula_open(int freq) {
    int c, b;
    ULONG colourclock;
    UWORD period;

    /* Paula tops out around 28kHz whatever the caller asked for. */
    if (freq > 28000) freq = 28000;
    if (freq < 4000)  freq = 4000;

    colourclock = (GfxBase->DisplayFlags & PAL) ? CLOCK_PAL : CLOCK_NTSC;
    period = (UWORD)(colourclock / freq);
    if (period < 124) period = 124;
    freq = (int)(colourclock / period);       /* the rate we will actually get */

    mixbuf = AllocVec((size_t)blocksamples * outchannels * sizeof(K_INT16),
                      MEMF_ANY | MEMF_CLEAR);
    if (!mixbuf) goto fail;

    audioport = CreateMsgPort();
    if (!audioport) goto fail;

    audioreq = AllocVec(sizeof(struct IOAudio), MEMF_PUBLIC | MEMF_CLEAR);
    if (!audioreq) goto fail;

    audioreq->ioa_Request.io_Message.mn_ReplyPort = audioport;
    audioreq->ioa_Request.io_Message.mn_Node.ln_Pri = 0;
    audioreq->ioa_Data   = allocmask;
    audioreq->ioa_Length = sizeof(allocmask);

    if (OpenDevice((STRPTR)"audio.device", 0, (struct IORequest *)audioreq, 0) != 0) {
        fprintf(stderr, "Could not allocate two audio channels.\n");
        FreeVec(audioreq);
        audioreq = NULL;
        goto fail;
    }

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

    return freq;

fail:
    paula_free();
    return 0;
}

static void paula_stop(void) {
    int c, b;

    for (c = 0; c < NCHAN; c++)
        for (b = 0; b < NBUF; b++)
            if (pending[c][b] && chanreq[c][b])
                AbortIO((struct IORequest *)chanreq[c][b]);

    for (c = 0; c < NCHAN; c++)
        for (b = 0; b < NBUF; b++)
            if (pending[c][b] && chanreq[c][b]) {
                WaitIO((struct IORequest *)chanreq[c][b]);
                pending[c][b] = 0;
            }
}

/* Convert one mixer block into the two Paula buffers: the mixer writes signed
   16 bit interleaved, Paula plays signed 8 bit, one stream per channel. */
static void paula_fill(int b) {
    int i;
    const K_INT16 *s = mixbuf;
    BYTE *l = chanbuf[0][b];
    BYTE *r = chanbuf[1][b];

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

static void paula_service(void) {
    struct IOAudio *done;
    int c, b;

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

        memset(mixbuf, 0, (size_t)blocksamples * outchannels * sizeof(K_INT16));
        AudioCallback(NULL, (unsigned char *)mixbuf,
                      blocksamples * outchannels * (int)sizeof(K_INT16));
        paula_fill(b);

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

/* --------------------------------------------------------------------- AHI */

static void ahi_free(void) {
    int b;

    if (ahiopen) {
        CloseDevice((struct IORequest *)ahireq[0]);
        ahiopen = 0;
    }
    if (ahireq[1]) {
        FreeVec(ahireq[1]);
        ahireq[1] = NULL;
    }
    if (ahireq[0]) {
        DeleteIORequest((struct IORequest *)ahireq[0]);
        ahireq[0] = NULL;
    }
    if (ahiport) {
        DeleteMsgPort(ahiport);
        ahiport = NULL;
    }
    for (b = 0; b < NBUF; b++) {
        if (ahibuf[b]) {
            FreeVec(ahibuf[b]);
            ahibuf[b] = NULL;
        }
        ahipending[b] = 0;
    }
    ahilink = NULL;
}

/* Returns the rate actually obtained, or 0 if ahi.device is not usable.
   AHI resamples for us, so whatever we ask for is what the game gets. */
static int ahi_open(int freq) {
    int b;

    ahiport = CreateMsgPort();
    if (!ahiport) goto fail;

    ahireq[0] = (struct AHIRequest *)
        CreateIORequest(ahiport, sizeof(struct AHIRequest));
    if (!ahireq[0]) goto fail;

    /* CMD_WRITE arrived in V4; anything older is of no use here. */
    ahireq[0]->ahir_Version = 4;

    if (OpenDevice((STRPTR)AHINAME, AHI_DEFAULT_UNIT,
                   (struct IORequest *)ahireq[0], 0) != 0)
        goto fail;
    ahiopen = 1;

    ahireq[1] = AllocVec(sizeof(struct AHIRequest), MEMF_PUBLIC | MEMF_CLEAR);
    if (!ahireq[1]) goto fail;
    *ahireq[1] = *ahireq[0];

    for (b = 0; b < NBUF; b++) {
        ahibuf[b] = AllocVec((size_t)blocksamples * outchannels *
                             sizeof(K_INT16), MEMF_PUBLIC | MEMF_CLEAR);
        if (!ahibuf[b]) goto fail;
        ahipending[b] = 0;
    }

    ahilink = NULL;
    ahifreq = freq;
    return freq;

fail:
    ahi_free();
    return 0;
}

/* Stop every write in flight.  They are chained to each other, so ask them
   all to abort before waiting on any of them. */
static void ahi_stop(void) {
    int b;

    for (b = 0; b < NBUF; b++)
        if (ahipending[b])
            AbortIO((struct IORequest *)ahireq[b]);

    for (b = 0; b < NBUF; b++)
        if (ahipending[b]) {
            WaitIO((struct IORequest *)ahireq[b]);
            ahipending[b] = 0;
        }
    ahilink = NULL;
}

static void ahi_service(void) {
    struct AHIRequest *done;
    int b;

    while ((done = (struct AHIRequest *)GetMsg(ahiport)) != NULL) {
        for (b = 0; b < NBUF; b++)
            if (ahireq[b] == done)
                ahipending[b] = 0;
        if (ahilink == done)
            ahilink = NULL;
    }

    if (audio_paused) return;

    for (b = 0; b < NBUF; b++) {
        struct AHIRequest *r;

        if (ahipending[b])
            continue;

        memset(ahibuf[b], 0,
               (size_t)blocksamples * outchannels * sizeof(K_INT16));
        AudioCallback(NULL, (unsigned char *)ahibuf[b],
                      blocksamples * outchannels * (int)sizeof(K_INT16));

        r = ahireq[b];
        r->ahir_Std.io_Command = CMD_WRITE;
        r->ahir_Std.io_Flags   = 0;
        r->ahir_Std.io_Data    = ahibuf[b];
        r->ahir_Std.io_Offset  = 0;
        /* io_Length is in bytes, and must be a whole number of frames. */
        r->ahir_Std.io_Length  = blocksamples * outchannels *
                                 (ULONG)sizeof(K_INT16);
        r->ahir_Type      = (outchannels == 2) ? AHIST_S16S : AHIST_M16S;
        r->ahir_Frequency = ahifreq;
        r->ahir_Volume    = 0x10000;        /* 1.0 */
        r->ahir_Position  = 0x8000;         /* centre */
        /* Chaining onto whatever is still playing is what makes the join
           between two buffers silent. */
        r->ahir_Link      = ahilink;

        SendIO((struct IORequest *)r);
        ahipending[b] = 1;
        ahilink = r;
    }
}

/* ------------------------------------------------------------------- setup */

static int want_ahi(void) {
    switch (amiga_cfg_audio) {
        case AMIGA_AUDIO_PAULA: return 0;
        case AMIGA_AUDIO_AHI:   return 1;
        default:
            return (SysBase->AttnFlags & (AFF_68040 | AFF_68060)) ? 1 : 0;
    }
}

/* Buffers are sized for the rate the game asked for; stretch them when the
   device runs faster so each one still covers the same length of time. */
static void set_blocksamples(int samples, int reqfreq, int freq) {
    blocksamples = samples > 0 ? samples : 512;
    if (freq > reqfreq)
        blocksamples = (int)(((long)blocksamples * freq + reqfreq - 1) /
                             reqfreq);
    if (blocksamples < 256) blocksamples = 256;
    /* The mixer works in whole steps of the sound buffer, up to 4 output
       frames each; a ragged end would be left silent and click. */
    blocksamples = (blocksamples + 3) & ~3;
}

/*
 * The output rate is the player's choice from the setup menu (Amiga/rate in
 * lab3d.cfg) rather than whatever the game asks for: 44100Hz Adlib synthesis
 * is too much for most real machines, and 11025Hz without it sounds rough
 * through AHI, which resamples it to the unit's own rate without
 * interpolation.  22050Hz is the default everywhere.
 */
int amiga_cfg_rate = AMIGA_RATE_DEFAULT;

static const int amiga_rates[AMIGA_RATES] = {
    11025, 22050, 28000, 44100, 48000
};

int amiga_rate_hz(int index) {
    if (index < 0 || index >= AMIGA_RATES) return AMIGA_RATE_DEFAULT;
    return amiga_rates[index];
}

int amiga_rate_index(int hz) {
    int i;

    for (i = 0; i < AMIGA_RATES; i++)
        if (amiga_rates[i] == hz) return i;
    return -1;
}

int PL_OpenAudio(int freq, int chans, int samples) {
    int got = 0;
    int reqfreq = freq;

    amiga_audio_close();

    outchannels  = (chans >= 2) ? 2 : 1;

    freq = (amiga_rate_index(amiga_cfg_rate) >= 0)
           ? amiga_cfg_rate : AMIGA_RATE_DEFAULT;

    if (want_ahi()) {
        set_blocksamples(samples, reqfreq, freq);
        got = ahi_open(freq);
        if (got) {
            backend = BACKEND_AHI;
        } else {
            fprintf(stderr, "Could not open ahi.device V4; "
                            "falling back to Paula.\n");
        }
    }

    if (!got) {
        set_blocksamples(samples, reqfreq, freq);
        got = paula_open(freq);
        if (got) backend = BACKEND_PAULA;
    }

    if (!got) {
        fprintf(stderr, "Audio unavailable; continuing without sound.\n");
        return freq;
    }

    audio_paused = 1;

    fprintf(stderr, "Audio: %d Hz %s, %s (%d sample buffers).\n",
            got, outchannels == 2 ? "stereo" : "mono",
            backend == BACKEND_AHI ? "16 bit through ahi.device"
                                   : "8 bit through audio.device",
            blocksamples);

    return got;
}

void amiga_audio_close(void) {
    switch (backend) {
        case BACKEND_PAULA: paula_stop(); paula_free(); break;
        case BACKEND_AHI:   ahi_stop();   ahi_free();   break;
        default: break;
    }
    backend = BACKEND_NONE;
    audio_paused = 1;
}

void PL_CloseAudio(void) {
    amiga_audio_close();
}

void PL_PauseAudio(int paused) {
    audio_paused = paused;
    if (!paused && backend != BACKEND_NONE) {
        /* Prime both buffers so playback starts immediately. */
        amiga_audio_service();
        amiga_audio_service();
    }
}

void amiga_audio_service(void) {
    switch (backend) {
        case BACKEND_PAULA: paula_service(); break;
        case BACKEND_AHI:   ahi_service();   break;
        default: break;
    }
}

/* ------------------------------------------------------------- resampling */

/* Linear rate conversion for the 8 bit unsigned sound effects.  The effects
   are short and this runs once per effect, not per frame, so it can afford
   to interpolate rather than repeat samples. */
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
        int  a, b, frac;

        if (idx >= inlen) {
            out[i] = 128;
        } else {
            frac = (int)(pos & 0xffff) >> 8;
            a = in[idx];
            b = (idx + 1 < inlen) ? in[idx + 1] : a;
            out[i] = (unsigned char)(a + (((b - a) * frac) >> 8));
        }
        pos += step;
    }
    return n;
}
