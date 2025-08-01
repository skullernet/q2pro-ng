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

// =======================================================================
// Internal sound data & structures
// =======================================================================

unsigned    s_registration_sequence;

channel_t   s_channels[MAX_CHANNELS];
int         s_numchannels;

loopsound_t  s_loopsounds[MAX_PACKET_ENTITIES];
int          s_numloopsounds;

hash_map_t  *s_entities;

sndstarted_t    s_started;
bool            s_active;
bool            s_supports_float;
const sndapi_t  *s_api;

vec3_t      listener_origin;
vec3_t      listener_forward;
vec3_t      listener_right;
vec3_t      listener_up;
int         listener_entnum;
bool        listener_underwater;

bool        s_registering;

int         s_paintedtime;  // sample PAIRS
unsigned    s_framecount;

// during registration it is possible to have more sounds
// than could actually be referenced during gameplay,
// because we don't want to free anything until we are
// sure we won't need it.
#define     MAX_SFX     (MAX_SOUNDS*2)
static sfx_t        known_sfx[MAX_SFX];
static int          num_sfx;

#define     MAX_PLAYSOUNDS  128
playsound_t s_playsounds[MAX_PLAYSOUNDS];
list_t      s_freeplays;
list_t      s_pendingplays;

cvar_t      *s_volume;
cvar_t      *s_ambient;
#if USE_DEBUG
cvar_t      *s_show;
#endif
cvar_t      *s_underwater;
cvar_t      *s_underwater_gain_hf;

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
    s_ambient = Cvar_Get("s_ambient", "1", 0);
#if USE_DEBUG
    s_show = Cvar_Get("s_show", "0", 0);
#endif
    s_auto_focus = Cvar_Get("s_auto_focus", "0", 0);
    s_underwater = Cvar_Get("s_underwater", "1", 0);
    s_underwater_gain_hf = Cvar_Get("s_underwater_gain_hf", "0.25", 0);

    // start one of available sound engines
    s_started = SS_NOT;

#if USE_OPENAL
    if (s_started == SS_NOT && s_enable->integer >= SS_OAL && snd_openal.init()) {
        s_started = SS_OAL;
        s_api = &snd_openal;
    }
#endif

#if USE_SNDDMA
    if (s_started == SS_NOT && s_enable->integer >= SS_DMA && snd_dma.init()) {
        s_started = SS_DMA;
        s_api = &snd_dma;
    }
#endif

    if (s_started == SS_NOT) {
        Com_EPrintf("Sound failed to initialize.\n");
        goto fail;
    }

    Cmd_Register(c_sound);

    // init playsound list
    // clear DMA buffer
    S_StopAllSounds();

    s_auto_focus->changed = s_auto_focus_changed;
    s_auto_focus_changed(s_auto_focus);

    num_sfx = 0;

    s_entities = HashMap_TagCreate(unsigned, vec3_t, HashInt32, NULL, TAG_SOUND);

    s_paintedtime = 0;

    s_registration_sequence = 1;

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

    s_auto_focus->changed = NULL;

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
sfx_t *S_SfxForHandle(qhandle_t hSfx)
{
    if (!hSfx) {
        return NULL;
    }

    Q_assert_soft(hSfx <= num_sfx);
    return &known_sfx[hSfx - 1];
}

static sfx_t *S_AllocSfx(void)
{
    sfx_t   *sfx;
    int     i;

    // find a free sfx
    for (i = 0, sfx = known_sfx; i < num_sfx; i++, sfx++) {
        if (!sfx->name[0])
            break;
    }

    if (i == num_sfx) {
        if (num_sfx == MAX_SFX)
            return NULL;
        num_sfx++;
    }

    return sfx;
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

    if (!s_registering) {
        S_LoadSound(sfx);
    }

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

    // load everything in
    for (i = 0, sfx = known_sfx; i < num_sfx; i++, sfx++) {
        if (!sfx->name[0])
            continue;
        S_LoadSound(sfx);
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
    int         life_left;
    channel_t   *ch;

// Check for replacement sound, or find the best one to replace
    first_to_die = -1;
    life_left = INT_MAX;
    for (ch_idx = 0; ch_idx < s_numchannels; ch_idx++) {
        ch = &s_channels[ch_idx];
        // channel 0 never overrides unless out of channels
        if (ch->entnum == entnum && ch->entchannel == entchannel && entchannel != 0) {
            if (entchannel > 255 && ch->sfx)
                return NULL;    // channels >255 only allow single sfx on that channel
            // always override sound from same entity
            first_to_die = ch_idx;
            break;
        }

        // don't let monster sounds override player sounds
        if (ch->entnum == listener_entnum && entnum != listener_entnum && ch->sfx)
            continue;

        if (ch->end - s_paintedtime < life_left) {
            life_left = ch->end - s_paintedtime;
            first_to_die = ch_idx;
        }
    }

    if (first_to_die == -1)
        return NULL;

    ch = &s_channels[first_to_die];
    if (s_api->stop_channel)
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
void S_IssuePlaysound(playsound_t *ps)
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

    sc = S_LoadSound(ps->sfx);
    if (!sc) {
        Com_Printf("S_IssuePlaysound: couldn't load %s\n", ps->sfx->name);
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
    ch->pos = 0;
    ch->end = s_paintedtime + sc->length;

    s_api->play_channel(ch);

    // free the playsound
    S_FreePlaysound(ps);
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
    sfxcache_t  *sc;
    playsound_t *ps, *sort;
    sfx_t       *sfx;

    if (!s_started)
        return;
    if (!s_active)
        return;
    if (!(sfx = S_SfxForHandle(hSfx)))
        return;

    // make sure the sound is loaded
    sc = S_LoadSound(sfx);
    if (!sc)
        return;     // couldn't load the sound's data

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
    ps->begin = s_api->get_begin_ofs(timeofs);

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
        S_StartSound(NULL, listener_entnum, 0, sfx, 1, ATTN_NONE, 0);
    }
}

void S_StartLocalSoundOnce(const char *sound)
{
    if (s_started) {
        qhandle_t sfx = S_RegisterSound(sound);
        S_StartSound(NULL, listener_entnum, 256, sfx, 1, ATTN_NONE, 0);
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

void S_UpdateEntity(unsigned entnum, const vec3_t origin)
{
    Q_assert_soft(entnum < ENTITYNUM_WORLD);
    if (s_entities)
        HashMap_InsertImpl(s_entities, sizeof(entnum), sizeof(vec3_t), &entnum, origin);
}

void S_UpdateListener(unsigned entnum, const vec3_t origin, const vec3_t axis[3], bool underwater)
{
    Q_assert_soft(entnum < ENTITYNUM_WORLD);
    listener_entnum = entnum;
    VectorCopy(origin, listener_origin);
    VectorCopy(axis[0], listener_forward);
    VectorNegate(axis[1], listener_right);
    VectorCopy(axis[2], listener_up);
    listener_underwater = underwater;
}

void S_GetEntityOrigin(unsigned entnum, vec3_t origin)
{
    if (entnum == listener_entnum) {
        VectorCopy(listener_origin, origin);
    } else {
        vec3_t *org = HashMap_Lookup(vec3_t, s_entities, &entnum);
        if (org)
            VectorCopy(*org, origin);
        else
            VectorClear(origin);
    }
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

    s_api->stop_all_sounds();

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
    VectorSubtract(origin, listener_origin, source_vec);

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
        dot = DotProduct(listener_right, source_vec);
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

    if (!s_started)
        return;

    // if the loading plaque is up, clear everything
    // out to make sure we aren't looping a dirty
    // dma buffer while loading
    if (cls.state == ca_loading) {
        // S_ClearBuffer should be already done in S_StopAllSounds
        return;
    }

    OGG_Update();

    s_api->update();
}
