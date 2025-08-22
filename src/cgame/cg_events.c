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
// cg_events.c -- client side temporary entities

#include "cg_local.h"

/*
=================
CG_FindFootstepSurface
=================
*/
static int CG_FindFootstepSurface(int entnum)
{
    const centity_t *cent = &cg_entities[entnum];

    // skip if no materials loaded
    if (cgs.num_footsteps <= MATERIAL_RESERVED_COUNT)
        return MATERIAL_ID_DEFAULT;

    // allow custom footsteps to be disabled
    if (cg_footsteps.integer >= 2)
        return MATERIAL_ID_DEFAULT;

    // use an X/Y only mins/maxs copy of the entity,
    // since we don't want it to get caught inside of any geometry above or below
    const vec3_t trace_mins = { cent->mins[0], cent->mins[1] };
    const vec3_t trace_maxs = { cent->maxs[0], cent->maxs[1] };

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
        return MATERIAL_ID_DEFAULT;
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

        return surf.material_id;
    }

    return MATERIAL_ID_DEFAULT;
}

/*
=================
CG_PlayFootstepSfx
=================
*/
static void CG_PlayFootstepSfx(unsigned step_id, int entnum, float volume, float attenuation)
{
    const cg_footstep_sfx_t *sfx;
    qhandle_t footstep_sfx;
    int sfx_num;

    if (!cg_footsteps.integer)
        return;
    if (!cgs.num_footsteps)
        return; // should not really happen

    if (step_id == MATERIAL_ID_DEFAULT)
        step_id = CG_FindFootstepSurface(entnum);
    if (step_id >= cgs.num_footsteps)
        step_id = MATERIAL_ID_DEFAULT;

    sfx = &cgs.footsteps[step_id];
    if (sfx->num_sfx <= 0)
        sfx = &cgs.footsteps[0];
    if (sfx->num_sfx <= 0)
        return; // no footsteps, not even fallbacks

    // pick a random footstep sound, but avoid playing the same one twice in a row
    sfx_num = Q_rand_uniform(sfx->num_sfx);
    footstep_sfx = sfx->sfx[sfx_num];
    if (footstep_sfx == cg.last_footstep)
        footstep_sfx = sfx->sfx[(sfx_num + 1) % sfx->num_sfx];

    trap_S_StartSound(NULL, entnum, CHAN_FOOTSTEP, footstep_sfx, volume, attenuation, 0);
    cg.last_footstep = footstep_sfx;
}

/*
=================
CG_RegisterFootstep
=================
*/
static void CG_RegisterFootstep(cg_footstep_sfx_t *sfx, const char *material)
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
        sfx->sfx[i] = trap_S_RegisterSound(name);
        if (!sfx->sfx[i])
            break;
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
    material_info_t info;
    int i;

    // load reserved footsteps
    CG_RegisterFootstep(&cgs.footsteps[MATERIAL_ID_DEFAULT], NULL);
    CG_RegisterFootstep(&cgs.footsteps[MATERIAL_ID_LADDER], "ladder");

    // load the rest
    for (i = MATERIAL_RESERVED_COUNT; i < MAX_MATERIALS; i++) {
        if (!trap_GetMaterialInfo(i, &info))
            break;
        CG_RegisterFootstep(&cgs.footsteps[i], info.material);
    }

    cgs.num_footsteps = i;
}

/*
=================
CG_RegisterTEntSounds
=================
*/
void CG_RegisterTEntSounds(void)
{
    cgs.sounds.ric1 = trap_S_RegisterSound("world/ric1.wav");
    cgs.sounds.ric2 = trap_S_RegisterSound("world/ric2.wav");
    cgs.sounds.ric3 = trap_S_RegisterSound("world/ric3.wav");
    cgs.sounds.lashit = trap_S_RegisterSound("weapons/lashit.wav");
    cgs.sounds.spark5 = trap_S_RegisterSound("world/spark5.wav");
    cgs.sounds.spark6 = trap_S_RegisterSound("world/spark6.wav");
    cgs.sounds.spark7 = trap_S_RegisterSound("world/spark7.wav");
    cgs.sounds.railg = trap_S_RegisterSound("weapons/railgf1a.wav");
    cgs.sounds.rockexp = trap_S_RegisterSound("weapons/rocklx1a.wav");
    cgs.sounds.grenexp = trap_S_RegisterSound("weapons/grenlx1a.wav");
    cgs.sounds.watrexp = trap_S_RegisterSound("weapons/xpld_wat.wav");
    cgs.sounds.land1 = trap_S_RegisterSound("player/land1.wav");
    cgs.sounds.lightning = trap_S_RegisterSound("weapons/tesla.wav");
    cgs.sounds.disrexp = trap_S_RegisterSound("weapons/disrupthit.wav");
    cgs.sounds.hit_marker = trap_S_RegisterSound("weapons/marker.wav");
    cgs.sounds.lowammo = trap_S_RegisterSound("weapons/lowammo.wav");
    cgs.sounds.help_marker = trap_S_RegisterSound("misc/help_marker.wav");
    cgs.sounds.talk[0] = trap_S_RegisterSound("misc/talk.wav");
    cgs.sounds.talk[1] = trap_S_RegisterSound("misc/talk1.wav");

    CG_RegisterFootsteps();
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
    cgs.models.explode = trap_R_RegisterModel("models/objects/explode/tris.md2");
    cgs.models.smoke = trap_R_RegisterModel("models/objects/smoke/tris.md2");
    cgs.models.flash = trap_R_RegisterModel("models/objects/flash/tris.md2");
    cgs.models.parasite_segment = trap_R_RegisterModel("models/monsters/parasite/segment/tris.md2");
    cgs.models.grapple_cable = trap_R_RegisterModel("models/ctf/segment/tris.md2");
    cgs.models.explo4 = trap_R_RegisterModel("models/objects/r_explode/tris.md2");
    cgs.models.bfg_explo = trap_R_RegisterModel("sprites/s_bfg2.sp2");
    cgs.models.powerscreen = trap_R_RegisterModel("models/items/armor/effect/tris.md2");
    cgs.models.lightning = trap_R_RegisterModel("models/proj/lightning/tris.md2");
    cgs.models.heatbeam = trap_R_RegisterModel("models/proj/beam/tris.md2");
    cgs.models.shell = trap_R_RegisterModel("models/items/shell/tris.md2");
    cgs.models.help_marker = trap_R_RegisterModel("models/objects/pointer/tris.md2");

    for (int i = 0; i < MFLASH_TOTAL; i++)
        cgs.models.muzzles[i] = trap_R_RegisterModel(va("models/weapons/%s/flash/tris.md2", muzzlenames[i]));

    // check for remaster powerscreen model
    cgs.need_powerscreen_scale = trap_FS_OpenFile("models/items/armor/effect/tris.md2", NULL, 0) == 2300;
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
        ex_light,
        ex_marker
    } type;

    entity_t    ent;
    int         frames;
    float       light;
    vec3_t      lightcolor;
    unsigned    start;
    int         baseframe;
} explosion_t;

static explosion_t  cg_explosions[MAX_EXPLOSIONS];

static void CG_ClearExplosions(void)
{
    memset(cg_explosions, 0, sizeof(cg_explosions));
}

static explosion_t *CG_AllocExplosion(void)
{
    explosion_t *e, *oldest;
    int         i;
    unsigned    time;

    for (i = 0, e = cg_explosions; i < MAX_EXPLOSIONS; i++, e++) {
        if (e->type == ex_free) {
            memset(e, 0, sizeof(*e));
            return e;
        }
    }
// find the oldest explosion
    time = cg.time;
    oldest = cg_explosions;

    for (i = 0, e = cg_explosions; i < MAX_EXPLOSIONS; i++, e++) {
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
    ex->ent.flags = RF_FULLBRIGHT | RF_TRANSLUCENT;
    ex->start = cg.oldframe->servertime;
    ex->light = 350;
    VectorSet(ex->lightcolor, 1.0f, 0.5f, 0.5f);
    ex->ent.angles[1] = Q_rand() % 360;
    ex->ent.model = cgs.models.explo4;
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
    ex->ent.flags = RF_FULLBRIGHT | RF_TRANSLUCENT;
    ex->start = cg.oldframe->servertime;
    ex->light = 350;
    VectorSet(ex->lightcolor, 0.0f, 1.0f, 0.0f);
    ex->ent.model = cgs.models.bfg_explo;
    ex->ent.alpha = 0.30f;
    ex->frames = 4;
}

void CG_AddWeaponMuzzleFX(cg_muzzlefx_t fx, const vec3_t offset, float scale)
{
    if (!cg_muzzleflashes.integer)
        return;

    Q_assert(fx < q_countof(cgs.models.muzzles));

    if (!cgs.models.muzzles[fx])
        return;

    cg.weapon.muzzle.model = cgs.models.muzzles[fx];
    cg.weapon.muzzle.scale = scale;
    if (fx == MFLASH_MACHN || fx == MFLASH_BEAMER)
        cg.weapon.muzzle.roll = Q_rand() % 360;
    else
        cg.weapon.muzzle.roll = 0;
    VectorCopy(offset, cg.weapon.muzzle.offset);
    cg.weapon.muzzle.time = cg.oldframe->servertime;
}

void CG_AddMuzzleFX(const vec3_t origin, const vec3_t angles, cg_muzzlefx_t fx, int skin, float scale)
{
    explosion_t *ex;

    if (!cg_muzzleflashes.integer)
        return;

    Q_assert(fx < q_countof(cgs.models.muzzles));

    if (!cgs.models.muzzles[fx])
        return;

    ex = CG_AllocExplosion();
    VectorCopy(origin, ex->ent.origin);
    VectorCopy(angles, ex->ent.angles);
    ex->type = ex_mflash;
    ex->ent.flags = RF_TRANSLUCENT | RF_NOSHADOW | RF_FULLBRIGHT;
    ex->ent.alpha = 1.0f;
    ex->start = cg.oldframe->servertime;
    ex->ent.model = cgs.models.muzzles[fx];
    ex->ent.skinnum = skin;
    ex->ent.scale = scale;
    if (fx != MFLASH_BOOMER)
        ex->ent.angles[2] = Q_rand() % 360;
}

void CG_AddHelpPath(const vec3_t origin, const vec3_t dir, bool first)
{
    if (first) {
        int i;
        explosion_t *ex;

        for (i = 0, ex = cg_explosions; i < MAX_EXPLOSIONS; i++, ex++) {
            if (ex->type == ex_marker) {
                ex->type = ex_free;
                continue;
            }
        }
    }

    explosion_t *ex = CG_AllocExplosion();
    VectorCopy(origin, ex->ent.origin);
    ex->lightcolor[0] = origin[2] + 16.0f;
    vectoangles(dir, ex->ent.angles);
    ex->type = ex_marker;
    ex->ent.flags = RF_NOSHADOW | RF_MINLIGHT | RF_TRANSLUCENT;
    ex->ent.alpha = 1.0f;
    ex->start = cg.time;
    ex->ent.model = cgs.models.help_marker;
    ex->ent.scale = 2.5f;
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
    ex->start = cg.oldframe->servertime;
    ex->ent.model = cgs.models.smoke;

    ex = CG_AllocExplosion();
    VectorCopy(origin, ex->ent.origin);
    ex->type = ex_flash;
    ex->ent.flags = RF_FULLBRIGHT;
    ex->frames = 2;
    ex->start = cg.oldframe->servertime;
    ex->ent.model = cgs.models.flash;
}

static void CG_AddExplosions(void)
{
    entity_t    *ent;
    int         i;
    explosion_t *ex;
    float       frac;
    int         f;

    for (i = 0, ex = cg_explosions; i < MAX_EXPLOSIONS; i++, ex++) {
        if (ex->type == ex_free)
            continue;

        frac = (cg.time - ex->start) * BASE_1_FRAMETIME;
        f = floorf(frac);

        ent = &ex->ent;

        switch (ex->type) {
        case ex_misc:
        case ex_light:
            if (f >= ex->frames - 1) {
                ex->type = ex_free;
                continue;
            }
            ent->alpha = 1.0f - frac / (ex->frames - 1);
            break;

        case ex_flash:
            if (f >= 1) {
                ex->type = ex_free;
                continue;
            }
            ent->alpha = 1.0f;
            break;

        case ex_mflash:
            if (cg.time - ex->start > 50)
                ex->type = ex_free;
            else
                trap_R_AddEntity(ent);
            continue;

        case ex_poly:
            if (f >= ex->frames - 1) {
                ex->type = ex_free;
                continue;
            }

            ent->alpha = 1.0f - frac / (ex->frames - 1);

            if (f < 10) {
                ent->skinnum = (f >> 1);
                if (ent->skinnum < 0)
                    ent->skinnum = 0;
            } else {
                if (f < 13)
                    ent->skinnum = 5;
                else
                    ent->skinnum = 6;
            }
            break;

        case ex_marker:
            frac = (cg.time - ex->start) / (cg_compass_time.value * 1000.0f);

            if (frac > 1.0f) {
                ex->type = ex_free;
                continue;
            }

            ent->alpha = (1.0f - frac) * 0.5f;

            frac = 1.0f - (cg.time - ex->start) / 1000.0f;

            if (frac > 0) {
                frac = frac * frac * frac * frac * frac;
                ent->origin[2] = ex->lightcolor[0] + frac * 512.0f;
            } else {
                ent->origin[2] = ex->lightcolor[0];
            }

            trap_R_AddEntity(ent);
            continue;

        default:
            Q_assert(!"bad type");
        }

        if (ex->light)
            trap_R_AddSphereLight(ent->origin, ex->light * ent->alpha,
                                  ex->lightcolor[0], ex->lightcolor[1], ex->lightcolor[2]);

        if (ex->type != ex_light) {
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

static laser_t  cg_lasers[MAX_LASERS];

static void CG_ClearLasers(void)
{
    memset(cg_lasers, 0, sizeof(cg_lasers));
}

static laser_t *CG_AllocLaser(void)
{
    laser_t *l;
    int i;

    for (i = 0, l = cg_lasers; i < MAX_LASERS; i++, l++) {
        if (cg.time - l->starttime >= l->lifetime) {
            memset(l, 0, sizeof(*l));
            l->starttime = cg.time;
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

    for (i = 0, l = cg_lasers; i < MAX_LASERS; i++, l++) {
        time = l->lifetime - (cg.time - l->starttime);
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

static cg_sustain_t     cg_sustains[MAX_SUSTAINS];

static void CG_ClearSustains(void)
{
    memset(cg_sustains, 0, sizeof(cg_sustains));
}

static cg_sustain_t *CG_AllocSustain(void)
{
    cg_sustain_t    *s;
    int             i;

    for (i = 0, s = cg_sustains; i < MAX_SUSTAINS; i++, s++) {
        if (!s->think)
            return s;
    }

    return NULL;
}

static void CG_ProcessSustain(void)
{
    cg_sustain_t    *s;
    int             i;

    for (i = 0, s = cg_sustains; i < MAX_SUSTAINS; i++, s++) {
        if (s->think) {
            if (s->endtime >= cg.time)
                s->think(s);
            else
                s->think = NULL;
        }
    }
}

static void CG_ParseWidow(const vec3_t pos)
{
    cg_sustain_t    *s;

    s = CG_AllocSustain();
    if (!s)
        return;

    VectorCopy(pos, s->org);
    s->endtime = cg.time + 2100;
    s->think = CG_Widowbeamout;
}

static void CG_ParseNuke(const vec3_t pos)
{
    explosion_t     *ex;
    cg_sustain_t    *s;

    trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_VOICE, cgs.sounds.grenexp, 1, ATTN_NONE, 0);
    trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.rockexp, 1, ATTN_NORM, 0);

    ex = CG_PlainExplosion(pos);
    ex->ent.model = cgs.models.explo4;
    ex->ent.scale = 2.0f;

    s = CG_AllocSustain();
    if (!s)
        return;

    VectorCopy(pos, s->org);
    s->endtime = cg.time + 1000;
    s->think = CG_Nukeblast;
}

//==============================================================

static color_t  railcore_color = { U32_RED };
static color_t  railspiral_color = { U32_BLUE };

static void CG_RailCore(const vec3_t start, const vec3_t end)
{
    laser_t *l;

    l = CG_AllocLaser();
    if (!l)
        return;

    if (cg_railcore_color.modified) {
        COM_ParseColor(cg_railcore_color.string, &railcore_color);
        cg_railcore_color.modified = false;
    }

    VectorCopy(start, l->start);
    VectorCopy(end, l->end);
    l->color = -1;
    l->lifetime = cg_railtrail_time.integer;
    l->width = cg_railcore_width.integer;
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

    if (cg_railspiral_color.modified) {
        COM_ParseColor(cg_railspiral_color.string, &railspiral_color);
        cg_railspiral_color.modified = false;
    }

    for (i = 0; i < len; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        VectorClear(p->accel);

        d = i * 0.1f;
        c = cosf(d);
        s = sinf(d);

        VectorScale(right, c, dir);
        VectorMA(dir, s, up, dir);

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (cg_railtrail_time.value + frand() * 0.2f);
        p->color = -1;
        p->rgba = railspiral_color;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + dir[j] * cg_railspiral_radius.value;
            p->vel[j] = dir[j] * 6;
        }

        VectorAdd(move, vec, move);
    }
}

static void CG_RailTrail(const vec3_t start, const vec3_t end, entity_event_t event)
{
    if (!cg_railtrail_type.integer && event != EV_RAILTRAIL2) {
        CG_OldRailTrail(start, end);
    } else {
        if (cg_railcore_width.integer > 0) {
            CG_RailCore(start, end);
        }
        if (cg_railtrail_type.integer > 1) {
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
        trap_S_StartSound(NULL, cent->current.number, CHAN_WEAPON, trap_S_RegisterSound("mutant/thud1.wav"), 1, ATTN_NORM, 0);
        trap_S_StartSound(NULL, cent->current.number, CHAN_AUTO, trap_S_RegisterSound("world/explod2.wav"), 0.75f, ATTN_NORM, 0);
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
    ex->ent.model = cgs.models.explode;
    ex->ent.flags = RF_FULLBRIGHT | RF_TRANSLUCENT;
    ex->ent.scale = 3;
    ex->ent.skinnum = 2;
    ex->start = cg.oldframe->servertime;
    ex->light = 550;
    VectorSet(ex->lightcolor, 0.19f, 0.41f, 0.75f);
    ex->frames = 4;
}

static void CG_SoundEvent(centity_t *cent, uint32_t param)
{
    int index = param & (MAX_SOUNDS - 1);
    int vol = (param >> 24) & 255;
    int att = (param >> 16) & 255;
    int channel = (param >> 11) & 31;
    if (vol == 0)
        vol = 255;
    if (att == ATTN_ESCAPE_CODE)
        att = 0;
    else if (att == 0)
        att = ATTN_ESCAPE_CODE;
    trap_S_StartSound(NULL, cent->current.number, channel, cgs.sounds.precache[index], vol / 255.0f, att / 64.0f, 0);
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
            trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.spark5, 1, ATTN_STATIC, 0);
        else if (r == 1)
            trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.spark6, 1, ATTN_STATIC, 0);
        else
            trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.spark7, 1, ATTN_STATIC, 0);
    }
}

static void CG_DamageEvent(const centity_t *cent, entity_event_t type, uint32_t param)
{
    int color = (param >>  8) & 255;
    int count = (param >> 16) & 255;
    const vec_t *pos = cent->current.origin;
    vec3_t dir;

    if (cg_disable_particles.integer & NOPART_BLOOD && type < EV_GUNSHOT)
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
        CG_ParticleSteamEffect(pos, dir, 0xe0, 20, 60);
        break;
    case EV_HEATBEAM_STEAM:
        CG_ParticleSteamEffect(pos, dir, 0x08, 50, 60);
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
        trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.lashit, 1, ATTN_NORM, 0);
        break;
    case EV_SCREEN_SPARKS:
    case EV_SHIELD_SPARKS:
    case EV_ELECTRIC_SPARKS:
        trap_S_StartSound(pos, ENTITYNUM_WORLD, 257, cgs.sounds.lashit, 1, ATTN_NORM, 0);
        break;
    default:
        break;
    }

    if (type == EV_GUNSHOT || type == EV_BULLET_SPARKS) {
        int r = Q_rand() & 15;
        if (r == 1)
            trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.ric1, 1, ATTN_NORM, 0);
        else if (r == 2)
            trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.ric2, 1, ATTN_NORM, 0);
        else if (r == 3)
            trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.ric3, 1, ATTN_NORM, 0);
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
        trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.rockexp, 1, ATTN_NORM, 0);
        break;

    case EV_EXPLOSION1_NP:
        CG_PlainExplosion(pos);
        trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.rockexp, 1, ATTN_NORM, 0);
        break;

    case EV_EXPLOSION1_BIG:
        ex = CG_PlainExplosion(pos);
        ex->ent.model = cgs.models.explo4;
        ex->ent.scale = 2.0f;
        trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.rockexp, 1, ATTN_NORM, 0);
        break;

    case EV_EXPLOSION2:
    case EV_EXPLOSION2_NL:
        ex = CG_PlainExplosion(pos);
        ex->frames = 19;
        ex->baseframe = 30;
        if (type == EV_EXPLOSION2_NL)
            ex->light = 0;
        CG_ExplosionParticles(pos);
        trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.grenexp, 1, ATTN_NORM, 0);
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
        ex->start = cg.oldframe->servertime;
        ex->light = 150;
        ex->ent.model = cgs.models.explode;
        ex->frames = 4;
        trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.lashit, 1, ATTN_NORM, 0);
        break;

    case EV_BLUEHYPERBLASTER:
        CG_BlasterParticles(pos, dir);
        break;

    case EV_GRENADE_EXPLOSION:
    case EV_GRENADE_EXPLOSION_WATER:
        ex = CG_PlainExplosion(pos);
        ex->frames = 19;
        ex->baseframe = 30;
        if (cg_disable_explosions.integer & NOEXP_GRENADE)
            ex->type = ex_light;

        if (!(cg_disable_particles.integer & NOPART_GRENADE_EXPLOSION))
            CG_ExplosionParticles(pos);

        if (cg_dlight_hacks.integer & DLHACK_SMALLER_EXPLOSION)
            ex->light = 200;

        if (type == EV_GRENADE_EXPLOSION_WATER)
            trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.watrexp, 1, ATTN_NORM, 0);
        else
            trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.grenexp, 1, ATTN_NORM, 0);
        break;

    case EV_ROCKET_EXPLOSION:
    case EV_ROCKET_EXPLOSION_WATER:
        ex = CG_PlainExplosion(pos);
        if (cg_disable_explosions.integer & NOEXP_ROCKET)
            ex->type = ex_light;

        if (!(cg_disable_particles.integer & NOPART_ROCKET_EXPLOSION))
            CG_ExplosionParticles(pos);

        if (cg_dlight_hacks.integer & DLHACK_SMALLER_EXPLOSION)
            ex->light = 200;

        if (type == EV_ROCKET_EXPLOSION_WATER)
            trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.watrexp, 1, ATTN_NORM, 0);
        else
            trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.rockexp, 1, ATTN_NORM, 0);
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
        trap_S_StartSound(pos, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.disrexp, 1, ATTN_NORM, 0);
        break;

    default:
        break;
    }
}

static void CG_SexedSound(int number, soundchan_t channel, sexed_sound_t index, float volume, float attenuation)
{
    const clientinfo_t *ci;

    if (number < MAX_CLIENTS)
        ci = &cgs.clientinfo[number];
    else
        ci = &cgs.baseclientinfo;

    trap_S_StartSound(NULL, number, channel, ci->sounds[index], volume, attenuation, 0);
}

static void CG_PainEvent(int number, int health)
{
    sexed_sound_t l;

    if (health < 25)
        l = SS_PAIN25_1;
    else if (health < 50)
        l = SS_PAIN50_1;
    else if (health < 75)
        l = SS_PAIN75_1;
    else
        l = SS_PAIN100_1;

    CG_SexedSound(number, CHAN_VOICE, l + (Q_rand() & 1), 1, ATTN_NORM);
}

static void CG_StairStep(centity_t *cent)
{
    float step_height = cent->current.origin[2] - cent->prev.origin[2];
    float prev_step   = 0;

    // check for stepping up before a previous step is completed
    int delta = cg.time - cent->step_time;
    if (delta < STEP_TIME)
        prev_step = cent->step_factor * (STEP_TIME - delta);

    cent->step_factor = Q_clipf(prev_step + step_height, -MAX_STEP, MAX_STEP) / STEP_TIME;
    cent->step_time   = cg.oldframe->servertime;

    // step local view too for demos if this is player entity
    if (cent->current.number == cg.frame->ps.clientnum && !CG_PredictionEnabled()) {
        step_height = cg.frame->ps.origin[2] - cg.oldframe->ps.origin[2];
        prev_step   = 0;

        // check for stepping up before a previous step is completed
        delta = cgs.realtime - cg.predicted_step_time;
        if (delta < STEP_TIME)
            prev_step = cg.predicted_step * (STEP_TIME - delta);

        cg.predicted_step = Q_clipf(prev_step + step_height, -MAX_STEP, MAX_STEP) / STEP_TIME;
        cg.predicted_step_time = cgs.realtime;
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
        trap_S_StartSound(NULL, number, CHAN_WEAPON, trap_S_RegisterSound("items/respawn1.wav"), 1, ATTN_IDLE, 0);
        CG_ItemRespawnParticles(s->origin);
        break;
    case EV_PLAYER_TELEPORT:
        trap_S_StartSound(NULL, number, CHAN_WEAPON, trap_S_RegisterSound("misc/tele1.wav"), 1, ATTN_IDLE, 0);
        CG_TeleportParticles(s->origin);
        break;
    case EV_FOOTSTEP:
        CG_PlayFootstepSfx(MATERIAL_ID_DEFAULT, number, 1.0f, ATTN_NORM);
        break;
    case EV_OTHER_FOOTSTEP:
        CG_PlayFootstepSfx(MATERIAL_ID_DEFAULT, number, 0.5f, ATTN_IDLE);
        break;
    case EV_LADDER_STEP:
        CG_PlayFootstepSfx(MATERIAL_ID_LADDER, number, 0.5f, ATTN_IDLE);
        break;
    case EV_STAIR_STEP:
        CG_StairStep(cent);
        break;
    case EV_FALL:
        if (param >= 55)
            CG_SexedSound(number, CHAN_AUTO, SS_FALL1, 1, ATTN_NORM);
        else if (param > 30)
            CG_SexedSound(number, CHAN_AUTO, SS_FALL2, 1, ATTN_NORM);
        else
            trap_S_StartSound(NULL, number, CHAN_AUTO, cgs.sounds.land1, 1, ATTN_NORM, 0);
        if (number == cg.frame->ps.clientnum) {
            cg.fall_time = cg.oldframe->servertime + FALL_TIME;
            cg.fall_value = min(param / 2, 40);
        }
        break;
    case EV_DEATH1 ... EV_DEATH4:
        CG_SexedSound(number, CHAN_VOICE, SS_DEATH1 + (event - EV_DEATH1), 1, ATTN_NORM);
        break;
    case EV_PAIN:
        CG_PainEvent(number, param);
        break;
    case EV_GURP:
        CG_SexedSound(number, CHAN_VOICE, SS_GURP1 + (Q_rand() & 1), 1, ATTN_NORM);
        break;
    case EV_DROWN:
        CG_SexedSound(number, CHAN_VOICE, SS_DROWN, 1, ATTN_NORM);
        break;
    case EV_JUMP:
        CG_SexedSound(number, CHAN_VOICE, SS_JUMP, 1, ATTN_NORM);
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
    case EV_EARTHQUAKE:
        if (cg.quake_time < cg.time) {
            VectorSet(cg.quake_angles[0], crand(), crand(), crand());
            VectorSet(cg.quake_angles[1], crand(), crand(), crand());
            cg.quake_time = cg.oldframe->servertime + QUAKE_TIME;
        }
        break;
    case EV_EARTHQUAKE2:
        cg.v_dmg_pitch = param * -0.1f;
        cg.v_dmg_time = cg.oldframe->servertime + DAMAGE_TIME;
        break;

    case EV_RAILTRAIL:
    case EV_RAILTRAIL2:
        CG_RailTrail(start, s->origin, event);
        trap_S_StartSound(s->origin, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.railg, 1, ATTN_NORM, 0);
        break;

    case EV_BUBBLETRAIL:
        CG_BubbleTrail(start, s->origin);
        break;

    case EV_BUBBLETRAIL2:
        CG_BubbleTrail2(start, s->origin, 8);
        trap_S_StartSound(start, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.lashit, 1, ATTN_NORM, 0);
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
        trap_S_StartSound(NULL, number, CHAN_AUTO, trap_S_RegisterSound("misc/mon_power2.wav"), 1, ATTN_NORM, 0);
        CG_PowerSplash(cent);
        break;

    case EV_BOSSTPORT:          // boss teleporting to station
        CG_BigTeleportParticles(s->origin);
        trap_S_StartSound(s->origin, ENTITYNUM_WORLD, CHAN_AUTO, trap_S_RegisterSound("misc/bigtele.wav"), 1, ATTN_NONE, 0);
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

void CG_EntityEffects(centity_t *cent)
{
    const entity_state_t *s = &cent->current;

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

void CG_EntityEvents(centity_t *cent)
{
    const entity_state_t *s = &cent->current;

    for (int i = 0; i < MAX_EVENTS; i++)
        if (s->event[i])
            CG_EntityEvent(cent, s->event[i], s->event_param[i]);
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
