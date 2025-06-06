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
// cl_tent.c -- client side temporary entities

#include "client.h"
#include "common/mdfour.h"

qhandle_t   cl_sfx_ric1;
qhandle_t   cl_sfx_ric2;
qhandle_t   cl_sfx_ric3;
qhandle_t   cl_sfx_lashit;
qhandle_t   cl_sfx_spark5;
qhandle_t   cl_sfx_spark6;
qhandle_t   cl_sfx_spark7;
qhandle_t   cl_sfx_railg;
qhandle_t   cl_sfx_rockexp;
qhandle_t   cl_sfx_grenexp;
qhandle_t   cl_sfx_watrexp;

qhandle_t   cl_sfx_lightning;
qhandle_t   cl_sfx_disrexp;

qhandle_t   cl_sfx_hit_marker;

qhandle_t   cl_mod_explode;
qhandle_t   cl_mod_smoke;
qhandle_t   cl_mod_flash;
qhandle_t   cl_mod_parasite_segment;
qhandle_t   cl_mod_grapple_cable;
qhandle_t   cl_mod_explo4;
qhandle_t   cl_mod_bfg_explo;
qhandle_t   cl_mod_powerscreen;
qhandle_t   cl_mod_laser;
qhandle_t   cl_mod_dmspot;

qhandle_t   cl_mod_lightning;
qhandle_t   cl_mod_heatbeam;

qhandle_t   cl_mod_muzzles[MFLASH_TOTAL];

qhandle_t   cl_img_flare;

static cvar_t   *cl_muzzleflashes;
cvar_t   *cl_hit_markers;

#define MAX_FOOTSTEP_IDS    256
#define MAX_FOOTSTEP_SFX    15

typedef struct {
    int         num_sfx;
    qhandle_t   sfx[MAX_FOOTSTEP_SFX];
} cl_footstep_sfx_t;

static cl_footstep_sfx_t    cl_footstep_sfx[MAX_FOOTSTEP_IDS];
static int                  cl_num_footsteps;
static qhandle_t            cl_last_footstep;

/*
=================
CG_FindFootstepSurface
=================
*/
static int CG_FindFootstepSurface(int entnum)
{
    const centity_t *cent = &cl_entities[entnum];

    // skip if no materials loaded
    if (cl_num_footsteps <= FOOTSTEP_RESERVED_COUNT)
        return FOOTSTEP_ID_DEFAULT;

    // allow custom footsteps to be disabled
    if (cl_footsteps->integer >= 2)
        return FOOTSTEP_ID_DEFAULT;

    // use an X/Y only mins/maxs copy of the entity,
    // since we don't want it to get caught inside of any geometry above or below
    const vec3_t trace_mins = { cent->mins[0], cent->mins[1], 0 };
    const vec3_t trace_maxs = { cent->maxs[0], cent->maxs[1], 0 };

    // trace start position is the entity's current origin + { 0 0 1 },
    // so that entities with their mins at 0 won't get caught in the floor
    vec3_t trace_start;
    VectorCopy(cent->current.origin, trace_start);
    trace_start[2] += 1;

    // the end of the trace starts down by half of STEPSIZE
    vec3_t trace_end;
    VectorCopy(trace_start, trace_end);
    trace_end[2] -= 9;
    if (cent->current.solid && cent->current.solid != PACKED_BSP) {
        // if the entity is a bbox'd entity, the mins.z is added to the end point as well
        trace_end[2] += cent->mins[2];
    } else {
        // otherwise use a value that should cover every monster in the game
        trace_end[2] -= 66; // should you wonder: monster_guardian is the biggest boi
    }

    // first, a trace done solely against MASK_SOLID
    trace_t tr;
    CG_Trace(&tr, trace_start, trace_mins, trace_maxs, trace_end, ENTITYNUM_NONE, MASK_SOLID);

    if (tr.fraction == 1.0f) {
        // if we didn't hit anything, use default step ID
        return FOOTSTEP_ID_DEFAULT;
    }

    if (tr.surface_id) {
        // copy over the surfaces' step ID
        surface_info_t surf;
        trap_GetSurfaceInfo(tr.surface_id, &surf);

        // do another trace that ends instead at endpos + { 0 0 1 }, and is against MASK_SOLID | MASK_WATER
        vec3_t new_end;
        VectorCopy(tr.endpos, new_end);
        new_end[2] += 1;

        CG_Trace(&tr, trace_start, trace_mins, trace_maxs, new_end, ENTITYNUM_NONE, MASK_SOLID | MASK_WATER);
        // if we hit something else, use that new footstep id instead of the first traces' value
        if (tr.surface_id)
            trap_GetSurfaceInfo(tr.surface_id, &surf);

        return surf.footstep_id;
    }

    return FOOTSTEP_ID_DEFAULT;
}

/*
=================
CG_PlayFootstepSfx
=================
*/
static void CG_PlayFootstepSfx(unsigned step_id, int entnum, float volume, float attenuation)
{
    const cl_footstep_sfx_t *sfx;
    qhandle_t footstep_sfx;
    int sfx_num;

    if (!cl_num_footsteps)
        return; // should not really happen

    if (step_id == FOOTSTEP_ID_DEFAULT)
        step_id = CG_FindFootstepSurface(entnum);
    if (step_id >= MAX_FOOTSTEP_IDS)
        step_id = FOOTSTEP_ID_DEFAULT;

    sfx = &cl_footstep_sfx[step_id];
    if (sfx->num_sfx <= 0)
        sfx = &cl_footstep_sfx[0];
    if (sfx->num_sfx <= 0)
        return; // no footsteps, not even fallbacks

    // pick a random footstep sound, but avoid playing the same one twice in a row
    sfx_num = Q_rand_uniform(sfx->num_sfx);
    footstep_sfx = sfx->sfx[sfx_num];
    if (footstep_sfx == cl_last_footstep)
        footstep_sfx = sfx->sfx[(sfx_num + 1) % sfx->num_sfx];

    S_StartSound(NULL, entnum, CHAN_FOOTSTEP, footstep_sfx, volume, attenuation, 0);
    cl_last_footstep = footstep_sfx;
}

/*
=================
CG_RegisterFootstep
=================
*/
static void CG_RegisterFootstep(cl_footstep_sfx_t *sfx, const char *material)
{
    char name[MAX_QPATH];
    size_t len;
    int i;

    Q_assert(!material || *material);

    for (i = 0; i < MAX_FOOTSTEP_SFX; i++) {
        if (material)
            len = Q_snprintf(name, sizeof(name), "#sound/player/steps/%s%i.wav", material, i + 1);
        else
            len = Q_snprintf(name, sizeof(name), "#sound/player/step%i.wav", i + 1);
        Q_assert(len < sizeof(name));
        if (FS_LoadFile(name + 1, NULL) < 0)
            break;
        sfx->sfx[i] = S_RegisterSound(name);
    }

    sfx->num_sfx = i;
}

/*
=================
CG_RegisterFootsteps
=================
*/
static void CG_RegisterFootsteps(void)
{
    surface_info_t surf;
    int i;

    cl_last_footstep = 0;

    for (i = 0; i < MAX_FOOTSTEP_IDS; i++)
        cl_footstep_sfx[i].num_sfx = -1;

    // load reserved footsteps
    CG_RegisterFootstep(&cl_footstep_sfx[FOOTSTEP_ID_DEFAULT], NULL);
    CG_RegisterFootstep(&cl_footstep_sfx[FOOTSTEP_ID_LADDER], "ladder");

    // load the rest
    for (i = 1; trap_GetSurfaceInfo(i, &surf) && surf.footstep_id < MAX_FOOTSTEP_IDS; i++) {
        cl_footstep_sfx_t *sfx = &cl_footstep_sfx[surf.footstep_id];
        if (sfx->num_sfx == -1)
            CG_RegisterFootstep(sfx, surf.material);
    }
}

/*
=================
CG_RegisterTEntSounds
=================
*/
void CG_RegisterTEntSounds(void)
{
    cl_sfx_ric1 = S_RegisterSound("world/ric1.wav");
    cl_sfx_ric2 = S_RegisterSound("world/ric2.wav");
    cl_sfx_ric3 = S_RegisterSound("world/ric3.wav");
    cl_sfx_lashit = S_RegisterSound("weapons/lashit.wav");
    cl_sfx_spark5 = S_RegisterSound("world/spark5.wav");
    cl_sfx_spark6 = S_RegisterSound("world/spark6.wav");
    cl_sfx_spark7 = S_RegisterSound("world/spark7.wav");
    cl_sfx_railg = S_RegisterSound("weapons/railgf1a.wav");
    cl_sfx_rockexp = S_RegisterSound("weapons/rocklx1a.wav");
    cl_sfx_grenexp = S_RegisterSound("weapons/grenlx1a.wav");
    cl_sfx_watrexp = S_RegisterSound("weapons/xpld_wat.wav");

    S_RegisterSound("player/land1.wav");
    S_RegisterSound("player/fall2.wav");
    S_RegisterSound("player/fall1.wav");

    CG_RegisterFootsteps();

    cl_sfx_lightning = S_RegisterSound("weapons/tesla.wav");
    cl_sfx_disrexp = S_RegisterSound("weapons/disrupthit.wav");

    cl_sfx_hit_marker = S_RegisterSound("weapons/marker.wav");
}

static const char *const muzzlenames[MFLASH_TOTAL] = {
    [MFLASH_MACHN]     = "v_machn",
    [MFLASH_SHOTG2]    = "v_shotg2",
    [MFLASH_SHOTG]     = "v_shotg",
    [MFLASH_ROCKET]    = "v_rocket",
    [MFLASH_RAIL]      = "v_rail",
    [MFLASH_LAUNCH]    = "v_launch",
    [MFLASH_ETF_RIFLE] = "v_etf_rifle",
    [MFLASH_DIST]      = "v_dist",
    [MFLASH_BOOMER]    = "v_boomer",
    [MFLASH_BLAST]     = "v_blast",
    [MFLASH_BFG]       = "v_bfg",
    [MFLASH_BEAMER]    = "v_beamer",
};

/*
=================
CG_RegisterTEntModels
=================
*/
void CG_RegisterTEntModels(void)
{
    void *data;
    int len;

    cl_mod_explode = R_RegisterModel("models/objects/explode/tris.md2");
    cl_mod_smoke = R_RegisterModel("models/objects/smoke/tris.md2");
    cl_mod_flash = R_RegisterModel("models/objects/flash/tris.md2");
    cl_mod_parasite_segment = R_RegisterModel("models/monsters/parasite/segment/tris.md2");
    cl_mod_grapple_cable = R_RegisterModel("models/ctf/segment/tris.md2");
    cl_mod_explo4 = R_RegisterModel("models/objects/r_explode/tris.md2");
    cl_mod_bfg_explo = R_RegisterModel("sprites/s_bfg2.sp2");
    cl_mod_powerscreen = R_RegisterModel("models/items/armor/effect/tris.md2");
    cl_mod_laser = R_RegisterModel("models/objects/laser/tris.md2");
    cl_mod_dmspot = R_RegisterModel("models/objects/dmspot/tris.md2");

    cl_mod_lightning = R_RegisterModel("models/proj/lightning/tris.md2");
    cl_mod_heatbeam = R_RegisterModel("models/proj/beam/tris.md2");

    for (int i = 0; i < MFLASH_TOTAL; i++)
        cl_mod_muzzles[i] = R_RegisterModel(va("models/weapons/%s/flash/tris.md2", muzzlenames[i]));

    cl_img_flare = R_RegisterImage("misc/flare.tga", IT_SPRITE, IF_DEFAULT_FLARE);

    // check for remaster powerscreen model (ugly!)
    len = FS_LoadFile("models/items/armor/effect/tris.md2", &data);
    cl.need_powerscreen_scale = len == 2300 && Com_BlockChecksum(data, len) == 0x19fca65b;
    FS_FreeFile(data);
}

/*
==============================================================

EXPLOSION MANAGEMENT

==============================================================
*/

#define MAX_EXPLOSIONS  256

typedef struct {
    enum {
        ex_free,
        ex_misc,
        ex_flash,
        ex_mflash,
        ex_poly,
        ex_light
    } type;

    entity_t    ent;
    int         frames;
    float       light;
    vec3_t      lightcolor;
    float       start;
    int         baseframe;
} explosion_t;

static explosion_t  cl_explosions[MAX_EXPLOSIONS];

static void CG_ClearExplosions(void)
{
    memset(cl_explosions, 0, sizeof(cl_explosions));
}

static explosion_t *CG_AllocExplosion(void)
{
    explosion_t *e, *oldest;
    int     i;
    int     time;

    for (i = 0, e = cl_explosions; i < MAX_EXPLOSIONS; i++, e++) {
        if (e->type == ex_free) {
            memset(e, 0, sizeof(*e));
            return e;
        }
    }
// find the oldest explosion
    time = cl.time;
    oldest = cl_explosions;

    for (i = 0, e = cl_explosions; i < MAX_EXPLOSIONS; i++, e++) {
        if (e->start < time) {
            time = e->start;
            oldest = e;
        }
    }
    memset(oldest, 0, sizeof(*oldest));
    return oldest;
}

static explosion_t *CG_PlainExplosion(const vec3_t pos)
{
    explosion_t *ex;

    ex = CG_AllocExplosion();
    VectorCopy(pos, ex->ent.origin);
    ex->type = ex_poly;
    ex->ent.flags = RF_FULLBRIGHT;
    ex->start = cl.servertime - CG_FRAMETIME;
    ex->light = 350;
    VectorSet(ex->lightcolor, 1.0f, 0.5f, 0.5f);
    ex->ent.angles[1] = Q_rand() % 360;
    ex->ent.model = cl_mod_explo4;
    ex->baseframe = 15 * (Q_rand() & 1);
    ex->frames = 15;

    return ex;
}

static void CG_BFGExplosion(const vec3_t pos)
{
    explosion_t *ex;

    ex = CG_AllocExplosion();
    VectorCopy(pos, ex->ent.origin);
    ex->type = ex_poly;
    ex->ent.flags = RF_FULLBRIGHT;
    ex->start = cl.servertime - CG_FRAMETIME;
    ex->light = 350;
    VectorSet(ex->lightcolor, 0.0f, 1.0f, 0.0f);
    ex->ent.model = cl_mod_bfg_explo;
    ex->ent.flags |= RF_TRANSLUCENT;
    ex->ent.alpha = 0.30f;
    ex->frames = 4;
}

void CG_AddWeaponMuzzleFX(cl_muzzlefx_t fx, const vec3_t offset, float scale)
{
    if (!cl_muzzleflashes->integer)
        return;

    Q_assert(fx < q_countof(cl_mod_muzzles));

    if (!cl_mod_muzzles[fx])
        return;

    cl.weapon.muzzle.model = cl_mod_muzzles[fx];
    cl.weapon.muzzle.scale = scale;
    if (fx == MFLASH_MACHN || fx == MFLASH_BEAMER)
        cl.weapon.muzzle.roll = Q_rand() % 360;
    else
        cl.weapon.muzzle.roll = 0;
    VectorCopy(offset, cl.weapon.muzzle.offset);
    cl.weapon.muzzle.time = cl.servertime - CG_FRAMETIME;
}

void CG_AddMuzzleFX(const vec3_t origin, const vec3_t angles, cl_muzzlefx_t fx, int skin, float scale)
{
    explosion_t *ex;

    if (!cl_muzzleflashes->integer)
        return;

    Q_assert(fx < q_countof(cl_mod_muzzles));

    if (!cl_mod_muzzles[fx])
        return;

    ex = CG_AllocExplosion();
    VectorCopy(origin, ex->ent.origin);
    VectorCopy(angles, ex->ent.angles);
    ex->type = ex_mflash;
    ex->ent.flags = RF_TRANSLUCENT | RF_NOSHADOW | RF_FULLBRIGHT;
    ex->ent.alpha = 1.0f;
    ex->start = cl.servertime - CG_FRAMETIME;
    ex->ent.model = cl_mod_muzzles[fx];
    ex->ent.skinnum = skin;
    ex->ent.scale = scale;
    if (fx != MFLASH_BOOMER)
        ex->ent.angles[2] = Q_rand() % 360;
}

/*
=================
CG_SmokeAndFlash
=================
*/
void CG_SmokeAndFlash(const vec3_t origin)
{
    explosion_t *ex;

    ex = CG_AllocExplosion();
    VectorCopy(origin, ex->ent.origin);
    ex->type = ex_misc;
    ex->frames = 4;
    ex->ent.flags = RF_TRANSLUCENT | RF_NOSHADOW;
    ex->start = cl.servertime - CG_FRAMETIME;
    ex->ent.model = cl_mod_smoke;

    ex = CG_AllocExplosion();
    VectorCopy(origin, ex->ent.origin);
    ex->type = ex_flash;
    ex->ent.flags = RF_FULLBRIGHT;
    ex->frames = 2;
    ex->start = cl.servertime - CG_FRAMETIME;
    ex->ent.model = cl_mod_flash;
}

static void CG_AddExplosions(void)
{
    entity_t    *ent;
    int         i;
    explosion_t *ex;
    float       frac;
    int         f;

    for (i = 0, ex = cl_explosions; i < MAX_EXPLOSIONS; i++, ex++) {
        if (ex->type == ex_free)
            continue;

        if (ex->type == ex_mflash) {
            if (cl.time - ex->start > 50)
                ex->type = ex_free;
            else
                trap_R_AddEntity(&ex->ent);
            continue;
        }

        frac = (cl.time - ex->start) * BASE_1_FRAMETIME;
        f = floorf(frac);

        ent = &ex->ent;

        switch (ex->type) {
        case ex_misc:
        case ex_light:
            if (f >= ex->frames - 1) {
                ex->type = ex_free;
                break;
            }
            ent->alpha = 1.0f - frac / (ex->frames - 1);
            break;
        case ex_flash:
            if (f >= 1) {
                ex->type = ex_free;
                break;
            }
            ent->alpha = 1.0f;
            break;
        case ex_poly:
            if (f >= ex->frames - 1) {
                ex->type = ex_free;
                break;
            }

            if (cl_smooth_explosions->integer) {
                ent->alpha = 1.0f - frac / (ex->frames - 1);
                ent->flags |= RF_TRANSLUCENT;
            } else {
                ent->alpha = (16.0f - (float)f) / 16.0f;
                ent->alpha = max(ent->alpha, 0.0f);
            }

            if (f < 10) {
                ent->skinnum = (f >> 1);
                if (ent->skinnum < 0)
                    ent->skinnum = 0;
            } else {
                ent->flags |= RF_TRANSLUCENT;
                if (f < 13)
                    ent->skinnum = 5;
                else
                    ent->skinnum = 6;
            }
            break;
        default:
            Q_assert(!"bad type");
        }

        if (ex->type == ex_free)
            continue;

        if (ex->light)
            trap_R_AddLight(ent->origin, ex->light * ent->alpha,
                            ex->lightcolor[0], ex->lightcolor[1], ex->lightcolor[2]);

        if (ex->type != ex_light) {
            VectorCopy(ent->origin, ent->oldorigin);

            if (f < 0)
                f = 0;
            ent->frame = ex->baseframe + f + 1;
            ent->oldframe = ex->baseframe + f;
            ent->backlerp = 1.0f - (frac - f);

            trap_R_AddEntity(ent);
        }
    }
}

/*
==============================================================

LASER MANAGEMENT

==============================================================
*/

#define MAX_LASERS  256

typedef struct {
    vec3_t      start;
    vec3_t      end;
    int         color;
    color_t     rgba;
    int         width;
    int         lifetime, starttime;
} laser_t;

static laser_t  cl_lasers[MAX_LASERS];

static void CG_ClearLasers(void)
{
    memset(cl_lasers, 0, sizeof(cl_lasers));
}

static laser_t *CG_AllocLaser(void)
{
    laser_t *l;
    int i;

    for (i = 0, l = cl_lasers; i < MAX_LASERS; i++, l++) {
        if (cl.time - l->starttime >= l->lifetime) {
            memset(l, 0, sizeof(*l));
            l->starttime = cl.time;
            return l;
        }
    }

    return NULL;
}

static void CG_AddLasers(void)
{
    laser_t     *l;
    entity_t    ent;
    int         i;
    int         time;

    memset(&ent, 0, sizeof(ent));

    for (i = 0, l = cl_lasers; i < MAX_LASERS; i++, l++) {
        time = l->lifetime - (cl.time - l->starttime);
        if (time <= 0) {
            continue;
        }

        if (l->color == -1) {
            ent.rgba = l->rgba;
            ent.alpha = (float)time / (float)l->lifetime;
        } else {
            ent.alpha = 0.30f;
        }

        ent.skinnum = l->color;
        ent.flags = RF_TRANSLUCENT | RF_BEAM;
        VectorCopy(l->start, ent.origin);
        VectorCopy(l->end, ent.oldorigin);
        ent.frame = l->width;

        trap_R_AddEntity(&ent);
    }
}

static void CG_ParseLaser(const vec3_t start, const vec3_t end, uint32_t colors)
{
    laser_t *l;

    l = CG_AllocLaser();
    if (!l)
        return;

    VectorCopy(start, l->start);
    VectorCopy(end, l->end);
    l->lifetime = 100;
    l->color = (colors >> ((Q_rand() % 4) * 8)) & 0xff;
    l->width = 4;
}

/*
==============================================================

SUSTAIN MANAGEMENT

==============================================================
*/

#define MAX_SUSTAINS    32

static cl_sustain_t     cl_sustains[MAX_SUSTAINS];

static void CG_ClearSustains(void)
{
    memset(cl_sustains, 0, sizeof(cl_sustains));
}

static cl_sustain_t *CG_AllocSustain(void)
{
    cl_sustain_t    *s;
    int             i;

    for (i = 0, s = cl_sustains; i < MAX_SUSTAINS; i++, s++) {
        if (s->id == 0)
            return s;
    }

    return NULL;
}

static void CG_ProcessSustain(void)
{
    cl_sustain_t    *s;
    int             i;

    for (i = 0, s = cl_sustains; i < MAX_SUSTAINS; i++, s++) {
        if (s->think) {
            if ((s->endtime >= cl.time) && (cl.time >= s->nextthink))
                s->think(s);
            else if (s->endtime < cl.time)
                s->think = NULL;
        }
    }
}

static void CG_ParseWidow(const vec3_t pos)
{
    cl_sustain_t    *s;

    s = CG_AllocSustain();
    if (!s)
        return;

    VectorCopy(pos, s->org);
    s->endtime = cl.time + 2100;
    s->think = CG_Widowbeamout;
    s->nextthink = cl.time;
}

static void CG_ParseNuke(const vec3_t pos)
{
    explosion_t     *ex;
    cl_sustain_t    *s;

    S_StartSound(pos, ENTITYNUM_WORLD, CHAN_VOICE, cl_sfx_grenexp, 1, ATTN_NONE, 0);
    S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_rockexp, 1, ATTN_NORM, 0);

    ex = CG_PlainExplosion(pos);
    ex->ent.model = cl_mod_explo4;
    ex->ent.scale = 2.0f;

    s = CG_AllocSustain();
    if (!s)
        return;

    VectorCopy(pos, s->org);
    s->endtime = cl.time + 1000;
    s->think = CG_Nukeblast;
    s->nextthink = cl.time;
}

//==============================================================

static color_t  railcore_color;
static color_t  railspiral_color;

static cvar_t *cl_railtrail_type;
static cvar_t *cl_railtrail_time;
static cvar_t *cl_railcore_color;
static cvar_t *cl_railcore_width;
static cvar_t *cl_railspiral_color;
static cvar_t *cl_railspiral_radius;

static void cl_railcore_color_changed(cvar_t *self)
{
    if (!SCR_ParseColor(self->string, &railcore_color)) {
        Com_WPrintf("Invalid value '%s' for '%s'\n", self->string, self->name);
        Cvar_Reset(self);
        railcore_color.u32 = U32_RED;
    }
}

static void cl_railspiral_color_changed(cvar_t *self)
{
    if (!SCR_ParseColor(self->string, &railspiral_color)) {
        Com_WPrintf("Invalid value '%s' for '%s'\n", self->string, self->name);
        Cvar_Reset(self);
        railspiral_color.u32 = U32_BLUE;
    }
}

static void CG_RailCore(const vec3_t start, const vec3_t end)
{
    laser_t *l;

    l = CG_AllocLaser();
    if (!l)
        return;

    VectorCopy(start, l->start);
    VectorCopy(end, l->end);
    l->color = -1;
    l->lifetime = cl_railtrail_time->integer;
    l->width = cl_railcore_width->integer;
    l->rgba = railcore_color;
}

static void CG_RailSpiral(const vec3_t start, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
    vec3_t      right, up;
    int         i;
    float       d, c, s;
    vec3_t      dir;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    MakeNormalVectors(vec, right, up);

    for (i = 0; i < len; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cl.time;
        VectorClear(p->accel);

        d = i * 0.1f;
        c = cosf(d);
        s = sinf(d);

        VectorScale(right, c, dir);
        VectorMA(dir, s, up, dir);

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (cl_railtrail_time->value + frand() * 0.2f);
        p->color = -1;
        p->rgba = railspiral_color;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + dir[j] * cl_railspiral_radius->value;
            p->vel[j] = dir[j] * 6;
        }

        VectorAdd(move, vec, move);
    }
}

static void CG_RailTrail(const vec3_t start, const vec3_t end, entity_event_t event)
{
    if (!cl_railtrail_type->integer && event != EV_RAILTRAIL2) {
        CG_OldRailTrail(start, end);
    } else {
        if (cl_railcore_width->integer > 0) {
            CG_RailCore(start, end);
        }
        if (cl_railtrail_type->integer > 1) {
            CG_RailSpiral(start, end);
        }
    }
}

static void dirtoangles(const vec3_t dir, vec3_t angles)
{
    angles[0] = RAD2DEG(acosf(dir[2]));
    if (dir[0])
        angles[1] = RAD2DEG(atan2f(dir[1], dir[0]));
    else if (dir[1] > 0)
        angles[1] = 90;
    else if (dir[1] < 0)
        angles[1] = 270;
    else
        angles[1] = 0;
}

static void CG_BerserkSlam(centity_t *cent, entity_event_t event)
{
    vec3_t  forward, right, ofs, dir, origin;
    float   scale;

    AngleVectors(cent->current.angles, forward, right, NULL);

    if (event == EV_BERSERK_SLAM) {
        S_StartSound(NULL, cent->current.number, CHAN_WEAPON, S_RegisterSound("mutant/thud1.wav"), 1, ATTN_NORM, 0);
        S_StartSound(NULL, cent->current.number, CHAN_AUTO, S_RegisterSound("world/explod2.wav"), 0.75f, ATTN_NORM, 0);
        VectorSet(ofs, 20.0f, -14.3f, -21.0f);
        VectorSet(dir, 0, 0, 1);
    } else {
        VectorSet(ofs, 20, 0, 14);
        VectorCopy(forward, dir);
    }

    scale = cent->current.scale;
    if (!scale)
        scale = 1.0f;

    VectorScale(ofs, scale, ofs);
    origin[0] = cent->current.origin[0] + forward[0] * ofs[0] + right[0] * ofs[1];
    origin[1] = cent->current.origin[1] + forward[1] * ofs[0] + right[1] * ofs[1];
    origin[2] = cent->current.origin[2] + forward[2] * ofs[0] + right[2] * ofs[1] + ofs[2];

    trace_t tr;
    CG_Trace(&tr, cent->current.origin, NULL, NULL, origin, cent->current.number, MASK_SOLID);

    CG_BerserkSlamParticles(tr.endpos, dir);

    explosion_t *ex = CG_AllocExplosion();
    VectorCopy(tr.endpos, ex->ent.origin);
    dirtoangles(dir, ex->ent.angles);
    ex->type = ex_misc;
    ex->ent.model = cl_mod_explode;
    ex->ent.flags = RF_FULLBRIGHT | RF_TRANSLUCENT;
    ex->ent.scale = 3;
    ex->ent.skinnum = 2;
    ex->start = cl.servertime - CG_FRAMETIME;
    ex->light = 550;
    VectorSet(ex->lightcolor, 0.19f, 0.41f, 0.75f);
    ex->frames = 4;
}

static void CG_SoundEvent(centity_t *cent, uint32_t param)
{
    int channel = (param >> 13) & 7;
    int index = param & (MAX_SOUNDS - 1);
    int vol = (param >> 24) & 255;
    int att = (param >> 16) & 255;
    if (vol == 0)
        vol = 255;
    if (att == ATTN_ESCAPE_CODE)
        att = 0;
    else if (att == 0)
        att = ATTN_ESCAPE_CODE;
    S_StartSound(NULL, cent->current.number, channel, cl.sound_precache[index], vol / 255.0f, att / 64.0f, 0);
}

static void CG_SplashEvent(centity_t *cent, entity_event_t color, uint32_t param)
{
    int count = (param >> 8) & 255;
    const vec_t *pos = cent->current.origin;
    vec3_t dir;
    int r;

    ByteToDir(param & 255, dir);

    if (color == EV_SPLASH_ELECTRIC_N64) {
        CG_ParticleEffect(pos, dir, 0x6c, count / 2);
        CG_ParticleEffect(pos, dir, 0xb0, (count + 1) / 2);
        color = EV_SPLASH_SPARKS;
    } else {
        static const byte splash_color[] = { 0x00, 0xe0, 0xb0, 0x50, 0xd0, 0xe0, 0xe8 };
        if (color - EV_SPLASH_UNKNOWN >= q_countof(splash_color))
            r = 0x00;
        else
            r = splash_color[color - EV_SPLASH_UNKNOWN];
        CG_ParticleEffect(pos, dir, r, count);
    }

    if (color == EV_SPLASH_SPARKS) {
        r = Q_rand() & 3;
        if (r == 0)
            S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_spark5, 1, ATTN_STATIC, 0);
        else if (r == 1)
            S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_spark6, 1, ATTN_STATIC, 0);
        else
            S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_spark7, 1, ATTN_STATIC, 0);
    }
}

static void CG_DamageEvent(const centity_t *cent, entity_event_t type, uint32_t param)
{
    int color = (param >>  8) & 255;
    int count = (param >> 16) & 255;
    const vec_t *pos = cent->current.origin;
    vec3_t dir;

    if (cl_disable_particles->integer & NOPART_BLOOD && type < EV_GUNSHOT)
        return;

    ByteToDir(param & 255, dir);

    switch (type) {
    case EV_BLOOD:
        CG_ParticleEffect(pos, dir, 0xe8, 60);
        break;
    case EV_MORE_BLOOD:
        CG_ParticleEffect(pos, dir, 0xe8, 250);
        break;
    case EV_GREEN_BLOOD:
        CG_ParticleEffect2(pos, dir, 0xdf, 30);
        break;
    case EV_GUNSHOT:
        CG_ParticleEffect(pos, dir, 0, 40);
        break;
    case EV_SHOTGUN:
        CG_ParticleEffect(pos, dir, 0, 20);
        break;
    case EV_SPARKS:
    case EV_BULLET_SPARKS:
        CG_ParticleEffect(pos, dir, 0xe0, 6);
        break;
    case EV_HEATBEAM_SPARKS:
        CG_ParticleSteamEffect(pos, dir, 0x08, 50, 60);
        break;
    case EV_HEATBEAM_STEAM:
        CG_ParticleSteamEffect(pos, dir, 0xe0, 20, 60);
        break;
    case EV_SCREEN_SPARKS:
        CG_ParticleEffect(pos, dir, 0xd0, 40);
        break;
    case EV_SHIELD_SPARKS:
        CG_ParticleEffect(pos, dir, 0xb0, 40);
        break;
    case EV_ELECTRIC_SPARKS:
        CG_ParticleEffect(pos, dir, 0x75, 40);
        break;
    case EV_LASER_SPARKS:
    case EV_WELDING_SPARKS:
        CG_ParticleEffect2(pos, dir, color, count);
        break;
    case EV_TUNNEL_SPARKS:
        CG_ParticleEffect3(pos, dir, color, count);
        break;
    default:
        break;
    }

    switch (type) {
    case EV_GUNSHOT:
    case EV_SHOTGUN:
    case EV_BULLET_SPARKS:
        CG_SmokeAndFlash(pos);
        break;
    case EV_HEATBEAM_SPARKS:
    case EV_HEATBEAM_STEAM:
        S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_lashit, 1, ATTN_NORM, 0);
        break;
    case EV_SCREEN_SPARKS:
    case EV_SHIELD_SPARKS:
    case EV_ELECTRIC_SPARKS:
        S_StartSound(pos, ENTITYNUM_WORLD, 257, cl_sfx_lashit, 1, ATTN_NORM, 0);
        break;
    default:
        break;
    }

    if (type == EV_GUNSHOT || type == EV_BULLET_SPARKS) {
        int r = Q_rand() & 15;
        if (r == 1)
            S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_ric1, 1, ATTN_NORM, 0);
        else if (r == 2)
            S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_ric2, 1, ATTN_NORM, 0);
        else if (r == 3)
            S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_ric3, 1, ATTN_NORM, 0);
    }

    if (type == EV_WELDING_SPARKS) {
        explosion_t *ex = CG_PlainExplosion(pos);
        ex->type = ex_light;
        ex->light = 100 + (Q_rand() % 75);
        VectorSet(ex->lightcolor, 1.0f, 1.0f, 0.3f);
        ex->frames = 2;
    }
}

static void CG_ExplosionEvent(const centity_t *cent, entity_event_t type, uint32_t param)
{
    const vec_t *pos = cent->current.origin;
    explosion_t *ex;
    vec3_t dir;

    ByteToDir(param & 255, dir);

    switch (type) {
    case EV_EXPLOSION_PLAIN:
        CG_PlainExplosion(pos);
        break;
    case EV_EXPLOSION1:
    case EV_EXPLOSION1_NL:
        ex = CG_PlainExplosion(pos);
        if (type == EV_EXPLOSION1_NL)
            ex->light = 0;
        CG_ExplosionParticles(pos);
        S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_rockexp, 1, ATTN_NORM, 0);
        break;

    case EV_EXPLOSION1_NP:
        CG_PlainExplosion(pos);
        S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_rockexp, 1, ATTN_NORM, 0);
        break;

    case EV_EXPLOSION1_BIG:
        ex = CG_PlainExplosion(pos);
        ex->ent.model = cl_mod_explo4;
        ex->ent.scale = 2.0f;
        S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_rockexp, 1, ATTN_NORM, 0);
        break;

    case EV_EXPLOSION2:
    case EV_EXPLOSION2_NL:
        ex = CG_PlainExplosion(pos);
        ex->frames = 19;
        ex->baseframe = 30;
        if (type == EV_EXPLOSION2_NL)
            ex->light = 0;
        CG_ExplosionParticles(pos);
        S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_grenexp, 1, ATTN_NORM, 0);
        break;

    case EV_BLASTER:            // blaster hitting wall
    case EV_BLASTER2:           // green blaster hitting wall
    case EV_FLECHETTE:          // flechette
        ex = CG_AllocExplosion();
        VectorCopy(pos, ex->ent.origin);
        dirtoangles(dir, ex->ent.angles);
        ex->type = ex_misc;
        ex->ent.flags = RF_FULLBRIGHT | RF_TRANSLUCENT;
        switch (type) {
        case EV_BLASTER:
            CG_BlasterParticles(pos, dir);
            ex->lightcolor[0] = 1;
            ex->lightcolor[1] = 1;
            break;
        case EV_BLASTER2:
            CG_BlasterParticles2(pos, dir, 0xd0);
            ex->ent.skinnum = 1;
            ex->lightcolor[1] = 1;
            break;
        default:
            CG_BlasterParticles2(pos, dir, 0x6f);
            ex->ent.skinnum = 2;
            VectorSet(ex->lightcolor, 0.19f, 0.41f, 0.75f);
            break;
        }
        ex->start = cl.servertime - CG_FRAMETIME;
        ex->light = 150;
        ex->ent.model = cl_mod_explode;
        ex->frames = 4;
        S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_lashit, 1, ATTN_NORM, 0);
        break;

    case EV_BLUEHYPERBLASTER:
        CG_BlasterParticles(pos, dir);
        break;

    case EV_GRENADE_EXPLOSION:
    case EV_GRENADE_EXPLOSION_WATER:
        ex = CG_PlainExplosion(pos);
        ex->frames = 19;
        ex->baseframe = 30;
        if (cl_disable_explosions->integer & NOEXP_GRENADE)
            ex->type = ex_light;

        if (!(cl_disable_particles->integer & NOPART_GRENADE_EXPLOSION))
            CG_ExplosionParticles(pos);

        if (cl_dlight_hacks->integer & DLHACK_SMALLER_EXPLOSION)
            ex->light = 200;

        if (type == EV_GRENADE_EXPLOSION_WATER)
            S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_watrexp, 1, ATTN_NORM, 0);
        else
            S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_grenexp, 1, ATTN_NORM, 0);
        break;

    case EV_ROCKET_EXPLOSION:
    case EV_ROCKET_EXPLOSION_WATER:
        ex = CG_PlainExplosion(pos);
        if (cl_disable_explosions->integer & NOEXP_ROCKET)
            ex->type = ex_light;

        if (!(cl_disable_particles->integer & NOPART_ROCKET_EXPLOSION))
            CG_ExplosionParticles(pos);

        if (cl_dlight_hacks->integer & DLHACK_SMALLER_EXPLOSION)
            ex->light = 200;

        if (type == EV_ROCKET_EXPLOSION_WATER)
            S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_watrexp, 1, ATTN_NORM, 0);
        else
            S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_rockexp, 1, ATTN_NORM, 0);
        break;

    case EV_BFG_EXPLOSION:
        CG_BFGExplosion(pos);
        break;

    case EV_BFG_EXPLOSION_BIG:
        CG_BFGExplosionParticles(pos);
        break;

    case EV_TRACKER_EXPLOSION:
        CG_ColorFlash(pos, 0, 150, -1, -1, -1);
        CG_ColorExplosionParticles(pos, 0, 1);
        S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_disrexp, 1, ATTN_NORM, 0);
        break;

    default:
        break;
    }
}

// an entity has just been parsed that has an event value
static void CG_EntityEvent(centity_t *cent, entity_event_t event, uint32_t param)
{
    entity_state_t *s = &cent->current;
    const vec_t *start = s->old_origin;
    int number = s->number;
    vec3_t dir;

    switch (event) {
    case EV_ITEM_RESPAWN:
        S_StartSound(NULL, number, CHAN_WEAPON, S_RegisterSound("items/respawn1.wav"), 1, ATTN_IDLE, 0);
        CG_ItemRespawnParticles(s->origin);
        break;
    case EV_PLAYER_TELEPORT:
        S_StartSound(NULL, number, CHAN_WEAPON, S_RegisterSound("misc/tele1.wav"), 1, ATTN_IDLE, 0);
        CG_TeleportParticles(s->origin);
        break;
    case EV_FOOTSTEP:
        if (cl_footsteps->integer)
            CG_PlayFootstepSfx(FOOTSTEP_ID_DEFAULT, number, 1.0f, ATTN_NORM);
        break;
    case EV_OTHER_FOOTSTEP:
        if (cl_footsteps->integer)
            CG_PlayFootstepSfx(FOOTSTEP_ID_DEFAULT, number, 0.5f, ATTN_IDLE);
        break;
    case EV_LADDER_STEP:
        if (cl_footsteps->integer)
            CG_PlayFootstepSfx(FOOTSTEP_ID_LADDER, number, 0.5f, ATTN_IDLE);
        break;
    case EV_FALLSHORT:
        S_StartSound(NULL, number, CHAN_AUTO, S_RegisterSound("player/land1.wav"), 1, ATTN_NORM, 0);
        break;
    case EV_FALL:
        S_StartSound(NULL, number, CHAN_AUTO, S_RegisterSound("*fall2.wav"), 1, ATTN_NORM, 0);
        break;
    case EV_FALLFAR:
        S_StartSound(NULL, number, CHAN_AUTO, S_RegisterSound("*fall1.wav"), 1, ATTN_NORM, 0);
        break;
    case EV_MUZZLEFLASH:
        CG_MuzzleFlash(cent, param);
        break;
    case EV_MUZZLEFLASH2:
        CG_MuzzleFlash2(cent, param);
        break;
    case EV_SOUND:
        CG_SoundEvent(cent, param);
        break;
    case EV_BERSERK_SLAM:
    case EV_GUNCMDR_SLAM:
        CG_BerserkSlam(cent, event);
        break;

    case EV_RAILTRAIL:
    case EV_RAILTRAIL2:
        CG_RailTrail(start, s->origin, event);
        S_StartSound(s->origin, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_railg, 1, ATTN_NORM, 0);
        break;

    case EV_BUBBLETRAIL:
        CG_BubbleTrail(start, s->origin);
        break;

    case EV_BUBBLETRAIL2:
        CG_BubbleTrail2(start, s->origin, 8);
        S_StartSound(start, ENTITYNUM_WORLD, CHAN_AUTO, cl_sfx_lashit, 1, ATTN_NORM, 0);
        break;

    case EV_BFG_LASER:
        CG_ParseLaser(start, s->origin, 0xd0d1d2d3);
        break;

    case EV_BFG_ZAP:
        CG_ParseLaser(start, s->origin, 0xd0d1d2d3);
        CG_BFGExplosion(s->origin);
        break;

    case EV_SPLASH_UNKNOWN ... EV_SPLASH_ELECTRIC_N64:
        CG_SplashEvent(cent, event, param);
        break;

    case EV_BLOOD ... EV_TUNNEL_SPARKS:
        CG_DamageEvent(cent, event, param);
        break;

    case EV_EXPLOSION_PLAIN ... EV_TRACKER_EXPLOSION:
        CG_ExplosionEvent(cent, event, param);
        break;

    case EV_POWER_SPLASH:
        S_StartSound(NULL, number, CHAN_AUTO, S_RegisterSound("misc/mon_power2.wav"), 1, ATTN_NORM, 0);
        CG_PowerSplash(cent);
        break;

    case EV_BOSSTPORT:          // boss teleporting to station
        CG_BigTeleportParticles(s->origin);
        S_StartSound(s->origin, ENTITYNUM_WORLD, CHAN_AUTO, S_RegisterSound("misc/bigtele.wav"), 1, ATTN_NONE, 0);
        break;

    case EV_NUKEBLAST:
        CG_ParseNuke(s->origin);
        break;

    case EV_CHAINFIST_SMOKE:
        VectorSet(dir, 0, 0, 1);
        CG_ParticleSmokeEffect(s->origin, dir, 0, 20, 20);
        break;

    case EV_TELEPORT_EFFECT:
        CG_TeleportParticles(s->origin);
        break;

    case EV_WIDOWBEAMOUT:
        CG_ParseWidow(s->origin);
        break;

    case EV_WIDOWSPLASH:
        CG_WidowSplash(s->origin);
        break;

    default:
        break;
    }
}

void CG_EntityEvents(centity_t *cent)
{
    entity_state_t *s = &cent->current;

    if (CG_FRAMESYNC) {
        // EF_TELEPORTER acts like an event, but is not cleared each frame
        if (s->effects & EF_TELEPORTER)
            CG_TeleporterParticles(s->origin);

        if (s->morefx & EFX_TELEPORTER2)
            CG_TeleporterParticles2(s->origin);

        if (s->morefx & EFX_BARREL_EXPLODING)
            CG_BarrelExplodingParticles(s->origin);

        if (s->morefx & EFX_STEAM) {
            uint32_t param = s->skinnum;
            int color = (param >> 8) & 0xff;
            int count = (param >> 16) & 0xff;
            int magnitude = (param >> 24) & 0xff;
            vec3_t dir;
            ByteToDir(param & 0xff, dir);
            CG_ParticleSteamEffect(s->origin, dir, color, count, magnitude);
        }
    }

#if USE_FPS
    if (cent->event_frame != cl.frame.number)
        return;
#endif

    CG_EntityEvent(cent, s->event[0], s->event_param[0]);
    CG_EntityEvent(cent, s->event[1], s->event_param[1]);
}

/*
=================
CG_AddTEnts
=================
*/
void CG_AddTEnts(void)
{
    CG_AddExplosions();
    CG_ProcessSustain();
    CG_AddLasers();
}

/*
=================
CG_ClearTEnts
=================
*/
void CG_ClearTEnts(void)
{
    CG_ClearExplosions();
    CG_ClearLasers();
    CG_ClearSustains();
}

void CG_InitTEnts(void)
{
    cl_muzzleflashes = Cvar_Get("cl_muzzleflashes", "1", 0);
    cl_hit_markers = Cvar_Get("cl_hit_markers", "2", 0);
    cl_railtrail_type = Cvar_Get("cl_railtrail_type", "0", 0);
    cl_railtrail_time = Cvar_Get("cl_railtrail_time", "1.0", 0);
    cl_railtrail_time->changed = cl_timeout_changed;
    cl_railtrail_time->changed(cl_railtrail_time);
    cl_railcore_color = Cvar_Get("cl_railcore_color", "red", 0);
    cl_railcore_color->changed = cl_railcore_color_changed;
    cl_railcore_color->generator = Com_Color_g;
    cl_railcore_color_changed(cl_railcore_color);
    cl_railcore_width = Cvar_Get("cl_railcore_width", "2", 0);
    cl_railspiral_color = Cvar_Get("cl_railspiral_color", "blue", 0);
    cl_railspiral_color->changed = cl_railspiral_color_changed;
    cl_railspiral_color->generator = Com_Color_g;
    cl_railspiral_color_changed(cl_railspiral_color);
    cl_railspiral_radius = Cvar_Get("cl_railspiral_radius", "3", 0);
}
