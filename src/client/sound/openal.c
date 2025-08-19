/*
Copyright (C) 2010 Andrey Nazarov

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
#include "qal.h"

// translates from AL coordinate system to quake
#define AL_UnpackVector(v)  -(v)[1],(v)[2],-(v)[0]
#define AL_CopyVector(a,b)  ((b)[0]=-(a)[1],(b)[1]=(a)[2],(b)[2]=-(a)[0])

// OpenAL implementation should support at least this number of sources
#define MIN_CHANNELS    16

static ALuint       s_srcnums[MAX_CHANNELS];
static ALuint       s_stream;
static ALuint       s_stream_buffers;
static bool         s_stream_paused;
static bool         s_loop_points;
static bool         s_source_spatialize;
static ALint        s_merge_looping_minval;

static ALuint       s_underwater_filter;
static bool         s_underwater_flag;

static bool             s_reverb_supported;
static ALuint           s_reverb_effects[2];
static ALuint           s_reverb_slots[2];
static reverb_preset_t  s_reverb_presets[2];

static const EFXEAXREVERBPROPERTIES s_reverb_params[REVERB_PRESET_MAX - 1] = {
    EFX_REVERB_PRESET_GENERIC,
    EFX_REVERB_PRESET_PADDEDCELL,
    EFX_REVERB_PRESET_ROOM,
    EFX_REVERB_PRESET_BATHROOM,
    EFX_REVERB_PRESET_LIVINGROOM,
    EFX_REVERB_PRESET_STONEROOM,
    EFX_REVERB_PRESET_AUDITORIUM,
    EFX_REVERB_PRESET_CONCERTHALL,
    EFX_REVERB_PRESET_CAVE,
    EFX_REVERB_PRESET_ARENA,
    EFX_REVERB_PRESET_HANGAR,
    EFX_REVERB_PRESET_CARPETEDHALLWAY,
    EFX_REVERB_PRESET_HALLWAY,
    EFX_REVERB_PRESET_STONECORRIDOR,
    EFX_REVERB_PRESET_ALLEY,
    EFX_REVERB_PRESET_FOREST,
    EFX_REVERB_PRESET_CITY,
    EFX_REVERB_PRESET_MOUNTAINS,
    EFX_REVERB_PRESET_QUARRY,
    EFX_REVERB_PRESET_PLAIN,
    EFX_REVERB_PRESET_PARKINGLOT,
    EFX_REVERB_PRESET_SEWERPIPE,
    EFX_REVERB_PRESET_UNDERWATER,
    EFX_REVERB_PRESET_DRUGGED,
    EFX_REVERB_PRESET_DIZZY,
    EFX_REVERB_PRESET_PSYCHOTIC
};

static void AL_LoadEffect(ALuint effect, const EFXEAXREVERBPROPERTIES *reverb)
{
    qalEffecti(effect, AL_EFFECT_TYPE, AL_EFFECT_EAXREVERB);
    qalEffectf(effect, AL_EAXREVERB_DENSITY, reverb->flDensity);
    qalEffectf(effect, AL_EAXREVERB_DIFFUSION, reverb->flDiffusion);
    qalEffectf(effect, AL_EAXREVERB_GAIN, reverb->flGain);
    qalEffectf(effect, AL_EAXREVERB_GAINHF, reverb->flGainHF);
    qalEffectf(effect, AL_EAXREVERB_GAINLF, reverb->flGainLF);
    qalEffectf(effect, AL_EAXREVERB_DECAY_TIME, reverb->flDecayTime);
    qalEffectf(effect, AL_EAXREVERB_DECAY_HFRATIO, reverb->flDecayHFRatio);
    qalEffectf(effect, AL_EAXREVERB_DECAY_LFRATIO, reverb->flDecayLFRatio);
    qalEffectf(effect, AL_EAXREVERB_REFLECTIONS_GAIN, reverb->flReflectionsGain);
    qalEffectf(effect, AL_EAXREVERB_REFLECTIONS_DELAY, reverb->flReflectionsDelay);
    qalEffectfv(effect, AL_EAXREVERB_REFLECTIONS_PAN, reverb->flReflectionsPan);
    qalEffectf(effect, AL_EAXREVERB_LATE_REVERB_GAIN, reverb->flLateReverbGain);
    qalEffectf(effect, AL_EAXREVERB_LATE_REVERB_DELAY, reverb->flLateReverbDelay);
    qalEffectfv(effect, AL_EAXREVERB_LATE_REVERB_PAN, reverb->flLateReverbPan);
    qalEffectf(effect, AL_EAXREVERB_ECHO_TIME, reverb->flEchoTime);
    qalEffectf(effect, AL_EAXREVERB_ECHO_DEPTH, reverb->flEchoDepth);
    qalEffectf(effect, AL_EAXREVERB_MODULATION_TIME, reverb->flModulationTime);
    qalEffectf(effect, AL_EAXREVERB_MODULATION_DEPTH, reverb->flModulationDepth);
    qalEffectf(effect, AL_EAXREVERB_AIR_ABSORPTION_GAINHF, reverb->flAirAbsorptionGainHF);
    qalEffectf(effect, AL_EAXREVERB_HFREFERENCE, reverb->flHFReference);
    qalEffectf(effect, AL_EAXREVERB_LFREFERENCE, reverb->flLFReference);
    qalEffectf(effect, AL_EAXREVERB_ROOM_ROLLOFF_FACTOR, reverb->flRoomRolloffFactor);
    qalEffecti(effect, AL_EAXREVERB_DECAY_HFLIMIT, reverb->iDecayHFLimit);
}

static void AL_StreamStop(void);
static void AL_StopChannel(channel_t *ch);

static void AL_SoundInfo(void)
{
    Com_Printf("AL_VENDOR: %s\n", qalGetString(AL_VENDOR));
    Com_Printf("AL_RENDERER: %s\n", qalGetString(AL_RENDERER));
    Com_Printf("AL_VERSION: %s\n", qalGetString(AL_VERSION));
    Com_Printf("AL_EXTENSIONS: %s\n", qalGetString(AL_EXTENSIONS));
    Com_Printf("Number of sources: %d\n", s_numchannels);
}

static void s_underwater_gain_hf_changed(cvar_t *self)
{
    if (s_underwater_flag) {
        for (int i = 0; i < s_numchannels; i++)
            qalSourcei(s_srcnums[i], AL_DIRECT_FILTER, 0);
        s_underwater_flag = false;
    }

    qalFilterf(s_underwater_filter, AL_LOWPASS_GAINHF, Cvar_ClampValue(self, 0.001f, 1));
}

static void s_volume_changed(cvar_t *self)
{
    qalListenerf(AL_GAIN, self->value);
}

static void s_doppler_factor_changed(cvar_t *self)
{
    qalDopplerFactor(self->value);
}

static void s_speed_of_sound_changed(cvar_t *self)
{
    qalSpeedOfSound(self->value);
}

static bool AL_Init(void)
{
    int i;

    i = QAL_Init();
    if (i == QAL_INIT_FAILED)
        goto fail0;
    s_merge_looping_minval = i;

    Com_DPrintf("AL_VENDOR: %s\n", qalGetString(AL_VENDOR));
    Com_DPrintf("AL_RENDERER: %s\n", qalGetString(AL_RENDERER));
    Com_DPrintf("AL_VERSION: %s\n", qalGetString(AL_VERSION));
    Com_DDPrintf("AL_EXTENSIONS: %s\n", qalGetString(AL_EXTENSIONS));

    // generate source names
    qalGetError();
    qalGenSources(1, &s_stream);
    for (i = 0; i < MAX_CHANNELS; i++) {
        qalGenSources(1, &s_srcnums[i]);
        if (qalGetError() != AL_NO_ERROR) {
            break;
        }
    }

    Com_DPrintf("Got %d AL sources\n", i);

    if (i < MIN_CHANNELS) {
        Com_SetLastError("Insufficient number of AL sources");
        goto fail1;
    }

    s_numchannels = i;

    s_volume->changed = s_volume_changed;
    s_volume_changed(s_volume);

    s_doppler_factor->changed = s_doppler_factor_changed;
    s_doppler_factor_changed(s_doppler_factor);

    s_speed_of_sound->changed = s_speed_of_sound_changed;
    s_speed_of_sound_changed(s_speed_of_sound);

    s_loop_points = qalIsExtensionPresent("AL_SOFT_loop_points");
    s_source_spatialize = qalIsExtensionPresent("AL_SOFT_source_spatialize");
    s_supports_float = qalIsExtensionPresent("AL_EXT_float32");

    // init distance model
    qalDistanceModel(AL_LINEAR_DISTANCE_CLAMPED);

    // init stream source
    qalSourcef(s_stream, AL_ROLLOFF_FACTOR, 0.0f);
    qalSourcei(s_stream, AL_SOURCE_RELATIVE, AL_TRUE);
    if (s_source_spatialize)
        qalSourcei(s_stream, AL_SOURCE_SPATIALIZE_SOFT, AL_FALSE);

    if (qalIsExtensionPresent("AL_SOFT_direct_channels_remix"))
        qalSourcei(s_stream, AL_DIRECT_CHANNELS_SOFT, AL_REMIX_UNMATCHED_SOFT);
    else if (qalIsExtensionPresent("AL_SOFT_direct_channels"))
        qalSourcei(s_stream, AL_DIRECT_CHANNELS_SOFT, AL_TRUE);

    // init underwater filter
    if (qalGenFilters && qalGetEnumValue("AL_FILTER_LOWPASS")) {
        qalGenFilters(1, &s_underwater_filter);
        qalFilteri(s_underwater_filter, AL_FILTER_TYPE, AL_FILTER_LOWPASS);
        s_underwater_gain_hf->changed = s_underwater_gain_hf_changed;
        s_underwater_gain_hf_changed(s_underwater_gain_hf);
    }

    // init reverb effect
    if (qalGenEffects && qalGetEnumValue("AL_EFFECT_EAXREVERB")) {
        qalGenEffects(2, s_reverb_effects);
        qalGenAuxiliaryEffectSlots(2, s_reverb_slots);
        s_reverb_supported = true;
    }

    Com_Printf("OpenAL initialized.\n");
    return true;

fail1:
    QAL_Shutdown();
fail0:
    Com_EPrintf("Failed to initialize OpenAL: %s\n", Com_GetLastError());
    return false;
}

static void AL_Shutdown(void)
{
    Com_Printf("Shutting down OpenAL.\n");

    if (s_numchannels) {
        // delete source names
        qalDeleteSources(s_numchannels, s_srcnums);
        memset(s_srcnums, 0, sizeof(s_srcnums));
        s_numchannels = 0;
    }

    if (s_stream) {
        AL_StreamStop();
        qalDeleteSources(1, &s_stream);
        s_stream = 0;
    }

    if (s_underwater_filter) {
        qalDeleteFilters(1, &s_underwater_filter);
        s_underwater_filter = 0;
    }

    if (s_reverb_supported) {
        qalDeleteEffects(2, s_reverb_effects);
        qalDeleteAuxiliaryEffectSlots(2, s_reverb_slots);
        memset(s_reverb_effects, 0, sizeof(s_reverb_effects));
        memset(s_reverb_slots, 0, sizeof(s_reverb_slots));
        memset(s_reverb_presets, 0, sizeof(s_reverb_presets));
        s_reverb_supported = false;
    }

    s_volume->changed = NULL;
    s_doppler_factor->changed = NULL;
    s_speed_of_sound->changed = NULL;
    s_underwater_gain_hf->changed = NULL;
    s_underwater_flag = false;

    QAL_Shutdown();
}

static ALenum AL_GetSampleFormat(int width, int channels)
{
    if (channels < 1 || channels > 2)
        return 0;

    switch (width) {
    case 1:
        return AL_FORMAT_MONO8 + (channels - 1) * 2;
    case 2:
        return AL_FORMAT_MONO16 + (channels - 1) * 2;
    case 4:
        if (!s_supports_float)
            return 0;
        return AL_FORMAT_MONO_FLOAT32 + (channels - 1);
    default:
        return 0;
    }
}

static int AL_UploadSfx(sfx_t *s)
{
    ALsizei size = s_info.samples * s_info.width * s_info.channels;
    ALenum format = AL_GetSampleFormat(s_info.width, s_info.channels);
    ALuint buffer = 0;

    if (!format) {
        Com_SetLastError("Unsupported sample format");
        return Q_ERR(EINVAL);
    }

    qalGetError();
    qalGenBuffers(1, &buffer);
    if (qalGetError()) {
        Com_SetLastError("Failed to generate buffer");
        return Q_ERR_LIBRARY_ERROR;
    }

    qalBufferData(buffer, format, s_info.data, size, s_info.rate);
    if (qalGetError()) {
        Com_SetLastError("Failed to upload samples");
        qalDeleteBuffers(1, &buffer);
        return Q_ERR_LIBRARY_ERROR;
    }

    // specify OpenAL-Soft style loop points
    if (s_info.loopstart > 0 && s_loop_points) {
        ALint points[2] = { s_info.loopstart, s_info.samples };
        qalBufferiv(buffer, AL_LOOP_POINTS_SOFT, points);
    }

    // allocate placeholder sfxcache
    sfxcache_t *sc = s->cache = S_Malloc(sizeof(*sc));
    sc->length = s_info.samples * 1000LL / s_info.rate; // in msec
    sc->loopstart = s_info.loopstart;
    sc->width = s_info.width;
    sc->channels = s_info.channels;
    sc->size = size;
    sc->bufnum = buffer;

    return Q_ERR_SUCCESS;
}

static void AL_DeleteSfx(sfx_t *s)
{
    sfxcache_t *sc = s->cache;
    if (sc) {
        ALuint name = sc->bufnum;
        qalDeleteBuffers(1, &name);
    }
}

static void AL_Spatialize(channel_t *ch)
{
    // merged autosounds are handled differently
    if (ch->autosound == AUTOSOUND_MERGED)
        return;

    // anything coming from the view entity will always be full volume
    bool fullvolume = S_IsFullVolume(ch);

    // update fullvolume flag if needed
    if (ch->fullvolume != fullvolume) {
        if (s_source_spatialize) {
            qalSourcei(ch->srcnum, AL_SOURCE_SPATIALIZE_SOFT, !fullvolume);
        }
        qalSourcei(ch->srcnum, AL_SOURCE_RELATIVE, fullvolume);
        if (fullvolume) {
            qalSource3f(ch->srcnum, AL_POSITION, 0, 0, 0);
        } else if (ch->fixed_origin) {
            qalSource3f(ch->srcnum, AL_POSITION, AL_UnpackVector(ch->origin));
        }
        if (ch->fixed_origin || fullvolume) {
            qalSource3f(ch->srcnum, AL_VELOCITY, 0, 0, 0);
        }
        ch->fullvolume = fullvolume;
    }

    // update position if needed
    if (!ch->fixed_origin && !ch->fullvolume) {
        const sound_entity_t *ent = S_FindEntity(ch->entnum);
        if (ent) {
            qalSource3f(ch->srcnum, AL_POSITION, AL_UnpackVector(ent->origin));
            qalSource3f(ch->srcnum, AL_VELOCITY, AL_UnpackVector(ent->velocity));
        }
    }
}

static void AL_StopChannel(channel_t *ch)
{
    if (!ch->sfx)
        return;

#if USE_DEBUG
    if (s_show->integer > 1)
        Com_Printf("%s: %s\n", __func__, ch->sfx->name);
#endif

    // stop it
    qalSourceStop(ch->srcnum);
    qalSourcei(ch->srcnum, AL_BUFFER, AL_NONE);
    memset(ch, 0, sizeof(*ch));
}

static void AL_EnableReverb(channel_t *ch)
{
    if (!s_reverb_supported)
        return;

    if (s_reverb->integer) {
        qalSource3i(ch->srcnum, AL_AUXILIARY_SEND_FILTER, s_reverb_slots[0], 0, AL_FILTER_NULL);
        qalSource3i(ch->srcnum, AL_AUXILIARY_SEND_FILTER, s_reverb_slots[1], 1, AL_FILTER_NULL);
    } else {
        qalSource3i(ch->srcnum, AL_AUXILIARY_SEND_FILTER, AL_EFFECT_NULL, 0, AL_FILTER_NULL);
        qalSource3i(ch->srcnum, AL_AUXILIARY_SEND_FILTER, AL_EFFECT_NULL, 1, AL_FILTER_NULL);
    }
}

static void AL_PlayChannel(channel_t *ch)
{
    sfxcache_t *sc = ch->sfx->cache;

#if USE_DEBUG
    if (s_show->integer > 1)
        Com_Printf("%s: %s\n", __func__, ch->sfx->name);
#endif

    ch->srcnum = s_srcnums[ch - s_channels];
    qalGetError();
    qalSourcei(ch->srcnum, AL_BUFFER, sc->bufnum);
    qalSourcei(ch->srcnum, AL_LOOPING, ch->autosound || sc->loopstart >= 0);
    qalSourcef(ch->srcnum, AL_GAIN, ch->master_vol);
    qalSourcef(ch->srcnum, AL_REFERENCE_DISTANCE, SOUND_FULLVOLUME);
    qalSourcef(ch->srcnum, AL_MAX_DISTANCE, 8192);
    qalSourcef(ch->srcnum, AL_ROLLOFF_FACTOR, ch->dist_mult * (8192 - SOUND_FULLVOLUME));

    AL_EnableReverb(ch);

    // force update
    ch->fullvolume = -1;
    AL_Spatialize(ch);

    // play it
    qalSourcePlay(ch->srcnum);
    if (qalGetError() != AL_NO_ERROR) {
        AL_StopChannel(ch);
    }
}

static void AL_MergeLoopSounds(void)
{
    int         i, j;
    float       left, right, left_total, right_total;
    float       pan, pan2, gain;
    loopsound_t *loop, *other;
    channel_t   *ch;
    sfx_t       *sfx;
    sfxcache_t  *sc;
    sound_entity_t *ent;

    for (i = 0; i < s_numloopsounds; i++) {
        loop = &s_loopsounds[i];
        if (loop->framecount == s_framecount)
            continue;

        sfx = loop->sfx;
        sc = sfx->cache;
        if (!sc)
            continue;

        ent = S_FindEntity(loop->entnum);
        if (!ent)
            continue;

        // find the total contribution of all sounds of this type
        S_SpatializeOrigin(ent->origin, loop->volume, loop->dist_mult,
                           &left_total, &right_total, loop->stereo_pan);
        for (j = i + 1; j < s_numloopsounds; j++) {
            other = &s_loopsounds[j];
            if (other->sfx != loop->sfx)
                continue;
            // don't check this again later
            other->framecount = s_framecount;

            ent = S_FindEntity(other->entnum);
            if (!ent)
                continue;
            S_SpatializeOrigin(ent->origin, other->volume, other->dist_mult,
                               &left, &right, other->stereo_pan);
            left_total += left;
            right_total += right;
        }

        if (left_total == 0 && right_total == 0)
            continue;       // not audible

        left_total = min(1.0f, left_total);
        right_total = min(1.0f, right_total);

        gain = left_total + right_total;

        pan  = (right_total - left_total) / (left_total + right_total);
        pan2 = -sqrtf(1.0f - pan * pan);

        ch = S_FindAutoChannel(ENTITYNUM_NONE, sfx);
        if (ch) {
            qalSourcef(ch->srcnum, AL_GAIN, gain);
            qalSource3f(ch->srcnum, AL_POSITION, pan, 0.0f, pan2);
            ch->autoframe = s_framecount;
            ch->end = cls.realtime + sc->length;
            continue;
        }

        // allocate a channel
        ch = S_PickChannel(ENTITYNUM_NONE, 0);
        if (!ch)
            continue;

        ch->srcnum = s_srcnums[ch - s_channels];
        qalGetError();
        qalSourcei(ch->srcnum, AL_BUFFER, sc->bufnum);
        qalSourcei(ch->srcnum, AL_LOOPING, AL_TRUE);
        qalSourcei(ch->srcnum, AL_SOURCE_RELATIVE, AL_TRUE);
        if (s_source_spatialize) {
            qalSourcei(ch->srcnum, AL_SOURCE_SPATIALIZE_SOFT, AL_TRUE);
        }
        qalSourcef(ch->srcnum, AL_ROLLOFF_FACTOR, 0.0f);
        qalSourcef(ch->srcnum, AL_GAIN, gain);
        qalSource3f(ch->srcnum, AL_POSITION, pan, 0.0f, pan2);
        qalSource3f(ch->srcnum, AL_VELOCITY, 0, 0, 0);

        AL_EnableReverb(ch);

        ch->autosound = AUTOSOUND_MERGED;   // remove next frame
        ch->autoframe = s_framecount;
        ch->sfx = sfx;
        ch->entnum = loop->entnum;
        ch->master_vol = loop->volume;
        ch->dist_mult = loop->dist_mult;
        ch->end = cls.realtime + sc->length;

        // play it
        qalSourcePlay(ch->srcnum);
        if (qalGetError() != AL_NO_ERROR) {
            AL_StopChannel(ch);
        }
    }
}

static void AL_AddLoopSounds(void)
{
    int         i;
    loopsound_t *loop;
    channel_t   *ch, *ch2;
    sfx_t       *sfx;
    sfxcache_t  *sc;

    for (i = 0; i < s_numloopsounds; i++) {
        loop = &s_loopsounds[i];

        sfx = loop->sfx;
        sc = sfx->cache;
        if (!sc)
            continue;

        ch = S_FindAutoChannel(loop->entnum, sfx);
        if (ch) {
            ch->autoframe = s_framecount;
            ch->end = cls.realtime + sc->length;
            continue;
        }

        // allocate a channel
        ch = S_PickChannel(ENTITYNUM_NONE, 0);
        if (!ch)
            continue;

        // attempt to synchronize with existing sounds of the same type
        ch2 = S_FindAutoChannel(ENTITYNUM_NONE, sfx);
        if (ch2) {
            ALfloat offset = 0;
            qalGetSourcef(ch2->srcnum, AL_SAMPLE_OFFSET, &offset);
            qalSourcef(s_srcnums[ch - s_channels], AL_SAMPLE_OFFSET, offset);
        }

        ch->autosound = AUTOSOUND_DISCRETE; // remove next frame
        ch->autoframe = s_framecount;
        ch->sfx = sfx;
        ch->entnum = loop->entnum;
        ch->master_vol = loop->volume;
        ch->dist_mult = loop->dist_mult;
        ch->end = cls.realtime + sc->length;

        AL_PlayChannel(ch);
    }
}

#define MAX_STREAM_BUFFERS  32

static void AL_StreamUpdate(void)
{
    ALint num_buffers = 0;
    qalGetSourcei(s_stream, AL_BUFFERS_PROCESSED, &num_buffers);

    while (num_buffers > 0) {
        ALuint buffers[MAX_STREAM_BUFFERS];
        ALsizei n = min(num_buffers, q_countof(buffers));
        Q_assert(s_stream_buffers >= n);

        qalSourceUnqueueBuffers(s_stream, n, buffers);
        qalDeleteBuffers(n, buffers);

        s_stream_buffers -= n;
        num_buffers -= n;
    }
}

static void AL_StreamStop(void)
{
    qalSourceStop(s_stream);
    AL_StreamUpdate();
    Q_assert(!s_stream_buffers);
    s_stream_paused = false;
}

static void AL_StreamPause(bool paused)
{
    s_stream_paused = paused;

    // force pause if not active
    if (!s_active)
        paused = true;

    ALint state = 0;
    qalGetSourcei(s_stream, AL_SOURCE_STATE, &state);

    if (paused && state == AL_PLAYING)
        qalSourcePause(s_stream);

    if (!paused && state != AL_PLAYING && s_stream_buffers)
        qalSourcePlay(s_stream);
}

static bool AL_NeedRawSamples(void)
{
    return s_stream_buffers < MAX_STREAM_BUFFERS;
}

static bool AL_HaveRawSamples(void)
{
    return s_stream_buffers > 0;
}

static bool AL_RawSamples(int samples, int rate, int width, int channels, const void *data, float volume)
{
    ALenum format = AL_GetSampleFormat(width, channels);
    if (!format)
        return false;

    if (AL_NeedRawSamples()) {
        ALuint buffer = 0;

        qalGetError();
        qalGenBuffers(1, &buffer);
        if (qalGetError())
            return false;

        qalBufferData(buffer, format, data, samples * width * channels, rate);
        if (qalGetError()) {
            qalDeleteBuffers(1, &buffer);
            return false;
        }

        qalSourceQueueBuffers(s_stream, 1, &buffer);
        if (qalGetError()) {
            qalDeleteBuffers(1, &buffer);
            return false;
        }
        s_stream_buffers++;
    }

    qalSourcef(s_stream, AL_GAIN, volume);

    ALint state = AL_PLAYING;
    qalGetSourcei(s_stream, AL_SOURCE_STATE, &state);
    if (state != AL_PLAYING) {
        qalSourcePlay(s_stream);
        s_stream_paused = false;
    }
    return true;
}

static void AL_UpdateUnderWater(void)
{
    bool underwater = S_IsUnderWater();
    ALint filter = 0;

    if (!s_underwater_filter)
        return;

    if (s_underwater_flag == underwater)
        return;

    if (underwater)
        filter = s_underwater_filter;

    for (int i = 0; i < s_numchannels; i++)
        qalSourcei(s_srcnums[i], AL_DIRECT_FILTER, filter);

    s_underwater_flag = underwater;
}

static void AL_UpdateReverb(void)
{
    if (!s_reverb_supported || !s_reverb->integer)
        return;

    // blend two environments
    for (int i = 0; i < 2; i++) {
        reverb_preset_t preset = s_listener.reverb[i].preset;

        if (preset)
            qalAuxiliaryEffectSlotf(s_reverb_slots[i], AL_EFFECTSLOT_GAIN, s_listener.reverb[i].gain);

        if (s_reverb_presets[i] == preset)
            continue;

        if (preset > REVERB_PRESET_NONE && preset < REVERB_PRESET_MAX) {
            AL_LoadEffect(s_reverb_effects[i], &s_reverb_params[preset - 1]);
            qalAuxiliaryEffectSloti(s_reverb_slots[i], AL_EFFECTSLOT_EFFECT, s_reverb_effects[i]);
        } else {
            qalAuxiliaryEffectSloti(s_reverb_slots[i], AL_EFFECTSLOT_EFFECT, AL_EFFECT_NULL);
        }

        s_reverb_presets[i] = preset;
    }
}

static void AL_Activate(void)
{
    S_StopAllSounds();
    AL_StreamPause(s_stream_paused);
}

static void AL_Update(void)
{
    int         i;
    channel_t   *ch;
    ALfloat     orientation[6];

    // set listener parameters
    qalListener3f(AL_POSITION, AL_UnpackVector(s_listener.origin));
    qalListener3f(AL_VELOCITY, AL_UnpackVector(s_listener.velocity));
    AL_CopyVector(s_listener.v_forward, orientation);
    AL_CopyVector(s_listener.v_up, orientation + 3);
    qalListenerfv(AL_ORIENTATION, orientation);

    AL_UpdateUnderWater();

    AL_UpdateReverb();

    // update spatialization for dynamic sounds
    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (!ch->sfx)
            continue;

        if (ch->autosound) {
            // autosounds are regenerated fresh each frame
            if (ch->autoframe != s_framecount) {
                AL_StopChannel(ch);
                continue;
            }
        } else {
            ALenum state = AL_STOPPED;
            qalGetSourcei(ch->srcnum, AL_SOURCE_STATE, &state);
            if (state == AL_STOPPED) {
                AL_StopChannel(ch);
                continue;
            }
        }

#if USE_DEBUG
        if (s_show->integer) {
            ALfloat offset = 0;
            qalGetSourcef(ch->srcnum, AL_SAMPLE_OFFSET, &offset);
            Com_Printf("%d %.1f %.1f %s\n", i, ch->master_vol, offset, ch->sfx->name);
        }
#endif

        AL_Spatialize(ch);  // respatialize channel
    }

    s_framecount++;

    // add loopsounds
    if (s_merge_looping->integer >= s_merge_looping_minval) {
        AL_MergeLoopSounds();
    } else {
        AL_AddLoopSounds();
    }

    AL_StreamUpdate();
}

const sndapi_t snd_openal = {
    .init = AL_Init,
    .shutdown = AL_Shutdown,
    .update = AL_Update,
    .activate = AL_Activate,
    .sound_info = AL_SoundInfo,
    .upload_sfx = AL_UploadSfx,
    .delete_sfx = AL_DeleteSfx,
    .raw_samples = AL_RawSamples,
    .need_raw_samples = AL_NeedRawSamples,
    .have_raw_samples = AL_HaveRawSamples,
    .drop_raw_samples = AL_StreamStop,
    .pause_raw_samples = AL_StreamPause,
    .play_channel = AL_PlayChannel,
    .stop_channel = AL_StopChannel,
    .get_sample_rate = QAL_GetSampleRate,
};
