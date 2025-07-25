/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
// snd_dma.c -- main control for any streaming sound output device

#include "sound.h"
#include "common/intreadwrite.h"

#define PAINTBUFFER_SIZE    2048

typedef struct {
    float   left;
    float   right;
} samplepair_t;

dma_t       dma;

cvar_t      *s_khz;

static cvar_t       *s_testsound;
static cvar_t       *s_swapstereo;
static cvar_t       *s_mixahead;

static float    snd_vol;

static int          s_rawend;
static samplepair_t s_rawsamples[MAX_RAW_SAMPLES];

/*
===============================================================================

SOUND LOADING

===============================================================================
*/

#define RESAMPLE \
    for (i = frac = 0; j = frac >> 8, i < outcount; i++, frac += fracstep)

static sfxcache_t *DMA_UploadSfx(sfx_t *sfx)
{
    float stepscale = (float)s_info.rate / dma.speed;   // this is usually 0.5, 1, or 2
    int i, j, frac, fracstep = stepscale * 256;

    int outcount = s_info.samples / stepscale;
    if (!outcount) {
        Com_SetLastError("Resampled to zero length");
        sfx->error = Q_ERR_INVALID_FORMAT;
        return NULL;
    }

    int size = outcount * s_info.width * s_info.channels;
    sfxcache_t *sc = sfx->cache = S_Malloc(sizeof(*sc) + size - 1);

    sc->length = outcount;
    sc->loopstart = s_info.loopstart == -1 ? -1 : s_info.loopstart / stepscale;
    sc->width = s_info.width;
    sc->channels = s_info.channels;
    sc->size = size;

    Q_assert(stepscale == 1 || s_info.samples <= MAX_SFX_SAMPLES);

// resample / decimate to the current source rate
    if (stepscale == 1) // fast special case
        memcpy(sc->data, s_info.data, size);
    else if (sc->width == 1 && sc->channels == 1)
        RESAMPLE sc->data[i] = s_info.data[j];
    else if (sc->width == 2 && sc->channels == 2)
        RESAMPLE memcpy(sc->data + i * 4, s_info.data + j * 4, 4);
    else
        RESAMPLE ((uint16_t *)sc->data)[i] = ((uint16_t *)s_info.data)[j];

    return sc;
}

#undef RESAMPLE

static void DMA_PageInSfx(sfx_t *sfx)
{
    sfxcache_t *sc = sfx->cache;
    if (sc)
        Com_PageInMemory(sc->data, sc->size);
}

/*
===============================================================================

RAW SAMPLES

===============================================================================
*/

#define RESAMPLE \
    for (i = frac = 0, j = s_rawend & (MAX_RAW_SAMPLES - 1); \
         k = frac >> 8, i < outcount; \
         i++, frac += fracstep, j = (j + 1) & (MAX_RAW_SAMPLES - 1))

static bool DMA_RawSamples(int samples, int rate, int width, int channels, const void *data, float volume)
{
    float stepscale = (float)rate / dma.speed;
    int i, j, k, frac, fracstep = stepscale * 256;
    int outcount = samples / stepscale;
    float vol = snd_vol * volume;

    if (s_rawend < s_paintedtime)
        s_rawend = s_paintedtime;

    if (width == 4) {
        const float *src = (const float *)data;
        vol *= 32768;
        if (channels == 2) {
            RESAMPLE {
                s_rawsamples[j].left  = src[k*2+0] * vol;
                s_rawsamples[j].right = src[k*2+1] * vol;
            }
        } else if (channels == 1) {
            RESAMPLE {
                s_rawsamples[j].left  =
                s_rawsamples[j].right = src[k] * vol;
            }
        }
    } else if (width == 2) {
        const int16_t *src = (const int16_t *)data;
        if (channels == 2) {
            RESAMPLE {
                s_rawsamples[j].left  = src[k*2+0] * vol;
                s_rawsamples[j].right = src[k*2+1] * vol;
            }
        } else if (channels == 1) {
            RESAMPLE {
                s_rawsamples[j].left  =
                s_rawsamples[j].right = src[k] * vol;
            }
        }
    } else if (width == 1) {
        const uint8_t *src = (const uint8_t *)data;
        vol *= 256;
        if (channels == 2) {
            RESAMPLE {
                s_rawsamples[j].left  = (src[k*2+0] - 128) * vol;
                s_rawsamples[j].right = (src[k*2+1] - 128) * vol;
            }
        } else if (channels == 1) {
            RESAMPLE {
                s_rawsamples[j].left  =
                s_rawsamples[j].right = (src[k] - 128) * vol;
            }
        }
    }

    s_rawend += outcount;
    return true;
}

#undef RESAMPLE

static int DMA_HaveRawSamples(void)
{
    return Q_clip(s_rawend - s_paintedtime, 0, MAX_RAW_SAMPLES);
}

static int DMA_NeedRawSamples(void)
{
    return MAX_RAW_SAMPLES - DMA_HaveRawSamples();
}

static void DMA_DropRawSamples(void)
{
    memset(s_rawsamples, 0, sizeof(s_rawsamples));
    s_rawend = s_paintedtime;
}

/*
===============================================================================

PAINTBUFFER TRANSFER

===============================================================================
*/

static void TransferStereo16(const samplepair_t *samp, int endtime)
{
    int ltime = s_paintedtime;
    int size = dma.samples >> 1;

    while (ltime < endtime) {
        // handle recirculating buffer issues
        int lpos = ltime & (size - 1);
        int count = min(size - lpos, endtime - ltime);

        // write a linear blast of samples
        int16_t *out = (int16_t *)dma.buffer + (lpos << 1);
        for (int i = 0; i < count; i++, samp++, out += 2) {
            out[0] = Q_clip_int16(samp->left);
            out[1] = Q_clip_int16(samp->right);
        }

        ltime += count;
    }
}

static void TransferStereo(const samplepair_t *samp, int endtime)
{
    const float *p = (const float *)samp;
    int count = (endtime - s_paintedtime) * dma.channels;
    int out_mask = dma.samples - 1;
    int out_idx = s_paintedtime * dma.channels & out_mask;
    int step = 3 - dma.channels;
    int val;

    if (dma.samplebits == 16) {
        int16_t *out = (int16_t *)dma.buffer;
        while (count--) {
            val = *p;
            p += step;
            out[out_idx] = Q_clip_int16(val);
            out_idx = (out_idx + 1) & out_mask;
        }
    } else if (dma.samplebits == 8) {
        uint8_t *out = (uint8_t *)dma.buffer;
        while (count--) {
            val = *p;
            p += step;
            out[out_idx] = (Q_clip_int16(val) >> 8) + 128;
            out_idx = (out_idx + 1) & out_mask;
        }
    }
}

static void TransferPaintBuffer(samplepair_t *samp, int endtime)
{
    int i;

    if (s_testsound->integer) {
        // write a fixed sine wave
        for (i = 0; i < endtime - s_paintedtime; i++) {
            samp[i].left = samp[i].right = sinf((s_paintedtime + i) * 0.1f) * 20000;
        }
    }

    if (s_swapstereo->integer) {
        for (i = 0; i < endtime - s_paintedtime; i++) {
            SWAP(float, samp[i].left, samp[i].right);
        }
    }

    if (dma.samplebits == 16 && dma.channels == 2) {
        // optimized case
        TransferStereo16(samp, endtime);
    } else {
        // general case
        TransferStereo(samp, endtime);
    }
}

/*
===============================================================================

UNDERWATER FILTER

===============================================================================
*/

typedef struct {
    float z1, z2;
} hist_t;

static hist_t hist[2];
static float a1, a2, b0, b1, b2;

// Implementation of "high shelf" biquad filter from OpenAL Soft.
static void s_underwater_gain_hf_changed(cvar_t *self)
{
    float gain = Cvar_ClampValue(self, 0.001f, 1);
    float f0norm = 5000.0f / dma.speed;
    float w0 = M_PIf * 2.0f * f0norm;
    float sin_w0 = sinf(w0);
    float cos_w0 = cosf(w0);
    float alpha = sin_w0 / 2.0f * M_SQRT2f;
    float sqrtgain_alpha_2 = 2.0f * sqrtf(gain) * alpha;
    float a0;

    b0 = gain * ((gain+1.0f) + (gain-1.0f) * cos_w0 + sqrtgain_alpha_2);
    b1 = gain * ((gain-1.0f) + (gain+1.0f) * cos_w0) * -2.0f;
    b2 = gain * ((gain+1.0f) + (gain-1.0f) * cos_w0 - sqrtgain_alpha_2);

    a0 =  (gain+1.0f) - (gain-1.0f) * cos_w0 + sqrtgain_alpha_2;
    a1 = ((gain-1.0f) - (gain+1.0f) * cos_w0) * 2.0f;
    a2 =  (gain+1.0f) - (gain-1.0f) * cos_w0 - sqrtgain_alpha_2;

    a1 /= a0, a2 /= a0, b0 /= a0, b1 /= a0, b2 /= a0;
}

static void filter_ch(hist_t *hist, float *samp, int count)
{
    float z1 = hist->z1;
    float z2 = hist->z2;

    for (int i = 0; i < count; i++, samp += 2) {
        float input = *samp;
        float output = input * b0 + z1;
        z1 = input * b1 - output * a1 + z2;
        z2 = input * b2 - output * a2;
        *samp = output;
    }

    hist->z1 = z1;
    hist->z2 = z2;
}

static void underwater_filter(samplepair_t *samp, int count)
{
    filter_ch(&hist[0], &samp->left, count);
    filter_ch(&hist[1], &samp->right, count);
}

/*
===============================================================================

CHANNEL MIXING

===============================================================================
*/

typedef void (*paintfunc_t)(const channel_t *, const sfxcache_t *, int, samplepair_t *);

#define PAINTFUNC(name) \
    static void name(const channel_t *ch, const sfxcache_t *sc, int count, samplepair_t *samp)

PAINTFUNC(PaintMono8)
{
    float leftvol = ch->leftvol * snd_vol * 256;
    float rightvol = ch->rightvol * snd_vol * 256;
    const uint8_t *sfx = sc->data + ch->pos;

    for (int i = 0; i < count; i++, samp++, sfx++) {
        samp->left += (*sfx - 128) * leftvol;
        samp->right += (*sfx - 128) * rightvol;
    }
}

PAINTFUNC(PaintStereoDmix8)
{
    float leftvol = ch->leftvol * snd_vol * (256 * M_SQRT1_2f);
    float rightvol = ch->rightvol * snd_vol * (256 * M_SQRT1_2f);
    const uint8_t *sfx = sc->data + ch->pos * 2;

    for (int i = 0; i < count; i++, samp++, sfx += 2) {
        int sum = (sfx[0] - 128) + (sfx[1] - 128);
        samp->left += sum * leftvol;
        samp->right += sum * rightvol;
    }
}

PAINTFUNC(PaintStereoFull8)
{
    float vol = ch->leftvol * snd_vol * 256;
    const uint8_t *sfx = sc->data + ch->pos * 2;

    for (int i = 0; i < count; i++, samp++, sfx += 2) {
        samp->left += (sfx[0] - 128) * vol;
        samp->right += (sfx[1] - 128) * vol;
    }
}

PAINTFUNC(PaintMono16)
{
    float leftvol = ch->leftvol * snd_vol;
    float rightvol = ch->rightvol * snd_vol;
    const int16_t *sfx = (const int16_t *)sc->data + ch->pos;

    for (int i = 0; i < count; i++, samp++, sfx++) {
        samp->left += *sfx * leftvol;
        samp->right += *sfx * rightvol;
    }
}

PAINTFUNC(PaintStereoDmix16)
{
    float leftvol = ch->leftvol * snd_vol * M_SQRT1_2f;
    float rightvol = ch->rightvol * snd_vol * M_SQRT1_2f;
    const int16_t *sfx = (const int16_t *)sc->data + ch->pos * 2;

    for (int i = 0; i < count; i++, samp++, sfx += 2) {
        int sum = sfx[0] + sfx[1];
        samp->left += sum * leftvol;
        samp->right += sum * rightvol;
    }
}

PAINTFUNC(PaintStereoFull16)
{
    float vol = ch->leftvol * snd_vol;
    const int16_t *sfx = (const int16_t *)sc->data + ch->pos * 2;

    for (int i = 0; i < count; i++, samp++, sfx += 2) {
        samp->left += sfx[0] * vol;
        samp->right += sfx[1] * vol;
    }
}

static const paintfunc_t paintfuncs[] = {
    PaintMono8,
    PaintStereoDmix8,
    PaintStereoFull8,
    PaintMono16,
    PaintStereoDmix16,
    PaintStereoFull16,
};

static void PaintChannels(int endtime)
{
    samplepair_t paintbuffer[PAINTBUFFER_SIZE];
    channel_t *ch;
    int i;
    bool underwater = S_IsUnderWater();

    while (s_paintedtime < endtime) {
        // if paintbuffer is smaller than DMA buffer
        int end = min(endtime, s_paintedtime + PAINTBUFFER_SIZE);

        // start any playsounds
        while (1) {
            playsound_t *ps = PS_FIRST(&s_pendingplays);
            if (PS_TERM(ps, &s_pendingplays))
                break;    // no more pending sounds
            if (ps->begin > s_paintedtime) {
                end = min(end, ps->begin);  // stop here
                break;
            }
            S_IssuePlaysound(ps);
        }

        // clear the paint buffer
        memset(paintbuffer, 0, (end - s_paintedtime) * sizeof(paintbuffer[0]));

        // paint in the channels.
        for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
            int ltime = s_paintedtime;

            while (ltime < end) {
                if (!ch->sfx || (!ch->leftvol && !ch->rightvol))
                    break;

                sfxcache_t *sc = S_LoadSound(ch->sfx);
                if (!sc)
                    break;

                // max painting is to the end of the buffer
                int count = min(end, ch->end) - ltime;

                if (count > 0) {
                    int func = (sc->width - 1) * 3 + (sc->channels - 1) * (S_IsFullVolume(ch) + 1);
                    Q_assert(func < q_countof(paintfuncs));
                    paintfuncs[func](ch, sc, count, &paintbuffer[ltime - s_paintedtime]);
                    ch->pos += count;
                    ltime += count;
                }

                // if at end of loop, restart
                if (ltime >= ch->end) {
                    if (ch->autosound) {
                        // autolooping sounds always go back to start
                        ch->pos = 0;
                        ch->end = ltime + sc->length;
                    } else if (sc->loopstart >= 0) {
                        ch->pos = sc->loopstart;
                        ch->end = ltime + sc->length - ch->pos;
                    } else {
                        // channel just stopped
                        ch->sfx = NULL;
                    }
                }
            }
        }

        if (underwater)
            underwater_filter(paintbuffer, end - s_paintedtime);

        // add from the streaming sound source
        int count = min(end, s_rawend) - s_paintedtime;

        for (i = 0; i < count; i++) {
            int s = (s_paintedtime + i) & (MAX_RAW_SAMPLES - 1);
            paintbuffer[i].left  += s_rawsamples[s].left;
            paintbuffer[i].right += s_rawsamples[s].right;
        }

        // transfer out according to DMA format
        TransferPaintBuffer(paintbuffer, end);
        s_paintedtime = end;
    }
}

static void s_volume_changed(cvar_t *self)
{
    snd_vol = Cvar_ClampValue(self, 0, 1);
}

/*
===============================================================================

INIT / SHUTDOWN

===============================================================================
*/

#ifdef _WIN32
extern const snddma_driver_t    snddma_dsound;
extern const snddma_driver_t    snddma_wave;
#endif

#if USE_SDL
extern const snddma_driver_t    snddma_sdl;
#endif

static const snddma_driver_t *const s_drivers[] = {
#ifdef _WIN32
    &snddma_dsound,
    &snddma_wave,
#endif
#if USE_SDL
    &snddma_sdl,
#endif
    NULL
};

static const snddma_driver_t    *snddma;

static void DMA_SoundInfo(void)
{
    Com_Printf("%5d channels\n", dma.channels);
    Com_Printf("%5d samples\n", dma.samples);
    Com_Printf("%5d samplepos\n", dma.samplepos);
    Com_Printf("%5d samplebits\n", dma.samplebits);
    Com_Printf("%5d submission_chunk\n", dma.submission_chunk);
    Com_Printf("%5d speed\n", dma.speed);
    Com_Printf("%p dma buffer\n", dma.buffer);
}

static bool DMA_Init(void)
{
    sndinitstat_t ret = SIS_FAILURE;
    int i;

    s_khz = Cvar_Get("s_khz", "44", CVAR_ARCHIVE | CVAR_SOUND);
    s_mixahead = Cvar_Get("s_mixahead", "0.1", CVAR_ARCHIVE);
    s_testsound = Cvar_Get("s_testsound", "0", 0);
    s_swapstereo = Cvar_Get("s_swapstereo", "0", 0);
    cvar_t *s_driver = Cvar_Get("s_driver", "", CVAR_SOUND);

    for (i = 0; s_drivers[i]; i++) {
        if (!strcmp(s_drivers[i]->name, s_driver->string)) {
            snddma = s_drivers[i];
            ret = snddma->init();
            break;
        }
    }

    if (ret != SIS_SUCCESS) {
        int tried = i;
        for (i = 0; s_drivers[i]; i++) {
            if (i == tried)
                continue;
            snddma = s_drivers[i];
            if ((ret = snddma->init()) == SIS_SUCCESS)
                break;
        }
        Cvar_Reset(s_driver);
    }

    if (ret != SIS_SUCCESS)
        return false;

    s_underwater_gain_hf->changed = s_underwater_gain_hf_changed;
    s_underwater_gain_hf_changed(s_underwater_gain_hf);

    s_volume->changed = s_volume_changed;
    s_volume_changed(s_volume);

    s_numchannels = MAX_CHANNELS;
    s_supports_float = true;

    Com_Printf("sound sampling rate: %i\n", dma.speed);

    return true;
}

static void DMA_Shutdown(void)
{
    snddma->shutdown();
    snddma = NULL;
    s_numchannels = 0;

    s_underwater_gain_hf->changed = NULL;
    s_volume->changed = NULL;
}

static void DMA_Activate(void)
{
    if (snddma->activate) {
        S_StopAllSounds();
        snddma->activate(s_active);
    }
}

/*
===============================================================================

FRAME UPDATES

===============================================================================
*/

static int DMA_DriftBeginofs(float timeofs)
{
    static int  s_beginofs;
    int         start;

    // drift s_beginofs
    start = cl.frame.servertime * 0.001f * dma.speed + s_beginofs;
    if (start < s_paintedtime) {
        start = s_paintedtime;
        s_beginofs = start - (cl.frame.servertime * 0.001f * dma.speed);
    } else if (start > s_paintedtime + 0.3f * dma.speed) {
        start = s_paintedtime + 0.1f * dma.speed;
        s_beginofs = start - (cl.frame.servertime * 0.001f * dma.speed);
    } else {
        s_beginofs -= 10;
    }

    return timeofs ? start + timeofs * dma.speed : s_paintedtime;
}

static void DMA_ClearBuffer(void)
{
    snddma->begin_painting();
    if (dma.buffer)
        memset(dma.buffer, dma.samplebits == 8 ? 0x80 : 0, dma.samples * dma.samplebits / 8);
    snddma->submit();
}

/*
=================
DMA_Spatialize
=================
*/
static void DMA_Spatialize(channel_t *ch)
{
    vec3_t      origin;

    // anything coming from the view entity will always be full volume
    if (S_IsFullVolume(ch)) {
        ch->leftvol = ch->master_vol;
        ch->rightvol = ch->master_vol;
        return;
    }

    if (ch->fixed_origin) {
        VectorCopy(ch->origin, origin);
    } else {
        S_GetEntityOrigin(ch->entnum, origin);
    }

    S_SpatializeOrigin(origin, ch->master_vol, ch->dist_mult, &ch->leftvol, &ch->rightvol, dma.channels - 1);
}

/*
==================
AddLoopSounds

Entities with a ->sound field will generated looped sounds
that are automatically started, stopped, and merged together
as the entities are sent to the client
==================
*/
static void AddLoopSounds(void)
{
    int         i, j;
    float       left, right, left_total, right_total;
    loopsound_t *loop, *other;
    channel_t   *ch;
    sfx_t       *sfx;
    sfxcache_t  *sc;
    vec3_t      origin;

    for (i = 0; i < s_numloopsounds; i++) {
        loop = &s_loopsounds[i];
        if (loop->framecount == s_framecount)
            continue;

        sfx = loop->sfx;
        sc = sfx->cache;
        if (!sc)
            continue;

        // find the total contribution of all sounds of this type
        S_GetEntityOrigin(loop->entnum, origin);
        S_SpatializeOrigin(origin, loop->volume, loop->dist_mult,
                           &left_total, &right_total, loop->stereo_pan & (dma.channels - 1));
        for (j = i + 1; j < s_numloopsounds; j++) {
            other = &s_loopsounds[j];
            if (other->sfx != loop->sfx)
                continue;
            // don't check this again later
            other->framecount = s_framecount;

            S_GetEntityOrigin(other->entnum, origin);
            S_SpatializeOrigin(origin, other->volume, other->dist_mult,
                               &left, &right, other->stereo_pan & (dma.channels - 1));
            left_total += left;
            right_total += right;
        }

        if (left_total == 0 && right_total == 0)
            continue;       // not audible

        // allocate a channel
        ch = S_PickChannel(0, 0);
        if (!ch)
            return;

        ch->leftvol = min(left_total, 1.0f);
        ch->rightvol = min(right_total, 1.0f);
        ch->master_vol = loop->volume;
        ch->dist_mult = loop->dist_mult;    // for S_IsFullVolume()
        ch->autosound = true;   // remove next frame
        ch->sfx = sfx;
        ch->pos = s_paintedtime % sc->length;
        ch->end = s_paintedtime + sc->length - ch->pos;
    }
}

static int DMA_GetTime(void)
{
    static int      buffers;
    static int      oldsamplepos;
    int fullsamples = dma.samples >> (dma.channels - 1);

// it is possible to miscount buffers if it has wrapped twice between
// calls to S_Update.  Oh well.
    if (dma.samplepos < oldsamplepos) {
        buffers++;      // buffer wrapped
        if (s_paintedtime > 0x40000000) {
            // time to chop things off to avoid 32 bit limits
            buffers = 0;
            s_rawend = s_paintedtime = fullsamples;
            S_StopAllSounds();
        }
    }
    oldsamplepos = dma.samplepos;

    return buffers * fullsamples + (dma.samplepos >> (dma.channels - 1));
}

static void DMA_Update(void)
{
    int         i;
    channel_t   *ch;
    int         samples, soundtime, endtime;
    float       sec;

    // update spatialization for dynamic sounds
    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (!ch->sfx)
            continue;

        if (ch->autosound) {
            // autosounds are regenerated fresh each frame
            memset(ch, 0, sizeof(*ch));
            continue;
        }

        DMA_Spatialize(ch);     // respatialize channel
        if (!ch->leftvol && !ch->rightvol) {
            memset(ch, 0, sizeof(*ch));
            continue;
        }
    }

    s_framecount++;

    // add loopsounds
    AddLoopSounds();

#if USE_DEBUG
    if (s_show->integer) {
        int total = 0;
        for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
            if (ch->sfx && (ch->leftvol || ch->rightvol)) {
                Com_Printf("%.3f %.3f %s\n", ch->leftvol, ch->rightvol, ch->sfx->name);
                total++;
            }
        }
        if (s_show->integer > 1 || total) {
            Com_Printf("----(%i)---- painted: %i\n", total, s_paintedtime);
        }
    }
#endif

    snddma->begin_painting();

    if (!dma.buffer)
        return;

    // update DMA time
    soundtime = DMA_GetTime();

    // check to make sure that we haven't overshot
    if (s_paintedtime < soundtime) {
        Com_DPrintf("%s: overflow\n", __func__);
        s_paintedtime = soundtime;
    }

    // mix ahead of current position
    sec = Cvar_ClampValue(s_mixahead, 0, 1);
    if (!cls.active)
        sec = max(sec, 0.125f);
    endtime = soundtime + sec * dma.speed;

    // mix to an even submission block size
    endtime = Q_ALIGN(endtime, dma.submission_chunk);
    samples = dma.samples >> (dma.channels - 1);
    endtime = min(endtime, soundtime + samples);

    PaintChannels(endtime);

    snddma->submit();
}

static int DMA_GetSampleRate(void)
{
    return dma.speed;
}

const sndapi_t snd_dma = {
    .init = DMA_Init,
    .shutdown = DMA_Shutdown,
    .update = DMA_Update,
    .activate = DMA_Activate,
    .sound_info = DMA_SoundInfo,
    .upload_sfx = DMA_UploadSfx,
    .page_in_sfx = DMA_PageInSfx,
    .raw_samples = DMA_RawSamples,
    .need_raw_samples = DMA_NeedRawSamples,
    .have_raw_samples = DMA_HaveRawSamples,
    .drop_raw_samples = DMA_DropRawSamples,
    .get_begin_ofs = DMA_DriftBeginofs,
    .play_channel = DMA_Spatialize,
    .stop_all_sounds = DMA_ClearBuffer,
    .get_sample_rate = DMA_GetSampleRate,
};
