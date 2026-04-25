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
// cg_entities.c -- entity parsing and management

#include "cg_local.h"

static void CG_SetEntitySoundOrigin(const centity_t *ent);

/*
=========================================================================

FRAME PARSING

=========================================================================
*/

/*
================
CG_PlayerToEntityState

Restores entity origin and angles from player state
================
*/
static void CG_PlayerToEntityState(const player_state_t *ps, entity_state_t *es)
{
    vec_t pitch;

    es->origin = ps->origin;

    pitch = ps->viewangles.pitch;
    if (pitch > 180) {
        pitch -= 360;
    }
    es->angles.pitch = pitch / 3;
    es->angles.yaw = ps->viewangles.yaw;
    es->angles.roll = 0;
}

static bool CG_EntityWasTeleported(const entity_state_t *state)
{
    for (int i = 0; i < MAX_EVENTS; i++)
        if (state->event[i] == EV_PLAYER_TELEPORT || state->event[i] == EV_OTHER_TELEPORT)
            return true;
    return false;
}

static void CG_InitEntity(centity_t *ent, const entity_state_t *state)
{
    ent->trailcount = 1024;     // for diminishing rocket / grenade trails
    ent->step_time = 0;

    // duplicate the current state so lerping doesn't hurt anything
    ent->prev = *state;
    ent->prev_frame = ent->curr_frame = state->frame;
    ent->anim_start = cg.oldframe->servertime;
}

static void CG_DeltaEntityNew(centity_t *ent, const entity_state_t *state)
{
    CG_InitEntity(ent, state);

    if (CG_EntityWasTeleported(state) || (state->renderfx & RF_BEAM) || state->number == cg.frame->ps.clientnum) {
        // no lerping if teleported, or no valid old_origin
        ent->lerp_origin = state->origin;
        return;
    }

    // old_origin is valid for new entities, use it as starting point for
    // interpolating between
    ent->prev.origin = state->old_origin;
    ent->lerp_origin = state->old_origin;
}

static void CG_DeltaEntityOld(centity_t *ent, const entity_state_t *state)
{
    if (state->modelindex != ent->current.modelindex
        || state->modelindex2 != ent->current.modelindex2
        || state->modelindex3 != ent->current.modelindex3
        || state->modelindex4 != ent->current.modelindex4
        || CG_EntityWasTeleported(state)) {
        // some data changes will force no lerping
        CG_InitEntity(ent, state);

        // no lerping if teleported or morphed
        ent->lerp_origin = state->origin;
        return;
    }

    // start alias model animation
    if (cg.oldframe->servertime - ent->anim_start >= BASE_FRAMETIME) {
        ent->prev_frame = ent->curr_frame;
        ent->curr_frame = state->frame;
        ent->anim_start = cg.oldframe->servertime;
    }

    // shuffle the last state to previous
    ent->prev = ent->current;
}

static bool CG_EntityIsNew(const centity_t *ent)
{
    if (ent->serverframe != cg.oldframe->number)
        return true;    // wasn't in last received frame

    if (cg.oldframe->number != cg.frame->number - 1)
        return true;    // previous server frame was dropped

    return false;
}

static void CG_DeltaEntity(entity_state_t *state)
{
    centity_t *ent = &cg_entities[state->number];

    // if entity is solid, decode mins/maxs and add to the list
    if (state->solid && state->number != cg.frame->ps.clientnum)
        cg.solid_entities[cg.num_solid_entities++] = ent;

    if (state->solid == PACKED_BSP) {
        ent->box = trap_GetBrushModelBounds(state->modelindex);
        ent->radius = 0;
    } else if (state->solid) {
        // encoded bbox
        ent->box = MSG_UnpackSolid(state->solid);
        if (state->scale)
            ent->box = Box3_Scale(ent->box, state->scale);
        ent->radius = Box3_Radius(ent->box);
    } else {
        ent->box = box3_origin;
        ent->radius = 0;
    }

    // work around Q2PRO server bandwidth optimization
    if (state->number == cg.frame->ps.clientnum)
        CG_PlayerToEntityState(&cg.frame->ps, state);

    if (CG_EntityIsNew(ent)) {
        // wasn't in last update, so initialize some things
        CG_DeltaEntityNew(ent, state);
    } else {
        CG_DeltaEntityOld(ent, state);
    }

    ent->serverframe = cg.frame->number;
    ent->current = *state;

    // set sound origin for events
    CG_SetEntitySoundOrigin(ent);
}

static void CG_DeltaPlayerstate(void)
{
    // find states to interpolate between
    const player_state_t *ps = &cg.frame->ps;
    player_state_t *ops = &cg.oldframe->ps;

    // no lerping if POV number changed
    if (ops->clientnum != ps->clientnum) {
        *ops = *ps;
        cg.weapon.prev_frame = ps->gunframe;
    }

    // hit markers
    if (ps->stats[STAT_HITS] > ops->stats[STAT_HITS]) {
        if (cg_hit_markers.integer > 0) {
            cg.hit_marker_count = ps->stats[STAT_HITS] - ops->stats[STAT_HITS];
            cg.hit_marker_time = cgs.realtime;
            if (cg_hit_markers.integer > 1)
                trap_S_StartSound(ps->clientnum, CHAN_HIT, cgs.sounds.hit_marker, 1, ATTN_NONE, 0);
        }
    }

    // low ammo warning
    int weapon = ps->stats[STAT_ACTIVE_WEAPON];
    if (weapon > 0 && weapon <= cgs.wheel.num_weapons && weapon == ops->stats[STAT_ACTIVE_WEAPON]) {
        int warn = cgs.wheel.weapons[weapon - 1].quantity_warn;
        if (ps->stats[STAT_AMMO] <= warn && ops->stats[STAT_AMMO] > warn)
            trap_S_StartSound(ps->clientnum, CHAN_AUTO, cgs.sounds.lowammo, 1, ATTN_NONE, 0);
    }

    // no lerping if teleport bit was flipped
    if ((ops->rdflags ^ ps->rdflags) & RDF_TELEPORT_BIT)
        *ops = *ps;

    if (ps->stats[STAT_DAMAGE]) {
        float kick = (ps->stats[STAT_DAMAGE] & 255) * 0.3f;
        int dir_b = (ps->stats[STAT_DAMAGE] >> 8) & 255;

        if (dir_b) {
            vec3_t dir = ByteToDir(dir_b);
            cg.v_dmg_pitch = -kick * Vec3_Dot(dir, cg.v_forward);
            cg.v_dmg_roll  =  kick * Vec3_Dot(dir, cg.v_right);
        } else {
            // make non-directional damage always centered
            cg.v_dmg_pitch = -kick;
            cg.v_dmg_roll = 0;
        }

        cg.v_dmg_time = cg.oldframe->servertime + DAMAGE_TIME;
    }

    if (!CG_PredictionEnabled()) {
        if (ps->viewheight != ops->viewheight) {
            cg.duck_time = cg.oldframe->servertime + DUCK_TIME;
            cg.duck_factor = (float)(ps->viewheight - ops->viewheight) / DUCK_TIME;
        }
    }

    if (ps->gunframe != ops->gunframe) {
        cg.weapon.prev_frame = ops->gunframe;
        cg.weapon.anim_start = cg.oldframe->servertime;
    }
}

/*
==================
CG_DeltaFrame

A valid frame has been parsed.
==================
*/
void CG_DeltaFrame(void)
{
    // rebuild the list of solid entities for this frame
    cg.num_solid_entities = 0;

    bool effects = false;
    if (cg.frame->servertime - cg.last_effects_time >= BASE_FRAMETIME) {
        effects = true;
        cg.last_effects_time = cg.frame->servertime;
    }

    // set current and prev, unpack solid, etc
    for (int i = 0; i < cg.frame->num_entities; i++)
        CG_DeltaEntity(&cg.frame->entities[i]);

    // fire events. due to footstep tracing this must be done
    // after updating all entities.
    for (int i = 0; i < cg.frame->num_entities; i++) {
        centity_t *ent = &cg_entities[cg.frame->entities[i].number];
        if (effects)
            CG_EntityEffects(ent);
        CG_EntityEvents(ent);
    }

    if (cgs.demoplayback) {
        // this delta has nothing to do with local viewangles,
        // clear it to avoid interfering with demo freelook hack
        cg.frame->ps.delta_angles = vec3_origin;
    }

    CG_DeltaPlayerstate();

    CG_CheckPredictionError();

    SCR_SetCrosshairColor();
}

/*
==========================================================================

INTERPOLATE BETWEEN FRAMES TO GET RENDERING PARAMS

==========================================================================
*/

float CG_LerpEntityAlpha(const centity_t *ent)
{
    float prev = ent->prev.alpha;
    float curr = ent->current.alpha;

    // no lerping from/to default alpha
    if (prev && curr)
        return prev + cg.lerpfrac * (curr - prev);

    return curr ? curr : 1.0f;
}

static float CG_HandMultiplier(void)
{
    if (cg_gun.integer == 3)
        return -1;
    if (cg_gun.integer == 2)
        return 1;
    if (info_hand.integer == 2)
        return 0;
    if (info_hand.integer == 1)
        return -1;

    return 1;
}

static void CG_DrawBeam(vec3_t start, vec3_t end, const centity_t *cent)
{
    int         i, steps;
    vec3_t      dist, org, offset = vec3_origin;
    float       x, y, z, d;
    entity_t    ent;
    vec3_t      angles;
    float       len;
    int         framenum;
    float       model_length;
    float       hand_multiplier = 0;
    qhandle_t   model = cgs.models.precache[cent->current.modelindex];
    int         entnum = cent->current.othernum;
    float       scale = cent->current.scale;

    if (!scale)
        scale = 1;

    if (entnum < cgs.maxclients) {
        if (model == cgs.models.heatbeam)
            offset = Vec3(2, 7, -3);
        else if (model == cgs.models.grapple_cable)
            offset = Vec3(9, 12, -3);
        else if (model == cgs.models.lightning)
            offset = Vec3(0, 12, -12);
    }

    // if coming from the player, update the start position
    if (entnum == cg.frame->ps.clientnum) {
        hand_multiplier = CG_HandMultiplier();

        // set up gun position
        org = cg.refdef.vieworg;

        org = Vec3_MA(org, cg_gun_y.value, cg.v_forward);
        org = Vec3_MA(org, cg_gun_x.value, cg.v_right);
        org = Vec3_MA(org, cg_gun_z.value, cg.v_up);

        x = offset.x;
        y = offset.y;
        z = offset.z;

        // adjust offset for gun fov
        if (cg_gunfov.value > 0) {
            float fov_x = Q_clipf(cg_gunfov.value, 30, 160);
            float fov_y = V_CalcFov(fov_x, 4, 3);

            x *= tanf(cg.fov_x * (M_PIf / 360)) / tanf(fov_x * (M_PIf / 360));
            z *= tanf(cg.fov_y * (M_PIf / 360)) / tanf(fov_y * (M_PIf / 360));
        }

        org = Vec3_MA(org, hand_multiplier * x, cg.v_right);
        org = Vec3_MA(org, y, cg.v_forward);
        org = Vec3_MA(org, z, cg.v_up);
        if (hand_multiplier == 0)
            org = Vec3_MA(org, -1, cg.v_up);

        // calculate pitch and yaw
        dist = Vec3_Sub(end, org);

        if (model != cgs.models.grapple_cable) {
            d = Vec3_Length(dist);
            dist = Vec3_Scale(cg.v_forward, d);
        }

        // FIXME: use cg.refdef.viewangles?
        angles = vectoangles(dist);

        // if it's the heatbeam, draw the particle effect
        if (model == cgs.models.heatbeam && !sv_paused.integer)
            CG_Heatbeam(org, dist);

        framenum = 1;
    } else {
        org = start;

        // if it's a player, use the hardcoded player offset
        if (entnum < cgs.maxclients) {
            vec3_t  tmp, f, r, u;

            // calculate pitch and yaw
            dist = Vec3_Sub(end, org);
            angles = vectoangles(dist);

            tmp.pitch = -angles.pitch;
            tmp.yaw = angles.yaw + 180.0f;
            tmp.roll = 0;
            AngleVectors(tmp, &f, &r, &u);

            org = Vec3_MA(org, -offset.x + 1, r);
            org = Vec3_MA(org, -offset.y, f);
            org = Vec3_MA(org, -offset.z - 10, u);
        } else if (model == cgs.models.heatbeam) {
            // if it's a monster, do the particle effect
            CG_MonsterPlasma_Shell(start);
        }

        // calculate pitch and yaw
        dist = Vec3_Sub(end, org);
        angles = vectoangles(dist);

        framenum = 2;
    }

    // add new entities for the beams
    d = Vec3_Normalize(&dist);
    if (model == cgs.models.heatbeam)
        model_length = 32.0f * scale;
    else if (model == cgs.models.lightning)
        model_length = 35.0f * scale;
    else
        model_length = 30.0f * scale;

    // correction for grapple cable model, which has origin in the middle
    if (entnum == cg.frame->ps.clientnum && model == cgs.models.grapple_cable && hand_multiplier) {
        org = Vec3_MA(org, model_length * 0.5f, dist);
        d -= model_length * 0.5f;
    }

    steps = ceilf(d / model_length);

    memset(&ent, 0, sizeof(ent));
    ent.model = model;
    ent.scale = scale;
    ent.alpha = CG_LerpEntityAlpha(cent);
    if (ent.alpha != 1.0f)
        ent.flags |= RF_TRANSLUCENT;

    if (steps > 1) {
        len = (d - model_length) / (steps - 1);
        dist = Vec3_Scale(dist, len);
    }

    if (model == cgs.models.heatbeam) {
        ent.frame = framenum;
        ent.flags |= RF_FULLBRIGHT;
        ent.angles.pitch = -angles.pitch;
        ent.angles.yaw = angles.yaw + 180.0f;
        ent.angles.roll = cg.time % 360;
    } else if (model == cgs.models.lightning) {
        ent.flags |= RF_FULLBRIGHT;
        ent.angles.pitch = -angles.pitch;
        ent.angles.yaw = angles.yaw + 180.0f;
    } else {
        ent.flags |= RF_NOSHADOW;
        ent.angles.pitch = angles.pitch;
        ent.angles.yaw = angles.yaw;
    }

    ent.origin = org;
    for (i = 0; i < steps; i++) {
        if (model != cgs.models.heatbeam)
            ent.angles.roll = Com_SlowRand() % 360;
        trap_R_AddEntity(&ent);
        ent.origin = Vec3_Add(ent.origin, dist);
    }
}

static void CG_SetEntitySoundOrigin(const centity_t *ent)
{
    vec3_t org, vel;

    // interpolate origin
    org = Vec3_Lerp(ent->prev.origin, ent->current.origin, cg.lerpfrac);

    // offset the origin for BSP models
    if (ent->current.solid == PACKED_BSP)
        org = Vec3_Add(org, Box3_Center(ent->box));

    // set velocity for doppler effect
    if (cg.frame->servertime > cg.oldframe->servertime) {
        float time = 1000.0f / (cg.frame->servertime - cg.oldframe->servertime);
        vel = Vec3_Sub(ent->current.origin, ent->prev.origin);
        vel = Vec3_Scale(vel, time);
    } else {
        vel = vec3_origin;
    }

    trap_S_UpdateEntity(ent->current.number, org, vel);
}

static void CG_AddEntityLoopingSound(const entity_state_t *ent)
{
    int index = ent->sound & (MAX_SOUNDS - 1);

    if (!index)
        return;
    if (sv_paused.integer)
        return;
    if (cg_loopsounds.integer <= 0)
        return;
    if (cg_loopsounds.integer == 2 && !ent->modelindex)
        return;
    if (cg_loopsounds.integer == 3 && ent->number != cg.frame->ps.clientnum)
        return;

    int vol = (ent->sound >> 24) & 255;
    int att = (ent->sound >> 16) & 255;
    int channel = (ent->sound >> 11) & 31;  // only used for NO_STEREO flag

    if (vol == 0)
        vol = 255;
    if (att == ATTN_ESCAPE_CODE)
        att = 0;
    else if (att == 0)
        att = ATTN_ESCAPE_CODE;

    trap_S_AddLoopingSound(ent->number, cgs.sounds.precache[index], vol / 255.0f, att / 64.0f, !(channel & CHAN_NO_STEREO));
}

int CG_EntityShellEffect(const entity_state_t *s)
{
    int renderfx = 0;

    if (s->effects & EF_PENT)
        renderfx |= RF_SHELL_RED;

    if (s->effects & EF_QUAD)
        renderfx |= RF_SHELL_BLUE;

    if (s->effects & EF_DOUBLE)
        renderfx |= RF_SHELL_DOUBLE;

    if (s->effects & EF_HALF_DAMAGE)
        renderfx |= RF_SHELL_HALF_DAM;

    if (s->morefx & EFX_DUALFIRE)
        renderfx |= RF_SHELL_LITE_GREEN;

    if (s->effects & EF_COLOR_SHELL)
        renderfx |= s->renderfx & RF_SHELL_MASK;

    // PMM - at this point, all of the shells have been handled
    // if we're in the rogue pack, set up the custom mixing, otherwise just
    // keep going

    // all of the solo colors are fine.  we need to catch any of the combinations that look bad
    // (double & half) and turn them into the appropriate color, and make double/quad something special
    if (renderfx & RF_SHELL_HALF_DAM) {
        // ditch the half damage shell if any of red, blue, or double are on
        if (renderfx & (RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_DOUBLE))
            renderfx &= ~RF_SHELL_HALF_DAM;
    }

    if (renderfx & RF_SHELL_DOUBLE) {
        // lose the yellow shell if we have a red, blue, or green shell
        if (renderfx & (RF_SHELL_RED | RF_SHELL_BLUE | RF_SHELL_GREEN))
            renderfx &= ~RF_SHELL_DOUBLE;
        // if we have a red shell, turn it to purple by adding blue
        if (renderfx & RF_SHELL_RED)
            renderfx |= RF_SHELL_BLUE;
        // if we have a blue shell (and not a red shell), turn it to cyan by adding green
        else if (renderfx & RF_SHELL_BLUE) {
            // go to green if it's on already, otherwise do cyan (flash green)
            if (renderfx & RF_SHELL_GREEN)
                renderfx &= ~RF_SHELL_BLUE;
            else
                renderfx |= RF_SHELL_GREEN;
        }
    }

    return renderfx;
}

/*
===============
CG_AddPacketEntities

===============
*/
static void CG_AddPacketEntities(void)
{
    entity_t                ent;
    const entity_state_t    *s1;
    float                   autorotate, autobob, autolerp;
    int                     i, pnum, autoanim;
    centity_t               *cent;
    const clientinfo_t      *ci;
    unsigned int            effects, renderfx, shellfx;
    bool                    has_trail;
    float                   ent_alpha;
    uint64_t                ent_flags;

    // bonus items rotate at a fixed rate
    autorotate = cg.time * 0.1f;

    // brush models can auto animate their frames
    autoanim = cg.time / 500;
    autolerp = 1.0f - (cg.time % 500) * 0.002;

    autobob = 5 * sinf(cg.time / 400.0f);

    memset(&ent, 0, sizeof(ent));

    for (pnum = 0; pnum < cg.frame->num_entities; pnum++) {
        s1 = &cg.frame->entities[pnum];

        cent = &cg_entities[s1->number];

        has_trail = false;

        effects = s1->effects;
        renderfx = s1->renderfx;

        // set frame
        if (effects & EF_ANIM01) {
            ent.frame = autoanim & 1;
            ent.oldframe = ent.frame ^ 1;
            ent.backlerp = autolerp;
        } else if (effects & EF_ANIM23) {
            ent.frame = 2 + (autoanim & 1);
            ent.oldframe = ent.frame ^ 1;
            ent.backlerp = autolerp;
        } else if (effects & EF_ANIM_ALL) {
            ent.frame = autoanim;
            ent.oldframe = ent.frame - 1;
            ent.backlerp = autolerp;
        } else if (effects & EF_ANIM_ALLFAST) {
            ent.frame = cg.time / 100;
            ent.oldframe = ent.frame - 1;
            ent.backlerp = 1.0f - (cg.time % 100) * 0.01f;
        } else if (renderfx & RF_BEAM) {
            ent.frame = ent.oldframe = s1->frame;
            ent.backlerp = 0;
        } else if (cent->prev_frame != cent->curr_frame) {
            // run alias model animation
            int delta = cg.time - cent->anim_start;

            if (delta > BASE_FRAMETIME) {
                cent->prev_frame = cent->curr_frame;
                cent->curr_frame = s1->frame;
                // start next frame
                if (cent->prev_frame != cent->curr_frame) {
                    cent->anim_start += BASE_FRAMETIME;
                    delta = cg.time - cent->anim_start;
                }
            }

            ent.frame = cent->curr_frame;
            ent.oldframe = cent->prev_frame;
            ent.backlerp = 1.0f - Q_clipf(delta * BASE_1_FRAMETIME, 0, 1);
        } else {
            ent.frame = ent.oldframe = cent->curr_frame;
            ent.backlerp = 0;
        }

        if (renderfx & RF_BEAM) {
            // interpolate start and end points for beams
            ent.origin = Vec3_Lerp(cent->prev.origin, cent->current.origin, cg.lerpfrac);
            ent.oldorigin = Vec3_Lerp(cent->prev.old_origin, cent->current.old_origin, cg.lerpfrac);
        } else {
            if (s1->number == cg.frame->ps.clientnum) {
                // use predicted origin
                ent.origin = cg.player_entity_origin;
                ent.oldorigin = cg.player_entity_origin;
            } else {
                // interpolate origin
                ent.origin = Vec3_Lerp(cent->prev.origin, cent->current.origin, cg.lerpfrac);
                // smooth out stair climbing
                int delta = cg.time - cent->step_time;
                if (delta < STEP_TIME)
                    ent.origin.z = cent->current.origin.z - cent->step_factor * (STEP_TIME - delta);
                ent.oldorigin = ent.origin;
            }

            // optionally remove the glowing effect
            if (cg_noglow.integer)
                renderfx &= ~RF_GLOW;
        }

        if (effects & EF_BOB && !cg_nobob.integer) {
            ent.origin.z += autobob;
            ent.oldorigin.z += autobob;
        }

        if (!cg_gibs.integer) {
            if (effects & EF_GIB && !(effects & EF_ROCKET))
                goto skip;
            if (effects & EF_GREENGIB)
                goto skip;
        }

        // create a new entity
        if (renderfx & RF_FLARE) {
            if (!cg_flares.integer)
                goto skip;
            float fade_start = s1->modelindex2;
            float fade_end = s1->modelindex3;
            float d = Vec3_Distance(cg.refdef.vieworg, ent.origin);
            if (d < fade_start)
                goto skip;
            if (d > fade_end)
                ent.alpha = 1;
            else
                ent.alpha = (d - fade_start) / (fade_end - fade_start);
            ent.skin = 0;
            if (renderfx & RF_CUSTOMSKIN && s1->frame < MAX_IMAGES)
                ent.skin = cgs.images.precache[s1->frame];
            if (!ent.skin)
                ent.skin = cgs.images.flare;
            ent.scale = s1->scale ? s1->scale : 1;
            ent.flags = renderfx | RF_TRANSLUCENT;
            if (!s1->skinnum)
                ent.rgba.u32 = U32_WHITE;
            else
                ent.rgba.u32 = BigLong(s1->skinnum);
            ent.skinnum = s1->number;
            trap_R_AddEntity(&ent);
            goto skip;
        }

        if (renderfx & RF_CUSTOM_LIGHT) {
            color_t color;
            if (!s1->skinnum)
                color.u32 = U32_WHITE;
            else
                color.u32 = BigLong(s1->skinnum);
            dlight_t light = {
                .origin = ent.origin,
                .radius = s1->frame,
                .color = Vec3_Scale(Vec3_Load(color.u8), 1.0f / 255.0f)
            };
            trap_R_AddLight(&light);
            goto skip;
        }

        if (renderfx & RF_CASTSHADOW) {
            if (!cg_shadowlights.integer)
                goto skip;

            float scale = s1->scale;
            float fade_start = s1->modelindex2;
            float fade_end = s1->modelindex3;
            if (fade_end > fade_start) {
                float d = Vec3_Distance(cg.refdef.vieworg, ent.origin);
                if (d > fade_end)
                    goto skip;
                if (d > fade_start)
                    scale *= 1.0f - (d - fade_start) / (fade_end - fade_start);
            }

            int style = s1->frame & 255;
            if (style)
                scale *= cg.lightstyles[style - 1];

            color_t color;
            if (!s1->skinnum)
                color.u32 = U32_WHITE;
            else
                color.u32 = BigLong(s1->skinnum);

            dlight_t light = {
                .origin = ent.origin,
                .radius = s1->modelindex,
                .color = Vec3_Scale(Vec3_Load(color.u8), scale / 255.0f),
                .cone_angle = s1->angles.roll,
                .resolution = s1->modelindex4,
                .key = s1->number,
            };

            AngleVectors(s1->angles, &light.dir, NULL, NULL);

            trap_R_AddLight(&light);
            goto skip;
        }

        if (renderfx & RF_BEAM && s1->modelindex > MODELINDEX_DUMMY) {
            CG_DrawBeam(ent.oldorigin, ent.origin, cent);
            goto skip;
        }

        // tweak the color of beams
        if (renderfx & RF_BEAM) {
            // the four beam colors are encoded in 32 bits of skinnum (hack)
            ent.skinnum = (s1->skinnum >> ((Com_SlowRand() % 4) * 8)) & 0xff;
            ent.skin = 0;
            ent.model = 0;
        } else {
            // set skin
            if (s1->solid == PACKED_BSP) {
                ent.skinnum = 0;
                ent.skin = 0;
                ent.model = ~s1->modelindex;
            } else if (s1->modelindex == MODELINDEX_PLAYER) {
                // use custom player skin
                ent.skinnum = 0;
                ci = &cgs.clientinfo[s1->skinnum & (MAX_CLIENTS - 1)];
                ent.skin = ci->skin;
                ent.model = ci->model;
                if (!ent.skin || !ent.model) {
                    ent.skin = cgs.baseclientinfo.skin;
                    ent.model = cgs.baseclientinfo.model;
                    ci = &cgs.baseclientinfo;
                }
                if (renderfx & RF_USE_DISGUISE) {
                    char buffer[MAX_QPATH];

                    Q_concat(buffer, sizeof(buffer), "players/", ci->model_name, "/disguise.pcx");
                    ent.skin = trap_R_RegisterSkin(buffer);
                }
            } else {
                ent.skinnum = s1->skinnum;
                ent.skin = 0;
                ent.model = cgs.models.precache[s1->modelindex];
            }
        }

        // allow skin override for remaster
        if (renderfx & RF_CUSTOMSKIN && s1->skinnum < MAX_IMAGES) {
            ent.skin = cgs.images.precache[s1->skinnum];
            ent.skinnum = 0;
        }

        // calculate angles
        if (effects & EF_ROTATE) {  // some bonus items auto-rotate
            ent.angles.pitch = 0;
            ent.angles.yaw = autorotate;
            ent.angles.roll = 0;
        } else if (effects & EF_SPINNINGLIGHTS) {
            vec3_t forward;
            vec3_t start;

            ent.angles.pitch = 0;
            ent.angles.yaw = cg.time / 2 + s1->angles.yaw;
            ent.angles.roll = 180;

            AngleVectors(ent.angles, &forward, NULL, NULL);
            start = Vec3_MA(ent.origin, 64, forward);
            CG_AddSphereLight(start, 100, 1, 0, 0);
        } else if (s1->number == cg.frame->ps.clientnum) {
            ent.angles = cg.player_entity_angles;    // use predicted angles
        } else { // interpolate angles
            ent.angles = Vec3_LerpAngles(cent->prev.angles, cent->current.angles, cg.lerpfrac);
            // mimic original ref_gl "leaning" bug (uuugly!)
            if (s1->solid != PACKED_BSP && s1->modelindex == MODELINDEX_PLAYER && cg_rollhack.integer)
                ent.angles.roll = -ent.angles.roll;
        }

        if (s1->morefx & EFX_FLASHLIGHT) {
            dlight_t light = {
                .radius = 512.0f,
                .cone_angle = 22.0f,
                .color = { 2, 2, 2 },
            };

            if (s1->number == cg.frame->ps.clientnum) {
                float hand = CG_HandMultiplier();
                light.origin = Vec3_MA(cg.refdef.vieworg, 7.0f * hand, cg.v_right);
                light.dir = cg.v_forward;
                light.flags = RF_VIEWERMODEL;   // skip player model shadow
            } else {
                light.origin = ent.origin;
                AngleVectors(ent.angles, &light.dir, NULL, NULL);
            }

            trap_R_AddLight(&light);
        }

        if (s1->morefx & EFX_GRENADE_LIGHT)
            CG_AddSphereLight(ent.origin, 100, 1, 1, 0);

        // if set to invisible, skip
        if (!s1->modelindex)
            goto skip;

        // only draw from mirrors, etc
        if (s1->number == cg.frame->ps.clientnum && !cg.third_person_view)
            renderfx |= RF_VIEWERMODEL;

        // beams don't have color shells
        if (renderfx & RF_BEAM)
            shellfx = 0;
        else
            shellfx = CG_EntityShellEffect(s1);

        // renderfx go on color shell entity
        if (shellfx)
            renderfx &= ~RF_SHELL_MASK;

        // render effects (fullbright, translucent, etc)
        ent_alpha = 1.0f;
        ent_flags = renderfx & ~RF_TRANSLUCENT;

        // tracker effect is duplicated for linked models
        if (IS_TRACKER(effects))
            ent_flags |= RF_TRACKER;

        // custom alpha overrides any derived value
        if (s1->alpha) {
            ent_alpha = CG_LerpEntityAlpha(cent);
        } else {
            if (renderfx & RF_BEAM)
                ent_alpha = 0.3f;
            else if (renderfx & RF_TRANSLUCENT)
                ent_alpha = 0.7f;

            if (effects & EF_BFG)
                ent_alpha = 0.3f;

            if (effects & EF_PLASMA)
                ent_alpha = 0.6f;

            if (effects & EF_SPHERETRANS) {
                if (effects & EF_TRACKERTRAIL)
                    ent_alpha = 0.6f;
                else
                    ent_alpha = 0.3f;
            }
        }

        // add third person alpha
        if (s1->number == cg.frame->ps.clientnum && cg.third_person_view)
            ent_alpha *= cg.third_person_alpha;

        if (ent_alpha != 1.0f)
            ent_flags |= RF_TRANSLUCENT;

        ent.flags = ent_flags;
        ent.alpha = ent_alpha;
        ent.scale = s1->scale;

        // add to refresh list
        trap_R_AddEntity(&ent);

        // color shells generate a separate entity for the main model
        if (shellfx) {
            ent.flags = ent_flags | shellfx | RF_TRANSLUCENT;
            ent.alpha = ent_alpha * 0.30f;
            trap_R_AddEntity(&ent);
        }

        // never use beam flag on others
        ent_flags &= ~(uint64_t)RF_BEAM;

        ent.skin = 0;       // never use a custom skin on others
        ent.skinnum = 0;
        ent.flags = ent_flags;
        ent.alpha = ent_alpha;

        // duplicate for linked models
        if (s1->modelindex2) {
            if (s1->modelindex2 == MODELINDEX_PLAYER) {
                // custom weapon
                ci = &cgs.clientinfo[s1->skinnum & (MAX_CLIENTS - 1)];
                i = (s1->skinnum >> 8) & (MAX_CLIENTWEAPONS - 1); // 0 is default weapon model
                ent.model = ci->weaponmodel[i];
                if (!ent.model) {
                    if (i != 0)
                        ent.model = ci->weaponmodel[0];
                    if (!ent.model)
                        ent.model = cgs.baseclientinfo.weaponmodel[0];
                }
            } else {
                ent.model = cgs.models.precache[s1->modelindex2];

                // PMM - check for the defender sphere shell .. make it translucent
                if (ent.model == cgs.models.shell) {
                    ent.flags = ent_flags | RF_TRANSLUCENT;
                    ent.alpha = ent_alpha * 0.32f;
                }
            }

            trap_R_AddEntity(&ent);

            // PGM - make sure these get reset.
            ent.flags = ent_flags;
            ent.alpha = ent_alpha;
        }

        if (s1->modelindex3) {
            ent.model = cgs.models.precache[s1->modelindex3];
            trap_R_AddEntity(&ent);
        }

        if (s1->modelindex4) {
            ent.model = cgs.models.precache[s1->modelindex4];
            trap_R_AddEntity(&ent);
        }

        if (effects & EF_POWERSCREEN) {
            ent.model = cgs.models.powerscreen;
            ent.oldframe = 0;
            ent.frame = 0;
            ent.flags = RF_TRANSLUCENT;
            ent.alpha = ent_alpha * 0.30f;

            // remaster powerscreen is tiny and needs scaling
            if (cgs.need_powerscreen_scale) {
                vec3_t forward, tmp = ent.origin;
                ent.origin = Vec3_Add(ent.origin, Box3_Center(cent->box));
                AngleVectors(ent.angles, &forward, NULL, NULL);
                ent.origin = Vec3_MA(ent.origin, cent->box.maxs.x, forward);
                ent.scale = cent->radius * 0.8f;
                ent.flags |= RF_FULLBRIGHT;
                trap_R_AddEntity(&ent);
                ent.origin = tmp;
            } else {
                ent.flags |= RF_SHELL_GREEN;
                trap_R_AddEntity(&ent);
            }
        }

        if (s1->morefx & EFX_HOLOGRAM)
            CG_HologramParticles(ent.origin);

        // add automatic particle trails
        if (!(effects & EF_TRAIL_MASK))
            goto skip;

        if (effects & EF_ROCKET) {
            if (effects & EF_GIB) {
                CG_DiminishingTrail(cent, ent.origin, DT_FIREBALL);
                has_trail = true;
            } else if (!(cg_disable_particles.integer & NOPART_ROCKET_TRAIL)) {
                CG_DiminishingTrail(cent, ent.origin, DT_ROCKET);
                has_trail = true;
            }
            if (cg_dlight_hacks.integer & DLHACK_ROCKET_COLOR)
                CG_AddSphereLight(ent.origin, 200, 1, 0.23f, 0);
            else
                CG_AddSphereLight(ent.origin, 200, 1, 1, 0);
        } else if (effects & EF_BLASTER) {
            if (effects & EF_TRACKER) {
                CG_BlasterTrail2(cent, ent.origin);
                CG_AddSphereLight(ent.origin, 200, 0, 1, 0);
                has_trail = true;
            } else {
                if (!(cg_disable_particles.integer & NOPART_BLASTER_TRAIL)) {
                    CG_BlasterTrail(cent, ent.origin);
                    has_trail = true;
                }
                CG_AddSphereLight(ent.origin, 200, 1, 1, 0);
            }
        } else if (effects & EF_HYPERBLASTER) {
            if (effects & EF_TRACKER)
                CG_AddSphereLight(ent.origin, 200, 0, 1, 0);
            else
                CG_AddSphereLight(ent.origin, 200, 1, 1, 0);
        } else if (effects & EF_GIB) {
            CG_DiminishingTrail(cent, ent.origin, DT_GIB);
            has_trail = true;
        } else if (effects & EF_GRENADE) {
            if (!(cg_disable_particles.integer & NOPART_GRENADE_TRAIL)) {
                CG_DiminishingTrail(cent, ent.origin, DT_GRENADE);
                has_trail = true;
            }
        } else if (effects & EF_FLIES) {
            CG_FlyEffect(cent, ent.origin);
        } else if (effects & EF_BFG) {
            static const uint16_t bfg_lightramp[6] = {300, 400, 600, 300, 150, 75};
            if (effects & EF_ANIM_ALLFAST) {
                CG_BfgParticles(&ent);
                i = 200;
            } else {
                i = bfg_lightramp[ent.oldframe % 6] * ent.backlerp +
                    bfg_lightramp[ent.frame    % 6] * (1.0f - ent.backlerp);
            }
            CG_AddSphereLight(ent.origin, i, 0, 1, 0);
        } else if (effects & EF_TRAP) {
            ent.origin.z += 32;
            CG_TrapParticles(cent, ent.origin);
            i = (Com_SlowRand() % 100) + 100;
            CG_AddSphereLight(ent.origin, i, 1, 0.8f, 0.1f);
        } else if (effects & EF_FLAG1) {
            CG_FlagTrail(cent, ent.origin, 242);
            CG_AddSphereLight(ent.origin, 225, 1, 0.1f, 0.1f);
            has_trail = true;
        } else if (effects & EF_FLAG2) {
            CG_FlagTrail(cent, ent.origin, 115);
            CG_AddSphereLight(ent.origin, 225, 0.1f, 0.1f, 1);
            has_trail = true;
        } else if (effects & EF_TAGTRAIL) {
            CG_TagTrail(cent, ent.origin, 220);
            CG_AddSphereLight(ent.origin, 225, 1.0f, 1.0f, 0.0f);
            has_trail = true;
        } else if (effects & EF_TRACKERTRAIL) {
            if (effects & EF_TRACKER) {
                float intensity = 50 + (500 * (sinf(cg.time / 500.0f) + 1.0f));
                CG_AddSphereLight(ent.origin, intensity, -1.0f, -1.0f, -1.0f);
            } else {
                CG_Tracker_Shell(cent, ent.origin);
                CG_AddSphereLight(ent.origin, 155, -1.0f, -1.0f, -1.0f);
            }
        } else if (effects & EF_TRACKER) {
            CG_TrackerTrail(cent, ent.origin);
            CG_AddSphereLight(ent.origin, 200, -1, -1, -1);
            has_trail = true;
        } else if (effects & EF_GREENGIB) {
            CG_DiminishingTrail(cent, ent.origin, DT_GREENGIB);
            has_trail = true;
        } else if (effects & EF_IONRIPPER) {
            CG_IonripperTrail(cent, ent.origin);
            CG_AddSphereLight(ent.origin, 100, 1, 0.5f, 0.5f);
            has_trail = true;
        } else if (effects & EF_BLUEHYPERBLASTER) {
            CG_AddSphereLight(ent.origin, 200, 0, 0, 1);
        } else if (effects & EF_PLASMA) {
            if (effects & EF_ANIM_ALLFAST) {
                CG_BlasterTrail(cent, ent.origin);
                has_trail = true;
            }
            CG_AddSphereLight(ent.origin, 130, 1, 0.5f, 0.5f);
        }

skip:
        if (!has_trail)
            cent->lerp_origin = ent.origin;

        CG_SetEntitySoundOrigin(cent);

        CG_AddEntityLoopingSound(s1);
    }
}

/*
===============
CG_AddEntities

Emits all entities, particles, and lights to the refresh
===============
*/
void CG_AddEntities(void)
{
    CG_AddLightStyles();
    CG_AddPacketEntities();
    CG_AddTEnts();
    CG_AddParticles();
    CG_AddDLights();
}
