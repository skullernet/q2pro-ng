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
// snd_main.c -- common sound functions

#include "sound.h"
#include "common/hash_map.h"

// =======================================================================
// Internal sound data & structures
// =======================================================================

unsigned    s_registration_sequence;
bool        s_registering;

channel_t   s_channels[MAX_CHANNELS];
int         s_numchannels;

loopsound_t  s_loopsounds[MAX_PACKET_ENTITIES];
int          s_numloopsounds;

static hash_map_t    *s_entities;
static sound_entity_t s_listener_ent;

sndstarted_t    s_started;
bool            s_active;
bool            s_supports_float;
const sndapi_t  *s_api;

listener_t  s_listener;
vec3_t      s_listener_right;

unsigned    s_framecount;

// during registration it is possible to have more sounds
// than could actually be referenced during gameplay,
// because we don't want to free anything until we are
// sure we won't need it.
#define MAX_SFX     (MAX_SOUNDS * 2)
static sfx_t        known_sfx[MAX_SFX];
static int          num_sfx;

#define MAX_PLAYSOUNDS  128
static playsound_t  s_playsounds[MAX_PLAYSOUNDS];
static list_t       s_freeplays;
static list_t       s_pendingplays;

cvar_t      *s_volume;
#if USE_DEBUG
cvar_t      *s_show;
#endif
cvar_t      *s_underwater;
cvar_t      *s_underwater_gain_hf;
cvar_t      *s_merge_looping;
cvar_t      *s_doppler_factor;
cvar_t      *s_speed_of_sound;
cvar_t      *s_reverb;

static cvar_t   *s_enable;
static cvar_t   *s_auto_focus;

// =======================================================================
// Console functions
// =======================================================================

static void S_SoundInfo_f(void)
{
    if (!s_started) {
        Com_Printf("Sound system not started.\n");
        return;
    }

    s_api->sound_info();
}

static void S_SoundList_f(void)
{
    int     i, count;
    sfx_t   *sfx;
    sfxcache_t  *sc;
    size_t  total;

    total = count = 0;
    for (sfx = known_sfx, i = 0; i < num_sfx; i++, sfx++) {
        if (!sfx->name[0])
            continue;
        sc = sfx->cache;
        if (sc) {
            total += sc->size;
            if (sc->loopstart >= 0)
                Com_Printf("L");
            else
                Com_Printf(" ");
            Com_Printf("(%2db) (%dch) %6i : %s\n", sc->width * 8, sc->channels, sc->size, sfx->name);
        } else {
            Com_Printf("  not loaded  : %s (%s)\n",
                       sfx->name, Q_ErrorString(sfx->error));
        }
        count++;
    }
    Com_Printf("Total sounds: %d (out of %d slots)\n", count, num_sfx);
    Com_Printf("Total resident: %zu\n", total);
}

static const cmdreg_t c_sound[] = {
    { "stopsound", S_StopAllSounds },
    { "soundlist", S_SoundList_f },
    { "soundinfo", S_SoundInfo_f },

    { NULL }
};

// =======================================================================
// Init sound engine
// =======================================================================

static void s_auto_focus_changed(cvar_t *self)
{
    S_Activate();
}

static void s_merge_looping_changed(cvar_t *self)
{
    int         i;
    channel_t   *ch;

    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (ch->autosound)
            s_api->stop_channel(ch);
    }
}

/*
================
S_Init
================
*/
void S_Init(void)
{
    s_enable = Cvar_Get("s_enable", "1", CVAR_SOUND);
    if (s_enable->integer <= SS_NOT) {
        Com_Printf("Sound initialization disabled.\n");
        return;
    }

    Com_Printf("------- S_Init -------\n");

    s_volume = Cvar_Get("s_volume", "0.7", CVAR_ARCHIVE);
#if USE_DEBUG
    s_show = Cvar_Get("s_show", "0", 0);
#endif
    s_auto_focus = Cvar_Get("s_auto_focus", "0", 0);
    s_underwater = Cvar_Get("s_underwater", "1", 0);
    s_underwater_gain_hf = Cvar_Get("s_underwater_gain_hf", "0.25", 0);
    s_merge_looping = Cvar_Get("s_merge_looping", "1", 0);
    s_doppler_factor = Cvar_Get("s_doppler_factor", "1", 0);
    s_speed_of_sound = Cvar_Get("s_speed_of_sound", "10000", 0);
    s_reverb = Cvar_Get("s_reverb", "1", 0);

    // start one of available sound engines
    s_started = SS_NOT;

#if USE_OPENAL
    if (s_started == SS_NOT && s_enable->integer >= SS_OPENAL && snd_openal.init()) {
        s_started = SS_OPENAL;
        s_api = &snd_openal;
    }
#endif

#if USE_MINIAUDIO
    if (s_started == SS_NOT && s_enable->integer >= SS_MINIAUDIO && snd_miniaudio.init()) {
        s_started = SS_MINIAUDIO;
        s_api = &snd_miniaudio;
    }
#endif

    if (s_started == SS_NOT) {
        Com_EPrintf("Sound failed to initialize.\n");
        goto fail;
    }

    Cmd_Register(c_sound);

    // init playsound list
    S_StopAllSounds();

    s_auto_focus->changed = s_auto_focus_changed;
    s_auto_focus_changed(s_auto_focus);

    s_merge_looping->changed = s_merge_looping_changed;

    num_sfx = 0;

    s_entities = HashMap_TagCreate(unsigned, sound_entity_t, HashInt32, NULL, TAG_SOUND);

    s_registration_sequence = 1;
    s_registering = false;

    // start the cd track
    OGG_Play();

fail:
    Cvar_SetInteger(s_enable, s_started, FROM_CODE);
    Com_Printf("----------------------\n");
}


// =======================================================================
// Shutdown sound engine
// =======================================================================

static void S_FreeSound(sfx_t *sfx)
{
    if (s_api->delete_sfx)
        s_api->delete_sfx(sfx);
    Z_Free(sfx->cache);
    memset(sfx, 0, sizeof(*sfx));
}

void S_FreeAllSounds(void)
{
    int     i;
    sfx_t   *sfx;

    // free all sounds
    for (i = 0, sfx = known_sfx; i < num_sfx; i++, sfx++) {
        if (!sfx->name[0])
            continue;
        S_FreeSound(sfx);
    }

    num_sfx = 0;
}

void S_Shutdown(void)
{
    if (!s_started)
        return;

    S_StopAllSounds();
    S_FreeAllSounds();
    OGG_Stop();

    if (s_api) {
        s_api->shutdown();
        s_api = NULL;
    }

    if (s_entities) {
        HashMap_Destroy(s_entities);
        s_entities = NULL;
    }

    s_started = SS_NOT;
    s_active = false;
    s_supports_float = false;
    s_listener = (listener_t){ 0 };

    s_auto_focus->changed = NULL;
    s_merge_looping->changed = NULL;

    Cmd_Deregister(c_sound);

    Z_LeakTest(TAG_SOUND);
}

void S_Activate(void)
{
    bool active;
    active_t level;

    if (!s_started)
        return;

    level = Cvar_ClampInteger(s_auto_focus, ACT_MINIMIZED, ACT_ACTIVATED);

    active = cls.active >= level;

    if (active == s_active)
        return;

    Com_DDDPrintf("%s: %d\n", __func__, active);
    s_active = active;

    s_api->activate();
}

// =======================================================================
// Load a sound
// =======================================================================

/*
==================
S_SfxForHandle
==================
*/
static sfx_t *S_SfxForHandle(qhandle_t h)
{
    Q_assert_soft(h <= num_sfx);
    if (!h)
        return NULL;

    sfx_t *sfx = &known_sfx[h - 1];
    if (!sfx->cache)
        return NULL;

    return sfx;
}

static sfx_t *S_AllocSfx(void)
{
    sfx_t   *sfx, *placeholder = NULL;
    int     i;

    // find a free sfx_t slot
    for (i = 0, sfx = known_sfx; i < num_sfx; i++, sfx++) {
        if (!sfx->name[0])
            return sfx;
        if (!sfx->cache && !placeholder)
            placeholder = sfx;
    }

    // allocate new slot if possible
    if (num_sfx < MAX_SFX) {
        num_sfx++;
        return sfx;
    }

    // reuse placeholder slot if available
    if (placeholder) {
        memset(placeholder, 0, sizeof(*placeholder));
        return placeholder;
    }

    return NULL;
}

/*
==================
S_FindName

==================
*/
static sfx_t *S_FindName(const char *name, size_t namelen)
{
    int     i;
    sfx_t   *sfx;

    // see if already loaded
    for (i = 0, sfx = known_sfx; i < num_sfx; i++, sfx++) {
        if (!FS_pathcmp(sfx->name, name)) {
            sfx->registration_sequence = s_registration_sequence;
            return sfx;
        }
    }

    // allocate new one
    sfx = S_AllocSfx();
    if (sfx) {
        memcpy(sfx->name, name, namelen + 1);
        sfx->registration_sequence = s_registration_sequence;
    }
    return sfx;
}

/*
=====================
S_BeginRegistration

=====================
*/
void S_BeginRegistration(void)
{
    s_registration_sequence++;
    s_registering = true;
}

/*
==================
S_RegisterSound

==================
*/
qhandle_t S_RegisterSound(const char *name)
{
    char    buffer[MAX_QPATH];
    sfx_t   *sfx;
    size_t  len;

    if (!s_started)
        return 0;

    Q_assert(name);

    // empty names are legal, silently ignore them
    if (!*name)
        return 0;

    if (*name == '#') {
        len = FS_NormalizePathBuffer(buffer, name + 1, MAX_QPATH);
    } else {
        len = Q_concat(buffer, MAX_QPATH, "sound/", name);
        if (len < MAX_QPATH)
            len = FS_NormalizePath(buffer);
    }

    // this MAY happen after prepending "sound/"
    if (len >= MAX_QPATH) {
        Com_DPrintf("%s: oversize name\n", __func__);
        return 0;
    }

    // normalized to empty name?
    if (len == 0) {
        Com_DPrintf("%s: empty name\n", __func__);
        return 0;
    }

    sfx = S_FindName(buffer, len);
    if (!sfx) {
        Com_DPrintf("%s: out of slots\n", __func__);
        return 0;
    }

    if (!S_LoadSound(sfx))
        return 0;

    return (sfx - known_sfx) + 1;
}

/*
=====================
S_EndRegistration

=====================
*/
void S_EndRegistration(void)
{
    int     i;
    sfx_t   *sfx;

    // clear playsound list, so we don't free sfx still present there
    S_StopAllSounds();

    // free any sounds not from this registration sequence
    for (i = 0, sfx = known_sfx; i < num_sfx; i++, sfx++) {
        if (!sfx->name[0])
            continue;
        if (sfx->registration_sequence != s_registration_sequence) {
            // don't need this sound
            S_FreeSound(sfx);
            continue;
        }
        // make sure it is paged in
        if (s_api->page_in_sfx)
            s_api->page_in_sfx(sfx);
    }

    s_registering = false;
}


//=============================================================================

/*
=================
S_PickChannel

picks a channel based on priorities, empty slots, number of channels
=================
*/
channel_t *S_PickChannel(int entnum, int entchannel)
{
    int         ch_idx;
    int         first_to_die;
    unsigned    life_left;
    channel_t   *ch;

// Check for replacement sound, or find the best one to replace
    first_to_die = -1;
    life_left = UINT_MAX;
    for (ch_idx = 0; ch_idx < s_numchannels; ch_idx++) {
        ch = &s_channels[ch_idx];
        if (!ch->sfx) {
            if (life_left) {
                first_to_die = ch_idx;
                life_left = 0;
            }
            continue;
        }

        // channel 0 never overrides unless out of channels
        if (entchannel && ch->entnum == entnum && ch->entchannel == entchannel) {
            if (entchannel > 255)
                return NULL;    // channels >255 only allow single sfx on that channel
            // always override sound from same entity
            first_to_die = ch_idx;
            break;
        }

        // don't let monster sounds override player sounds
        if (ch->entnum == s_listener.entnum && entnum != s_listener.entnum)
            continue;

        if (ch->end - cls.realtime < life_left) {
            life_left = ch->end - cls.realtime;
            first_to_die = ch_idx;
        }
    }

    if (first_to_die == -1)
        return NULL;

    ch = &s_channels[first_to_die];
    s_api->stop_channel(ch);
    memset(ch, 0, sizeof(*ch));

    return ch;
}

/*
=================
S_AllocPlaysound
=================
*/
static playsound_t *S_AllocPlaysound(void)
{
    playsound_t *ps = PS_FIRST(&s_freeplays);

    if (PS_TERM(ps, &s_freeplays))
        return NULL;        // no free playsounds

    // unlink from freelist
    List_Remove(&ps->entry);

    return ps;
}

/*
=================
S_FreePlaysound
=================
*/
static void S_FreePlaysound(playsound_t *ps)
{
    // unlink from channel
    List_Remove(&ps->entry);

    // add to free list
    List_Insert(&s_freeplays, &ps->entry);
}

/*
===============
S_IssuePlaysound

Take the next playsound and begin it on the channel
This is never called directly by S_Play*, but only
by the update loop.
===============
*/
static void S_IssuePlaysound(playsound_t *ps)
{
    channel_t   *ch;
    sfxcache_t  *sc;

#if USE_DEBUG
    if (s_show->integer)
        Com_Printf("Issue %i\n", ps->begin);
#endif
    // pick a channel to play on
    ch = S_PickChannel(ps->entnum, ps->entchannel);
    if (!ch) {
        S_FreePlaysound(ps);
        return;
    }

    sc = ps->sfx->cache;
    if (!sc) {
        // should never happen
        S_FreePlaysound(ps);
        return;
    }

    // spatialize
    if (ps->attenuation == ATTN_STATIC)
        ch->dist_mult = ps->attenuation * 0.001f;
    else
        ch->dist_mult = ps->attenuation * 0.0005f;
    ch->master_vol = ps->volume;
    ch->entnum = ps->entnum;
    ch->entchannel = ps->entchannel;
    ch->sfx = ps->sfx;
    VectorCopy(ps->origin, ch->origin);
    ch->fixed_origin = ps->fixed_origin;
    ch->end = cls.realtime + sc->length;

    s_api->play_channel(ch);

    // free the playsound
    S_FreePlaysound(ps);
}

static void S_IssuePlaysounds(void)
{
    // start any playsounds
    while (1) {
        playsound_t *ps = PS_FIRST(&s_pendingplays);
        if (PS_TERM(ps, &s_pendingplays))
            break;  // no more pending sounds
        if (ps->begin > cls.realtime)
            break;
        S_IssuePlaysound(ps);
    }
}

// =======================================================================
// Start a sound effect
// =======================================================================

/*
====================
S_StartSound

Validates the params and queues the sound up
if pos is NULL, the sound will be dynamically sourced from the entity
Entchannel 0 will never override a playing sound
====================
*/
void S_StartSound(const vec3_t origin, int entnum, int entchannel, qhandle_t hSfx, float vol, float attenuation, float timeofs)
{
    playsound_t *ps, *sort;
    sfx_t       *sfx;

    if (!s_started || !s_active)
        return;

    sfx = S_SfxForHandle(hSfx);
    if (!sfx)
        return;

    // make the playsound_t
    ps = S_AllocPlaysound();
    if (!ps)
        return;

    if (origin) {
        VectorCopy(origin, ps->origin);
        ps->fixed_origin = true;
    } else {
        ps->fixed_origin = false;
    }

    ps->entnum = entnum;
    ps->entchannel = entchannel;
    ps->attenuation = attenuation;
    ps->volume = vol;
    ps->sfx = sfx;
    ps->begin = cls.realtime + timeofs * 1000;

    // sort into the pending sound list
    LIST_FOR_EACH(sort, &s_pendingplays, entry)
        if (sort->begin >= ps->begin)
            break;

    List_Append(&sort->entry, &ps->entry);
}

/*
==================
S_StartLocalSound
==================
*/
void S_StartLocalSound(const char *sound)
{
    if (s_started) {
        qhandle_t sfx = S_RegisterSound(sound);
        S_StartSound(NULL, s_listener.entnum, 0, sfx, 1, ATTN_NONE, 0);
    }
}

void S_StartLocalSoundOnce(const char *sound)
{
    if (s_started) {
        qhandle_t sfx = S_RegisterSound(sound);
        S_StartSound(NULL, s_listener.entnum, 256, sfx, 1, ATTN_NONE, 0);
    }
}

void S_ClearLoopingSounds(void)
{
    s_numloopsounds = 0;
}

void S_AddLoopingSound(unsigned entnum, qhandle_t hSfx, float volume, float attenuation, bool stereo_pan)
{
    Q_assert_soft(entnum < ENTITYNUM_WORLD);

    if (!s_started || !s_active)
        return;

    sfx_t *sfx = S_SfxForHandle(hSfx);
    if (!sfx)
        return;

    if (s_numloopsounds >= q_countof(s_loopsounds))
        return;

    loopsound_t *loop = &s_loopsounds[s_numloopsounds++];
    loop->entnum = entnum;
    loop->sfx = sfx;
    loop->volume = volume;
    if (attenuation == ATTN_STATIC)
        loop->dist_mult = SOUND_LOOPATTENUATE;
    else
        loop->dist_mult = attenuation * SOUND_LOOPATTENUATE_MULT;
    loop->stereo_pan = stereo_pan;
}

void S_UpdateEntity(unsigned entnum, const vec3_t origin, const vec3_t velocity)
{
    Q_assert_soft(entnum < ENTITYNUM_WORLD);

    if (s_entities) {
        sound_entity_t ent = {
            .origin = VectorInit(origin),
            .velocity = VectorInit(velocity),
        };
        HashMap_Insert(s_entities, &entnum, &ent);
    }
}

void S_UpdateListener(const listener_t *params)
{
    s_listener = *params;
    CrossProduct(s_listener.v_forward, s_listener.v_up, s_listener_right);
    VectorCopy(s_listener.origin, s_listener_ent.origin);
    VectorCopy(s_listener.velocity, s_listener_ent.velocity);
}

sound_entity_t *S_FindEntity(unsigned entnum)
{
    if (entnum == s_listener.entnum)
        return &s_listener_ent;

    return HashMap_Lookup(sound_entity_t, s_entities, &entnum);
}

/*
==================
S_StopAllSounds
==================
*/
void S_StopAllSounds(void)
{
    int     i;

    if (!s_started)
        return;

    s_numloopsounds = 0;

    // clear all the playsounds
    memset(s_playsounds, 0, sizeof(s_playsounds));

    List_Init(&s_freeplays);
    List_Init(&s_pendingplays);

    for (i = 0; i < MAX_PLAYSOUNDS; i++)
        List_Append(&s_freeplays, &s_playsounds[i].entry);

    for (i = 0; i < s_numchannels; i++)
        s_api->stop_channel(&s_channels[i]);

    // clear all the channels
    memset(s_channels, 0, sizeof(s_channels));
}

void S_RawSamples(int samples, int rate, int width, int channels, const void *data)
{
    if (s_started && s_active)
        s_api->raw_samples(samples, rate, width, channels, data, 1.0f);
}

int S_GetSampleRate(void)
{
    if (s_api && s_api->get_sample_rate)
        return s_api->get_sample_rate();
    return 0;
}

bool S_SupportsFloat(void)
{
    return s_supports_float;
}

void S_PauseRawSamples(bool paused)
{
    if (s_api && s_api->pause_raw_samples)
        s_api->pause_raw_samples(paused);
}

// =======================================================================
// Update sound buffer
// =======================================================================

channel_t *S_FindAutoChannel(int entnum, const sfx_t *sfx)
{
    int         i;
    channel_t   *ch;

    for (i = 0, ch = s_channels; i < s_numchannels; i++, ch++) {
        if (!ch->autosound)
            continue;
        if (entnum != ENTITYNUM_NONE && ch->entnum != entnum)
            continue;
        if (ch->sfx != sfx)
            continue;
        return ch;
    }

    return NULL;
}

/*
=================
S_SpatializeOrigin

Used for spatializing channels and autosounds
=================
*/
void S_SpatializeOrigin(const vec3_t origin, float master_vol, float dist_mult, float *left_vol, float *right_vol, bool stereo)
{
    vec_t       dot;
    vec_t       dist;
    vec_t       lscale, rscale, scale;
    vec3_t      source_vec;

// calculate stereo separation and distance attenuation
    VectorSubtract(origin, s_listener.origin, source_vec);

    dist = VectorNormalize(source_vec);
    dist -= SOUND_FULLVOLUME;
    if (dist < 0)
        dist = 0;           // close enough to be at full volume
    dist *= dist_mult;      // different attenuation levels

    if (!stereo || !dist_mult) {
        // no attenuation = no spatialization
        rscale = 1.0f;
        lscale = 1.0f;
    } else {
        dot = DotProduct(s_listener_right, source_vec);
        rscale = 0.5f * (1.0f + dot);
        lscale = 0.5f * (1.0f - dot);
    }

    // add in distance effect
    scale = (1.0f - dist) * rscale;
    *right_vol = master_vol * scale;
    if (*right_vol < 0)
        *right_vol = 0;

    scale = (1.0f - dist) * lscale;
    *left_vol = master_vol * scale;
    if (*left_vol < 0)
        *left_vol = 0;
}

/*
============
S_Update

Called once each time through the main loop
============
*/
void S_Update(void)
{
    if (cvar_modified & CVAR_SOUND) {
        Cbuf_AddText(&cmd_buffer, "snd_restart\n");
        cvar_modified &= ~CVAR_SOUND;
        return;
    }

    if (!s_started || !s_active)
        return;

    OGG_Update();

    S_IssuePlaysounds();

    s_api->update();
}
