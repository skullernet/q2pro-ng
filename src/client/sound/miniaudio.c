/*
Copyright (C) 2025 Andrey Nazarov

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

#include "sound.h"

#define MA_UnpackVector(v)  -(v)[1],(v)[2],-(v)[0]

static ma_engine engine;
static ma_pcm_rb stream_rb;
static ma_sound stream_sound;
static ma_sound_group main_group;
static ma_hishelf_node underwater_filter;
static bool s_underwater_flag;
static bool s_stream_paused;

static void MA_SoundInfo(void)
{
    Com_Printf("%5d channels\n", ma_engine_get_channels(&engine));
    Com_Printf("%5d speed\n", ma_engine_get_sample_rate(&engine));
}

static void s_volume_changed(cvar_t *self)
{
    ma_engine_set_volume(&engine, self->value);
}

static ma_hishelf_config underwater_filter_config(cvar_t *self)
{
    float f = Cvar_ClampValue(self, 0.001f, 1);
    return ma_hishelf2_config_init(ma_format_f32, 2, ma_engine_get_sample_rate(&engine), log10f(f) * 40, 1.0f, 5000.0f);
}

static void s_underwater_gain_hf_changed(cvar_t *self)
{
    ma_hishelf_config config = underwater_filter_config(self);
    ma_hishelf_node_reinit(&config, &underwater_filter);
}

static bool MA_Init(void)
{
    ma_result result = ma_engine_init(NULL, &engine);
    if (result < 0) {
        Com_EPrintf("Miniaudio failed to initialize with error %d\n", result);
        return false;
    }

    result = ma_pcm_rb_init(ma_format_f32, 2, 32 * MAX_RAW_SAMPLES, NULL, NULL, &stream_rb);
    if (result < 0) {
        Com_EPrintf("Ring buffer creation failed with error %d\n", result);
        goto fail1;
    }

    ma_pcm_rb_set_sample_rate(&stream_rb, ma_engine_get_sample_rate(&engine));

    result = ma_sound_init_from_data_source(&engine, &stream_rb, MA_SOUND_FLAG_NO_SPATIALIZATION, NULL, &stream_sound);
    if (result < 0) {
        Com_EPrintf("Stream sound creation failed with error %d\n", result);
        goto fail2;
    }

     ma_hishelf_node_config config = {
        .nodeConfig = ma_node_config_init(),
        .hishelf = underwater_filter_config(s_underwater_gain_hf)
    };
    result = ma_hishelf_node_init(ma_engine_get_node_graph(&engine), &config, NULL, &underwater_filter);
    if (result < 0) {
        Com_EPrintf("Underwater filter creation failed with error %d\n", result);
        goto fail3;
    }
    ma_node_attach_output_bus(&underwater_filter, 0, ma_engine_get_endpoint(&engine), 0);

    result = ma_sound_group_init(&engine, 0, NULL, &main_group);
    if (result < 0) {
        Com_EPrintf("Sound group creation failed with error %d\n", result);
        goto fail4;
    }

    Com_Printf("Miniaudio initialized.\n");
    s_numchannels = MAX_CHANNELS;
    s_supports_float = true;

    s_volume->changed = s_volume_changed;
    s_volume_changed(s_volume);

    s_underwater_gain_hf->changed = s_underwater_gain_hf_changed;

    return true;

fail4:
    ma_hishelf_node_uninit(&underwater_filter, NULL);
fail3:
    ma_sound_uninit(&stream_sound);
fail2:
    ma_pcm_rb_uninit(&stream_rb);
fail1:
    ma_engine_uninit(&engine);
    return false;
}

static void MA_Shutdown(void)
{
    Com_Printf("Shutting down Miniaudio.\n");

    ma_sound_group_uninit(&main_group);
    ma_hishelf_node_uninit(&underwater_filter, NULL);
    ma_sound_uninit(&stream_sound);
    ma_pcm_rb_uninit(&stream_rb);
    ma_engine_uninit(&engine);

    s_volume->changed = NULL;
    s_underwater_gain_hf->changed = NULL;
}

static int MA_UploadSfx(sfx_t *s)
{
    ma_format format;

    switch (s_info.width) {
    case 4:
        format = ma_format_f32;
        break;
    case 2:
        format = ma_format_s16;
        break;
    case 1:
        format = ma_format_u8;
        break;
    default:
        Com_SetLastError("Unsupported sample format");
        return Q_ERR(EINVAL);
    }

    int size = s_info.samples * s_info.width * s_info.channels;
    sfxcache_t *sc = s->cache = S_Malloc(sizeof(*sc) + size - 1);

    sc->length = s_info.samples * 1000LL / s_info.rate; // in msec
    sc->loopstart = s_info.loopstart;
    sc->width = s_info.width;
    sc->format = format;
    sc->channels = s_info.channels;
    sc->samples = s_info.samples;
    sc->rate = s_info.rate;
    sc->size = size;
    memcpy(sc->data, s_info.data, size);

    return Q_ERR_SUCCESS;
}

static void MA_PageInSfx(sfx_t *sfx)
{
    sfxcache_t *sc = sfx->cache;
    if (sc)
        Com_PageInMemory(sc->data, sc->size);
}

static void MA_StopChannel(channel_t *ch)
{
    if (!ch->sfx)
        return;
    ma_node_uninit(&ch->panner, NULL);
    ma_sound_uninit(&ch->sound);
    ma_audio_buffer_ref_uninit(&ch->buffer);
    memset(ch, 0, sizeof(*ch));
}

static void MA_Spatialize(channel_t *ch)
{
    // merged autosounds are handled differently
    if (ch->autosound)
        return;

    // anything coming from the view entity will always be full volume
    bool fullvolume = S_IsFullVolume(ch);

    // update fullvolume flag if needed
    if (ch->fullvolume != fullvolume) {
        ma_sound_set_spatialization_enabled(&ch->sound, !fullvolume);
        ch->fullvolume = fullvolume;
    }

    // update position if needed
    if (!ch->fixed_origin && !ch->fullvolume) {
        vec3_t origin;
        S_GetEntityOrigin(ch->entnum, origin);
        ma_sound_set_position(&ch->sound, MA_UnpackVector(origin));
    }
}

static void MA_PlayChannel(channel_t *ch)
{
    sfxcache_t *sc = ch->sfx->cache;
    ma_result result;
    ma_uint32 flags = 0;

    result = ma_audio_buffer_ref_init(sc->format, sc->channels, sc->data, sc->samples, &ch->buffer);
    if (result < 0)
        goto fail0;
    ch->buffer.sampleRate = sc->rate;

    if (sc->loopstart > 0)
        ma_data_source_set_loop_point_in_pcm_frames(&ch->buffer, sc->loopstart, sc->samples);

    // no attenuation = no spatialization
    if (ch->dist_mult == 0)
        flags |= MA_SOUND_FLAG_NO_SPATIALIZATION;

    if (ch->autosound || sc->loopstart >= 0)
        flags |= MA_SOUND_FLAG_LOOPING;

    result = ma_sound_init_from_data_source(&engine, &ch->buffer, flags, &main_group, &ch->sound);
    if (result < 0)
        goto fail1;

    ma_sound_set_volume(&ch->sound, ch->master_vol);

    if (ch->dist_mult) {
        ma_sound_set_attenuation_model(&ch->sound, ma_attenuation_model_linear);
        ma_sound_set_min_distance(&ch->sound, SOUND_FULLVOLUME);
        ma_sound_set_max_distance(&ch->sound, SOUND_FULLVOLUME + 1.0f / ch->dist_mult);
        if (ch->fixed_origin)
            ma_sound_set_position(&ch->sound, MA_UnpackVector(ch->origin));
        ch->fullvolume = -1; // force update
    } else {
        ch->fullvolume = 1;
    }

    MA_Spatialize(ch);

    // play it
    result = ma_sound_start(&ch->sound);
    if (result < 0)
        goto fail2;
    return;

fail2:
    ma_sound_uninit(&ch->sound);
fail1:
    ma_audio_buffer_ref_uninit(&ch->buffer);
fail0:
    memset(ch, 0, sizeof(*ch));
}

static void MA_StreamStop(void)
{
    ma_sound_stop(&stream_sound);
    ma_pcm_rb_reset(&stream_rb);
    s_stream_paused = false;
}

static void MA_StreamPause(bool paused)
{
    s_stream_paused = paused;

    // force pause if not active
    if (paused || !s_active)
        ma_sound_stop(&stream_sound);
    else
        ma_sound_start(&stream_sound);
}

static bool MA_NeedRawSamples(void)
{
    return ma_pcm_rb_available_write(&stream_rb) >= MAX_RAW_SAMPLES;
}

static bool MA_HaveRawSamples(void)
{
    return ma_pcm_rb_available_read(&stream_rb) > 0;
}

static bool MA_RawSamples(int samples, int rate, int width, int channels, const void *data, float volume)
{
    if (channels != 2 || width != 4 || rate != ma_pcm_rb_get_sample_rate(&stream_rb))
        return false;

    const byte *src = data;
    while (samples > 0) {
        ma_uint32 frames = samples;
        void *buffer;
        if (ma_pcm_rb_acquire_write(&stream_rb, &frames, &buffer) < 0)
            return false;
        if (!frames)
            return false;
        memcpy(buffer, src, frames * 8);
        if (ma_pcm_rb_commit_write(&stream_rb, frames) < 0)
            return false;
        src += frames * 8;
        samples -= frames;
    }

    ma_sound_set_volume(&stream_sound, volume);
    ma_sound_start(&stream_sound);
    s_stream_paused = false;
    return true;
}

static void MA_Activate(void)
{
    S_StopAllSounds();
    MA_StreamPause(s_stream_paused);
}

static void MA_UpdateUnderWater(void)
{
    ma_node     *node;
    bool        underwater = S_IsUnderWater();

    if (s_underwater_flag == underwater)
        return;

    if (underwater)
        node = &underwater_filter;
    else
        node = ma_engine_get_endpoint(&engine);

    ma_node_attach_output_bus(&main_group, 0, node, 0);
    s_underwater_flag = underwater;
}

static void my_panner_node_process_pcm_frames(ma_node *pNode, const float **ppFramesIn,
                                              ma_uint32 *pFrameCountIn, float **ppFramesOut,
                                              ma_uint32 *pFrameCountOut)
{
    my_panner_node *pan = (my_panner_node *)pNode;
    const float *src = ppFramesIn[0];
    float *dst       = ppFramesOut[0];
    float left       = atomic_load(&pan->left);
    float right      = atomic_load(&pan->right);

    for (ma_uint32 i = 0; i < pFrameCountOut[0]; i++, src += 2, dst += 2) {
        dst[0] = src[0] * left;
        dst[1] = src[1] * right;
    }
}

static const ma_node_vtable my_panner_node_vtable = {
    my_panner_node_process_pcm_frames, NULL, 1, 1, 0
};

static ma_result my_panner_node_init(ma_node_graph *pNodeGraph, my_panner_node *pNode)
{
    pNode->channels = 2;

    ma_node_config baseNodeConfig  = ma_node_config_init();
    baseNodeConfig.vtable          = &my_panner_node_vtable;
    baseNodeConfig.pInputChannels  = &pNode->channels;
    baseNodeConfig.pOutputChannels = &pNode->channels;

    return ma_node_init(pNodeGraph, &baseNodeConfig, NULL, pNode);
}

static void MA_MergeLoopSounds(void)
{
    int         i, j;
    float       left, right, left_total, right_total;
    loopsound_t *loop, *other;
    channel_t   *ch;
    sfx_t       *sfx;
    sfxcache_t  *sc;
    vec3_t      origin;
    ma_result   result;

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
                           &left_total, &right_total, loop->stereo_pan);
        for (j = i + 1; j < s_numloopsounds; j++) {
            other = &s_loopsounds[j];
            if (other->sfx != loop->sfx)
                continue;
            // don't check this again later
            other->framecount = s_framecount;

            S_GetEntityOrigin(other->entnum, origin);
            S_SpatializeOrigin(origin, other->volume, other->dist_mult,
                               &left, &right, other->stereo_pan);
            left_total += left;
            right_total += right;
        }

        if (left_total == 0 && right_total == 0)
            continue;       // not audible

        left_total = min(1.0f, left_total);
        right_total = min(1.0f, right_total);

        ch = S_FindAutoChannel(0, sfx);
        if (ch) {
            atomic_store(&ch->panner.left, left_total);
            atomic_store(&ch->panner.right, right_total);
            ch->autoframe = s_framecount;
            ch->end = cls.realtime + sc->length;
            continue;
        }

        // allocate a channel
        ch = S_PickChannel(0, 0);
        if (!ch)
            continue;

        result = ma_audio_buffer_ref_init(sc->format, sc->channels, sc->data, sc->samples, &ch->buffer);
        if (result < 0)
            continue;
        ch->buffer.sampleRate = sc->rate;

        if (sc->loopstart > 0)
            ma_data_source_set_loop_point_in_pcm_frames(&ch->buffer, sc->loopstart, sc->samples);

        result = ma_sound_init_from_data_source(&engine, &ch->buffer,
            MA_SOUND_FLAG_NO_SPATIALIZATION | MA_SOUND_FLAG_LOOPING, &main_group, &ch->sound);
        if (result < 0) {
            ma_audio_buffer_ref_uninit(&ch->buffer);
            continue;
        }

        result = my_panner_node_init(ma_engine_get_node_graph(&engine), &ch->panner);
        if (result < 0) {
            ma_sound_uninit(&ch->sound);
            ma_audio_buffer_ref_uninit(&ch->buffer);
            continue;
        }
        ma_node_attach_output_bus(&ch->sound, 0, &ch->panner, 0);
        ma_node_attach_output_bus(&ch->panner, 0, &main_group, 0);

        atomic_store(&ch->panner.left, left_total);
        atomic_store(&ch->panner.right, right_total);

        ch->autosound = true;   // remove next frame
        ch->autoframe = s_framecount;
        ch->sfx = sfx;
        ch->entnum = loop->entnum;
        ch->master_vol = loop->volume;
        ch->dist_mult = loop->dist_mult;
        ch->end = cls.realtime + sc->length;

        // play it
        result = ma_sound_start(&ch->sound);
        if (result < 0) {
            MA_StopChannel(ch);
            continue;
        }
    }
}

static void MA_Update(void)
{
    int         i;
    channel_t   *ch;

    // set listener parameters
    ma_engine_listener_set_position(&engine, 0, MA_UnpackVector(listener_origin));
    ma_engine_listener_set_direction(&engine, 0, MA_UnpackVector(listener_forward));
    ma_engine_listener_set_world_up(&engine, 0, MA_UnpackVector(listener_up));

    MA_UpdateUnderWater();

    // update spatialization for dynamic sounds
    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (!ch->sfx)
            continue;

        if (ch->autosound) {
            // autosounds are regenerated fresh each frame
            if (ch->autoframe != s_framecount) {
                MA_StopChannel(ch);
                continue;
            }
        } else {
            if (ma_sound_at_end(&ch->sound)) {
                MA_StopChannel(ch);
                continue;
            }
        }

#if USE_DEBUG
        if (s_show->integer) {
            ma_uint64 cursor = 0;
            ma_sound_get_cursor_in_pcm_frames(&ch->sound, &cursor);
            Com_Printf("%d %.1f %"PRId64" %s\n", i, ch->master_vol, cursor, ch->sfx->name);
        }
#endif

        MA_Spatialize(ch);  // respatialize channel
    }

    s_framecount++;

    // add loopsounds
    MA_MergeLoopSounds();
}

static int MA_GetSampleRate(void)
{
    return ma_engine_get_sample_rate(&engine);
}

const sndapi_t snd_miniaudio = {
    .init = MA_Init,
    .shutdown = MA_Shutdown,
    .update = MA_Update,
    .activate = MA_Activate,
    .sound_info = MA_SoundInfo,
    .upload_sfx = MA_UploadSfx,
    .page_in_sfx = MA_PageInSfx,
    .raw_samples = MA_RawSamples,
    .need_raw_samples = MA_NeedRawSamples,
    .have_raw_samples = MA_HaveRawSamples,
    .drop_raw_samples = MA_StreamStop,
    .pause_raw_samples = MA_StreamPause,
    .play_channel = MA_PlayChannel,
    .stop_channel = MA_StopChannel,
    .get_sample_rate = MA_GetSampleRate,
};
