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
// cl_ents.c -- entity parsing and management

#include "client.h"

extern qhandle_t cl_mod_powerscreen;
extern qhandle_t cl_mod_laser;
extern qhandle_t cl_mod_dmspot;
extern qhandle_t cl_img_flare;

/*
=========================================================================

FRAME PARSING

=========================================================================
*/

// returns true if origin/angles update has been optimized out
static inline bool entity_is_optimized(const entity_state_t *state)
{
    return state->number == cl.frame.ps.clientnum && cl.frame.ps.pmove.pm_type < PM_DEAD;
}

static inline void
entity_update_new(centity_t *ent, const entity_state_t *state, const vec_t *origin)
{
    ent->trailcount = 1024;     // for diminishing rocket / grenade trails
    ent->flashlightfrac = 1.0f;

    // duplicate the current state so lerping doesn't hurt anything
    ent->prev = *state;
#if USE_FPS
    ent->prev_frame = state->frame;
    ent->event_frame = cl.frame.number;
#endif

    if (state->event[0] == EV_PLAYER_TELEPORT ||
        state->event[0] == EV_OTHER_TELEPORT ||
        (state->renderfx & RF_BEAM)) {
        // no lerping if teleported
        VectorCopy(origin, ent->lerp_origin);
        return;
    }

    // old_origin is valid for new entities,
    // so use it as starting point for interpolating between
    VectorCopy(state->old_origin, ent->prev.origin);
    VectorCopy(state->old_origin, ent->lerp_origin);
}

static inline void
entity_update_old(centity_t *ent, const entity_state_t *state, const vec_t *origin)
{
    int event = state->event[0];

#if USE_FPS
    // check for new event
    if (state->event != ent->current.event)
        ent->event_frame = cl.frame.number; // new
    else if (cl.frame.number - ent->event_frame >= cl.frametime.div)
        ent->event_frame = cl.frame.number; // refreshed
    else
        event = 0; // duplicated
#endif

    if (state->modelindex != ent->current.modelindex
        || state->modelindex2 != ent->current.modelindex2
        || state->modelindex3 != ent->current.modelindex3
        || state->modelindex4 != ent->current.modelindex4
        || event == EV_PLAYER_TELEPORT
        || event == EV_OTHER_TELEPORT
        || fabsf(origin[0] - ent->current.origin[0]) > 512
        || fabsf(origin[1] - ent->current.origin[1]) > 512
        || fabsf(origin[2] - ent->current.origin[2]) > 512
        || cl_nolerp->integer == 1) {
        // some data changes will force no lerping
        ent->trailcount = 1024;     // for diminishing rocket / grenade trails
        ent->flashlightfrac = 1.0f;

        // duplicate the current state so lerping doesn't hurt anything
        ent->prev = *state;
#if USE_FPS
        ent->prev_frame = state->frame;
#endif
        // no lerping if teleported or morphed
        VectorCopy(origin, ent->lerp_origin);
        return;
    }

#if USE_FPS
    // start alias model animation
    if (state->frame != ent->current.frame) {
        ent->prev_frame = ent->current.frame;
        ent->anim_start = cl.servertime - cl.frametime.time;
    }
#endif

    // shuffle the last state to previous
    ent->prev = ent->current;
}

static inline bool entity_is_new(const centity_t *ent)
{
    if (!cl.oldframe.valid)
        return true;    // last received frame was invalid

    if (ent->serverframe != cl.oldframe.number)
        return true;    // wasn't in last received frame

    if (cl_nolerp->integer == 2)
        return true;    // developer option, always new

    if (cl_nolerp->integer == 3)
        return false;   // developer option, lerp from last received frame

    if (cl.oldframe.number != cl.frame.number - 1)
        return true;    // previous server frame was dropped

    return false;
}

static void parse_entity_update(const entity_state_t *state)
{
    centity_t *ent = &cl_entities[state->number];
    const vec_t *origin;
    vec3_t origin_v;

    // if entity is solid, decode mins/maxs and add to the list
    if (state->solid && state->number != cl.frame.ps.clientnum
        && cl.numSolidEntities < MAX_PACKET_ENTITIES)
        cl.solidEntities[cl.numSolidEntities++] = ent;

    if (state->solid && state->solid != PACKED_BSP) {
        // encoded bbox
        MSG_UnpackSolid(state->solid, ent->mins, ent->maxs);
        ent->radius = Distance(ent->maxs, ent->mins) * 0.5f;
    } else {
        VectorClear(ent->mins);
        VectorClear(ent->maxs);
        ent->radius = 0;
    }

    // work around Q2PRO server bandwidth optimization
    if (entity_is_optimized(state)) {
        VectorCopy(cl.frame.ps.pmove.origin, origin_v);
        origin = origin_v;
    } else {
        origin = state->origin;
    }

    if (entity_is_new(ent)) {
        // wasn't in last update, so initialize some things
        entity_update_new(ent, state, origin);
    } else {
        entity_update_old(ent, state, origin);
    }

    ent->serverframe = cl.frame.number;
    ent->current = *state;

    // work around Q2PRO server bandwidth optimization
    if (entity_is_optimized(state)) {
        Com_PlayerToEntityState(&cl.frame.ps, &ent->current);
    }
}

static void set_active_state(void)
{
    cls.state = ca_active;

    cl.serverdelta = Q_align_down(cl.frame.number, CG_FRAMEDIV);
    cl.time = cl.servertime = 0; // set time, needed for demos
#if USE_FPS
    cl.keytime = cl.keyservertime = 0;
    cl.keyframe = cl.frame; // initialize keyframe to make sure it's valid
#endif

    // initialize oldframe so lerping doesn't hurt anything
    cl.oldframe.valid = false;
    cl.oldframe.ps = cl.frame.ps;
#if USE_FPS
    cl.oldkeyframe.valid = false;
    cl.oldkeyframe.ps = cl.keyframe.ps;
#endif

    cl.frameflags = 0;
    cl.initialSeq = cls.netchan.outgoing_sequence;

    if (cls.demo.playback) {
        // init some demo things
        CG_FirstDemoFrame();
    } else {
        // set initial cl.predicted_origin and cl.predicted_angles
        VectorCopy(cl.frame.ps.pmove.origin, cl.predicted_origin);
        VectorCopy(cl.frame.ps.pmove.velocity, cl.predicted_velocity);
        if (cl.frame.ps.pmove.pm_type < PM_DEAD) {
            // enhanced servers don't send viewangles
            CG_PredictAngles();
        } else {
            // just use what server provided
            VectorCopy(cl.frame.ps.viewangles, cl.predicted_angles);
        }
    }

    SCR_EndLoadingPlaque();     // get rid of loading plaque
    SCR_LagClear();
    Con_Close(false);           // get rid of connection screen

    CG_CheckForPause();

    CG_UpdateFrameTimes();

    IN_Activate();

    if (!cls.demo.playback) {
        EXEC_TRIGGER(cl_beginmapcmd);
        Cmd_ExecTrigger("#cl_enterlevel");
    }
}

static void
check_player_lerp(server_frame_t *oldframe, server_frame_t *frame, int framediv)
{
    player_state_t *ps, *ops;
    const centity_t *ent;
    int oldnum;

    // find states to interpolate between
    ps = &frame->ps;
    ops = &oldframe->ps;

    // no lerping if previous frame was dropped or invalid
    if (!oldframe->valid)
        goto dup;

    oldnum = frame->number - framediv;
    if (oldframe->number != oldnum)
        goto dup;

    // no lerping if player entity was teleported (origin check)
    if (abs(ops->pmove.origin[0] - ps->pmove.origin[0]) > 256 * 8 ||
        abs(ops->pmove.origin[1] - ps->pmove.origin[1]) > 256 * 8 ||
        abs(ops->pmove.origin[2] - ps->pmove.origin[2]) > 256 * 8) {
        goto dup;
    }

    // no lerping if player entity was teleported (event check)
    ent = &cl_entities[frame->ps.clientnum];
    if (ent->serverframe > oldnum &&
        ent->serverframe <= frame->number &&
#if USE_FPS
        ent->event_frame > oldnum &&
        ent->event_frame <= frame->number &&
#endif
        (ent->current.event[0] == EV_PLAYER_TELEPORT
         || ent->current.event[0] == EV_OTHER_TELEPORT)) {
        goto dup;
    }

    // no lerping if teleport bit was flipped
    if ((ops->rdflags ^ ps->rdflags) & RDF_TELEPORT_BIT)
        goto dup;

    // no lerping if POV number changed
    if (oldframe->ps.clientnum != frame->ps.clientnum)
        goto dup;

    // developer option
    if (cl_nolerp->integer == 1)
        goto dup;

    return;

dup:
    // duplicate the current state so lerping doesn't hurt anything
    *ops = *ps;
}

/*
==================
CG_DeltaFrame

A valid frame has been parsed.
==================
*/
void CG_DeltaFrame(void)
{
    centity_t           *ent;
    int                 i, j;
    int                 framenum;

    // getting a valid frame message ends the connection process
    if (cls.state == ca_precached)
        set_active_state();

    // set server time
    framenum = cl.frame.number - cl.serverdelta;

    if (framenum < 0)
        Com_Error(ERR_DROP, "%s: server time went backwards", __func__);

    if (framenum > INT_MAX / CG_FRAMETIME)
        Com_Error(ERR_DROP, "%s: server time overflowed", __func__);

    cl.servertime = framenum * CG_FRAMETIME;
#if USE_FPS
    cl.keyservertime = (framenum / cl.frametime.div) * BASE_FRAMETIME;
#endif

    // rebuild the list of solid entities for this frame
    cl.numSolidEntities = 0;

    // initialize position of the player's own entity from playerstate.
    // this is needed in situations when player entity is invisible, but
    // server sends an effect referencing it's origin (such as MZ_LOGIN, etc)
    ent = &cl_entities[cl.frame.ps.clientnum];
    Com_PlayerToEntityState(&cl.frame.ps, &ent->current);

    // set current and prev, unpack solid, etc
    for (i = 0; i < cl.frame.numEntities; i++) {
        j = (cl.frame.firstEntity + i) & PARSE_ENTITIES_MASK;
        parse_entity_update(&cl.entityStates[j]);
    }

    // fire events. due to footstep tracing this must be after updating entities.
    for (i = 0; i < cl.frame.numEntities; i++) {
        j = (cl.frame.firstEntity + i) & PARSE_ENTITIES_MASK;
        ent = &cl_entities[cl.entityStates[j].number];
        CG_EntityEvents(ent);
    }

    if (cls.demo.recording && !cls.demo.paused && !cls.demo.seeking && CG_FRAMESYNC) {
        CG_EmitDemoFrame();
    }

    if (cls.demo.playback) {
        // this delta has nothing to do with local viewangles,
        // clear it to avoid interfering with demo freelook hack
        VectorClear(cl.frame.ps.pmove.delta_angles);
    }

    if (cl.oldframe.ps.pmove.pm_type != cl.frame.ps.pmove.pm_type) {
        IN_Activate();
    }

    check_player_lerp(&cl.oldframe, &cl.frame, 1);

#if USE_FPS
    if (CG_FRAMESYNC)
        check_player_lerp(&cl.oldkeyframe, &cl.keyframe, cl.frametime.div);
#endif

    if (cl.frame.ps.stats[STAT_HITS] > cl.oldframe.ps.stats[STAT_HITS]) {
        extern cvar_t *cl_hit_markers;
        extern qhandle_t cl_sfx_hit_marker;

        if (cl_hit_markers->integer > 0) {
            cl.hit_marker_count = cl.frame.ps.stats[STAT_HITS] - cl.oldframe.ps.stats[STAT_HITS];
            cl.hit_marker_time = cls.realtime;
            if (cl_hit_markers->integer > 1)
                S_StartSound(NULL, listener_entnum, 257, cl_sfx_hit_marker, 1, ATTN_NONE, 0);
        }
    }

    CG_CheckPredictionError();

    SCR_SetCrosshairColor();
}

#if USE_DEBUG
// for debugging problems when out-of-date entity origin is referenced
void CG_CheckEntityPresent(int entnum, const char *what)
{
    const centity_t *e;

    if (entnum == cl.frame.ps.clientnum) {
        return; // player entity = current
    }

    e = &cl_entities[entnum];
    if (e->serverframe == cl.frame.number) {
        return; // current
    }

    if (e->serverframe) {
        Com_LPrintf(PRINT_DEVELOPER,
                    "SERVER BUG: %s on entity %d last seen %d frames ago\n",
                    what, entnum, cl.frame.number - e->serverframe);
    } else {
        Com_LPrintf(PRINT_DEVELOPER,
                    "SERVER BUG: %s on entity %d never seen before\n",
                    what, entnum);
    }
}
#endif


/*
==========================================================================

INTERPOLATE BETWEEN FRAMES TO GET RENDERING PARAMS

==========================================================================
*/

static float lerp_entity_alpha(const centity_t *ent)
{
    float prev = ent->prev.alpha;
    float curr = ent->current.alpha;

    // no lerping from/to default alpha
    if (prev && curr)
        return prev + cl.lerpfrac * (curr - prev);

    return curr ? curr : 1.0f;
}

static void CG_DrawBeam(const vec3_t start, const vec3_t end, qhandle_t model, int entnum)
{
    int         i, steps;
    vec3_t      dist, org, offset = { 0 };
    float       x, y, z, d;
    entity_t    ent;
    vec3_t      angles;
    float       len;
    int         framenum;
    float       model_length;
    float       hand_multiplier = 0;
    player_state_t  *ps, *ops;

    if (entnum < cl.maxclients) {
        if (model == cl_mod_heatbeam)
            VectorSet(offset, 2, 7, -3);
        else if (model == cl_mod_grapple_cable)
            VectorSet(offset, 9, 12, -3);
        else if (model == cl_mod_lightning)
            VectorSet(offset, 0, 12, -12);
    }

    // if coming from the player, update the start position
    if (entnum == cl.frame.ps.clientnum) {
        if (cl_gun->integer == 3)
            hand_multiplier = -1;
        else if (cl_gun->integer == 2)
            hand_multiplier = 1;
        else if (info_hand->integer == 2)
            hand_multiplier = 0;
        else if (info_hand->integer == 1)
            hand_multiplier = -1;
        else
            hand_multiplier = 1;

        // set up gun position
        ps = CG_KEYPS;
        ops = CG_OLDKEYPS;

        for (i = 0; i < 3; i++)
            org[i] = cl.refdef.vieworg[i] + ops->gunoffset[i] +
                CG_KEYLERPFRAC * (ps->gunoffset[i] - ops->gunoffset[i]);

        x = offset[0];
        y = offset[1];
        z = offset[2];

        // adjust offset for gun fov
        if (cl_gunfov->value > 0) {
            float fov_x = Cvar_ClampValue(cl_gunfov, 30, 160);
            float fov_y = V_CalcFov(fov_x, 4, 3);

            x *= tanf(cl.fov_x * (M_PIf / 360)) / tanf(fov_x * (M_PIf / 360));
            z *= tanf(cl.fov_y * (M_PIf / 360)) / tanf(fov_y * (M_PIf / 360));
        }

        VectorMA(org, hand_multiplier * x, cl.v_right, org);
        VectorMA(org, y, cl.v_forward, org);
        VectorMA(org, z, cl.v_up, org);
        if (hand_multiplier == 0)
            VectorMA(org, -1, cl.v_up, org);

        // calculate pitch and yaw
        VectorSubtract(end, org, dist);

        if (model != cl_mod_grapple_cable) {
            d = VectorLength(dist);
            VectorScale(cl.v_forward, d, dist);
        }

        // FIXME: use cl.refdef.viewangles?
        vectoangles2(dist, angles);

        // if it's the heatbeam, draw the particle effect
        if (model == cl_mod_heatbeam && !sv_paused->integer)
            CG_Heatbeam(org, dist);

        framenum = 1;
    } else {
        VectorCopy(start, org);

        // calculate pitch and yaw
        VectorSubtract(end, org, dist);
        vectoangles2(dist, angles);

        // if it's a player, use the hardcoded player offset
        if (entnum < cl.maxclients) {
            vec3_t  tmp, f, r, u;

            tmp[0] = -angles[0];
            tmp[1] = angles[1] + 180.0f;
            tmp[2] = 0;
            AngleVectors(tmp, f, r, u);

            VectorMA(org, -offset[0] + 1, r, org);
            VectorMA(org, -offset[1], f, org);
            VectorMA(org, -offset[2] - 10, u, org);
        } else if (model == cl_mod_heatbeam) {
            // if it's a monster, do the particle effect
            CG_MonsterPlasma_Shell(start);
        }

        framenum = 2;
    }

    // add new entities for the beams
    d = VectorNormalize(dist);
    if (model == cl_mod_heatbeam) {
        model_length = 32.0f;
    } else if (model == cl_mod_lightning) {
        model_length = 35.0f;
        d -= 20.0f; // correction so it doesn't end in middle of tesla
    } else {
        model_length = 30.0f;
    }

    // correction for grapple cable model, which has origin in the middle
    if (entnum == cl.frame.ps.clientnum && model == cl_mod_grapple_cable && hand_multiplier) {
        VectorMA(org, model_length * 0.5f, dist, org);
        d -= model_length * 0.5f;
    }

    steps = ceilf(d / model_length);

    memset(&ent, 0, sizeof(ent));
    ent.model = model;

    // PMM - special case for lightning model .. if the real length is shorter than the model,
    // flip it around & draw it from the end to the start.  This prevents the model from going
    // through the tesla mine (instead it goes through the target)
    if ((model == cl_mod_lightning) && (steps <= 1)) {
        VectorCopy(end, ent.origin);
        ent.flags = RF_FULLBRIGHT;
        ent.angles[0] = angles[0];
        ent.angles[1] = angles[1];
        ent.angles[2] = Com_SlowRand() % 360;
        trap_R_AddEntity(&ent);
        return;
    }

    if (steps > 1) {
        len = (d - model_length) / (steps - 1);
        VectorScale(dist, len, dist);
    }

    if (model == cl_mod_heatbeam) {
        ent.frame = framenum;
        ent.flags = RF_FULLBRIGHT;
        ent.angles[0] = -angles[0];
        ent.angles[1] = angles[1] + 180.0f;
        ent.angles[2] = cl.time % 360;
    } else if (model == cl_mod_lightning) {
        ent.flags = RF_FULLBRIGHT;
        ent.angles[0] = -angles[0];
        ent.angles[1] = angles[1] + 180.0f;
    } else {
        ent.flags = RF_NOSHADOW;
        ent.angles[0] = angles[0];
        ent.angles[1] = angles[1];
    }

    VectorCopy(org, ent.origin);
    for (i = 0; i < steps; i++) {
        if (model != cl_mod_heatbeam)
            ent.angles[2] = Com_SlowRand() % 360;
        trap_R_AddEntity(&ent);
        VectorAdd(ent.origin, dist, ent.origin);
    }
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
    float                   autorotate, autobob;
    int                     i;
    int                     pnum;
    centity_t               *cent;
    int                     autoanim;
    const clientinfo_t      *ci;
    unsigned int            effects, renderfx;
    bool                    has_alpha, has_trail;
    float                   custom_alpha;
    uint64_t                custom_flags;

    // bonus items rotate at a fixed rate
    autorotate = anglemod(cl.time * 0.1f);

    // brush models can auto animate their frames
    autoanim = cl.time / 500;

    autobob = 5 * sinf(cl.time / 400.0f);

    memset(&ent, 0, sizeof(ent));

    for (pnum = 0; pnum < cl.frame.numEntities; pnum++) {
        i = (cl.frame.firstEntity + pnum) & PARSE_ENTITIES_MASK;
        s1 = &cl.entityStates[i];

        cent = &cl_entities[s1->number];

        has_trail = false;

        effects = s1->effects;
        renderfx = s1->renderfx;

        // set frame
        if (effects & EF_ANIM01)
            ent.frame = autoanim & 1;
        else if (effects & EF_ANIM23)
            ent.frame = 2 + (autoanim & 1);
        else if (effects & EF_ANIM_ALL)
            ent.frame = autoanim;
        else if (effects & EF_ANIM_ALLFAST)
            ent.frame = cl.time / 100;
        else
            ent.frame = s1->frame;

        // quad and pent can do different things on client
        if (effects & EF_PENT) {
            effects &= ~EF_PENT;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_RED;
        }

        if (effects & EF_QUAD) {
            effects &= ~EF_QUAD;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_BLUE;
        }

        if (effects & EF_DOUBLE) {
            effects &= ~EF_DOUBLE;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_DOUBLE;
        }

        if (effects & EF_HALF_DAMAGE) {
            effects &= ~EF_HALF_DAMAGE;
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_HALF_DAM;
        }

        if (s1->morefx & EFX_DUALFIRE) {
            effects |= EF_COLOR_SHELL;
            renderfx |= RF_SHELL_LITE_GREEN;
        }

        // optionally remove the glowing effect
        if (cl_noglow->integer && !(renderfx & RF_BEAM))
            renderfx &= ~RF_GLOW;

        ent.oldframe = cent->prev.frame;
        ent.backlerp = 1.0f - cl.lerpfrac;

        if (renderfx & RF_BEAM) {
            // interpolate start and end points for beams
            LerpVector(cent->prev.origin, cent->current.origin,
                       cl.lerpfrac, ent.origin);
            LerpVector(cent->prev.old_origin, cent->current.old_origin,
                       cl.lerpfrac, ent.oldorigin);
        } else {
            if (s1->number == cl.frame.ps.clientnum) {
                // use predicted origin
                VectorCopy(cl.playerEntityOrigin, ent.origin);
                VectorCopy(cl.playerEntityOrigin, ent.oldorigin);
            } else {
                // interpolate origin
                LerpVector(cent->prev.origin, cent->current.origin,
                           cl.lerpfrac, ent.origin);
                VectorCopy(ent.origin, ent.oldorigin);
            }
#if USE_FPS
            // run alias model animation
            if (cent->prev_frame != s1->frame) {
                int delta = cl.time - cent->anim_start;
                float frac;

                if (delta > BASE_FRAMETIME) {
                    cent->prev_frame = s1->frame;
                    frac = 1;
                } else if (delta > 0) {
                    frac = delta * BASE_1_FRAMETIME;
                } else {
                    frac = 0;
                }

                ent.oldframe = cent->prev_frame;
                ent.backlerp = 1.0f - frac;
            }
#endif
        }

        if (effects & EF_BOB && !cl_nobob->integer) {
            ent.origin[2] += autobob;
            ent.oldorigin[2] += autobob;
        }

        if (!cl_gibs->integer) {
            if (effects & EF_GIB && !(effects & EF_ROCKET))
                goto skip;
            if (effects & EF_GREENGIB)
                goto skip;
        }

        if (s1->morefx & EFX_STEAM)
            goto skip;
        if ((renderfx & RF_BEAM_TEMP) == RF_BEAM_TEMP)
            goto skip;

        // create a new entity
        if (renderfx & RF_FLARE) {
            if (!cl_flares->integer)
                goto skip;
            float fade_start = s1->modelindex2;
            float fade_end = s1->modelindex3;
            float d = Distance(cl.refdef.vieworg, ent.origin);
            if (d < fade_start)
                goto skip;
            if (d > fade_end)
                ent.alpha = 1;
            else
                ent.alpha = (d - fade_start) / (fade_end - fade_start);
            ent.skin = 0;
            if (renderfx & RF_CUSTOMSKIN && (unsigned)s1->frame < MAX_IMAGES)
                ent.skin = cl.image_precache[s1->frame];
            if (!ent.skin)
                ent.skin = cl_img_flare;
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
            trap_R_AddLight(ent.origin, DLIGHT_CUTOFF + s1->frame,
                            color.u8[0] / 255.0f,
                            color.u8[1] / 255.0f,
                            color.u8[2] / 255.0f);
            goto skip;
        }

        if (renderfx & RF_BEAM && s1->modelindex > 1) {
            CG_DrawBeam(ent.oldorigin, ent.origin, cl.model_draw[s1->modelindex], s1->othernum);
            goto skip;
        }

        // tweak the color of beams
        if (renderfx & RF_BEAM) {
            // the four beam colors are encoded in 32 bits of skinnum (hack)
            ent.alpha = 0.30f;
            ent.skinnum = (s1->skinnum >> ((Com_SlowRand() % 4) * 8)) & 0xff;
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
                ci = &cl.clientinfo[s1->skinnum & 0xff];
                ent.skin = ci->skin;
                ent.model = ci->model;
                if (!ent.skin || !ent.model) {
                    ent.skin = cl.baseclientinfo.skin;
                    ent.model = cl.baseclientinfo.model;
                    ci = &cl.baseclientinfo;
                }
                if (renderfx & RF_USE_DISGUISE) {
                    char buffer[MAX_QPATH];

                    Q_concat(buffer, sizeof(buffer), "players/", ci->model_name, "/disguise.pcx");
                    ent.skin = R_RegisterSkin(buffer);
                }
            } else {
                ent.skinnum = s1->skinnum;
                ent.skin = 0;
                ent.model = cl.model_draw[s1->modelindex];
                if (ent.model == cl_mod_laser || ent.model == cl_mod_dmspot)
                    renderfx |= RF_NOSHADOW;
            }
        }

        // allow skin override for remaster
        if (renderfx & RF_CUSTOMSKIN && (unsigned)s1->skinnum < MAX_IMAGES) {
            ent.skin = cl.image_precache[s1->skinnum];
            ent.skinnum = 0;
        }

        // only used for black hole model right now, FIXME: do better
        if ((renderfx & RF_TRANSLUCENT) && !(renderfx & RF_BEAM))
            ent.alpha = 0.70f;

        // render effects (fullbright, translucent, etc)
        if (effects & EF_COLOR_SHELL)
            ent.flags = 0;  // renderfx go on color shell entity
        else
            ent.flags = renderfx;

        // calculate angles
        if (effects & EF_ROTATE) {  // some bonus items auto-rotate
            ent.angles[0] = 0;
            ent.angles[1] = autorotate;
            ent.angles[2] = 0;
        } else if (effects & EF_SPINNINGLIGHTS) {
            vec3_t forward;
            vec3_t start;

            ent.angles[0] = 0;
            ent.angles[1] = anglemod(cl.time / 2) + s1->angles[1];
            ent.angles[2] = 180;

            AngleVectors(ent.angles, forward, NULL, NULL);
            VectorMA(ent.origin, 64, forward, start);
            trap_R_AddLight(start, 100, 1, 0, 0);
        } else if (s1->number == cl.frame.ps.clientnum) {
            VectorCopy(cl.playerEntityAngles, ent.angles);      // use predicted angles
        } else { // interpolate angles
            LerpAngles(cent->prev.angles, cent->current.angles,
                       cl.lerpfrac, ent.angles);
            // mimic original ref_gl "leaning" bug (uuugly!)
            if (s1->solid != PACKED_BSP && s1->modelindex == MODELINDEX_PLAYER && cl_rollhack->integer)
                ent.angles[ROLL] = -ent.angles[ROLL];
        }

        if (s1->morefx & EFX_FLASHLIGHT) {
            vec3_t forward, start, end;
            trace_t trace;
            const int mask = CONTENTS_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER;

            if (s1->number == cl.frame.ps.clientnum) {
                VectorMA(cl.refdef.vieworg, 256, cl.v_forward, end);
                VectorCopy(cl.refdef.vieworg, start);
            } else {
                AngleVectors(ent.angles, forward, NULL, NULL);
                VectorMA(ent.origin, 256, forward, end);
                VectorCopy(ent.origin, start);
            }

            CG_Trace(&trace, start, vec3_origin, vec3_origin, end, ENTITYNUM_NONE, mask);
            LerpVector(start, end, cent->flashlightfrac, end);
            trap_R_AddLight(end, 256, 1, 1, 1);

            // smooth out distance "jumps"
            CG_AdvanceValue(&cent->flashlightfrac, trace.fraction, 1);
        }

        if (s1->morefx & EFX_GRENADE_LIGHT)
            trap_R_AddLight(ent.origin, 100, 1, 1, 0);

        if (s1->number == cl.frame.ps.clientnum && !cl.thirdPersonView) {
            if (effects & EF_FLAG1)
                trap_R_AddLight(ent.origin, 225, 1.0f, 0.1f, 0.1f);
            else if (effects & EF_FLAG2)
                trap_R_AddLight(ent.origin, 225, 0.1f, 0.1f, 1.0f);
            else if (effects & EF_TAGTRAIL)
                trap_R_AddLight(ent.origin, 225, 1.0f, 1.0f, 0.0f);
            else if (effects & EF_TRACKERTRAIL)
                trap_R_AddLight(ent.origin, 225, -1.0f, -1.0f, -1.0f);
            goto skip;
        }

        // if set to invisible, skip
        if (!s1->modelindex)
            goto skip;

        if (effects & EF_BFG) {
            ent.flags |= RF_TRANSLUCENT;
            ent.alpha = 0.30f;
        }

        if (effects & EF_PLASMA) {
            ent.flags |= RF_TRANSLUCENT;
            ent.alpha = 0.6f;
        }

        if (effects & EF_SPHERETRANS) {
            ent.flags |= RF_TRANSLUCENT;
            if (effects & EF_TRACKERTRAIL)
                ent.alpha = 0.6f;
            else
                ent.alpha = 0.3f;
        }

        // custom alpha overrides any derived value
        custom_alpha = 1.0f;
        custom_flags = 0;
        has_alpha = false;

        if (s1->alpha) {
            custom_alpha = lerp_entity_alpha(cent);
            has_alpha = true;
        }

        if (s1->number == cl.frame.ps.clientnum && cl.thirdPersonView && cl.thirdPersonAlpha != 1.0f) {
            custom_alpha *= cl.thirdPersonAlpha;
            has_alpha = true;
        }

        if (has_alpha) {
            ent.alpha = custom_alpha;
            if (custom_alpha == 1.0f)
                ent.flags &= ~RF_TRANSLUCENT;
            else
                ent.flags |= RF_TRANSLUCENT;
            custom_flags = ent.flags & RF_TRANSLUCENT;
        }

        // tracker effect is duplicated for linked models
        if (IS_TRACKER(effects)) {
            ent.flags    |= RF_TRACKER;
            custom_flags |= RF_TRACKER;
        }

        ent.scale = s1->scale;

        // add to refresh list
        trap_R_AddEntity(&ent);

        // color shells generate a separate entity for the main model
        if (effects & EF_COLOR_SHELL) {
            // PMM - at this point, all of the shells have been handled
            // if we're in the rogue pack, set up the custom mixing, otherwise just
            // keep going
            if (!strcmp(fs_game->string, "rogue")) {
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
            }
            ent.flags = renderfx | RF_TRANSLUCENT;
            ent.alpha = custom_alpha * 0.30f;
            trap_R_AddEntity(&ent);
        }

        ent.skin = 0;       // never use a custom skin on others
        ent.skinnum = 0;
        ent.flags = custom_flags;
        ent.alpha = custom_alpha;

        // duplicate for linked models
        if (s1->modelindex2) {
            if (s1->modelindex2 == MODELINDEX_PLAYER) {
                // custom weapon
                ci = &cl.clientinfo[s1->skinnum & 0xff];
                i = (s1->skinnum >> 8) & 0xff; // 0 is default weapon model
                if (i >= cl.numWeaponModels)
                    i = 0;
                ent.model = ci->weaponmodel[i];
                if (!ent.model) {
                    if (i != 0)
                        ent.model = ci->weaponmodel[0];
                    if (!ent.model)
                        ent.model = cl.baseclientinfo.weaponmodel[0];
                }
            } else
                ent.model = cl.model_draw[s1->modelindex2];

            // PMM - check for the defender sphere shell .. make it translucent
            if (!Q_strcasecmp(cl.configstrings[CS_MODELS + s1->modelindex2], "models/items/shell/tris.md2")) {
                ent.alpha = custom_alpha * 0.32f;
                ent.flags = RF_TRANSLUCENT;
            }

            trap_R_AddEntity(&ent);

            //PGM - make sure these get reset.
            ent.flags = custom_flags;
            ent.alpha = custom_alpha;
        }

        if (s1->modelindex3) {
            ent.model = cl.model_draw[s1->modelindex3];
            trap_R_AddEntity(&ent);
        }

        if (s1->modelindex4) {
            ent.model = cl.model_draw[s1->modelindex4];
            trap_R_AddEntity(&ent);
        }

        if (effects & EF_POWERSCREEN) {
            ent.model = cl_mod_powerscreen;
            ent.oldframe = 0;
            ent.frame = 0;
            ent.flags = RF_TRANSLUCENT;
            ent.alpha = custom_alpha * 0.30f;

            // remaster powerscreen is tiny and needs scaling
            if (cl.need_powerscreen_scale) {
                vec3_t forward, mid, tmp;
                VectorCopy(ent.origin, tmp);
                VectorAvg(cent->mins, cent->maxs, mid);
                VectorAdd(ent.origin, mid, ent.origin);
                AngleVectors(ent.angles, forward, NULL, NULL);
                VectorMA(ent.origin, cent->maxs[0], forward, ent.origin);
                ent.scale = cent->radius * 0.8f;
                ent.flags |= RF_FULLBRIGHT;
                trap_R_AddEntity(&ent);
                VectorCopy(tmp, ent.origin);
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
            } else if (!(cl_disable_particles->integer & NOPART_ROCKET_TRAIL)) {
                CG_DiminishingTrail(cent, ent.origin, DT_ROCKET);
                has_trail = true;
            }
            if (cl_dlight_hacks->integer & DLHACK_ROCKET_COLOR)
                trap_R_AddLight(ent.origin, 200, 1, 0.23f, 0);
            else
                trap_R_AddLight(ent.origin, 200, 1, 1, 0);
        } else if (effects & EF_BLASTER) {
            if (effects & EF_TRACKER) {
                CG_BlasterTrail2(cent, ent.origin);
                trap_R_AddLight(ent.origin, 200, 0, 1, 0);
                has_trail = true;
            } else {
                if (!(cl_disable_particles->integer & NOPART_BLASTER_TRAIL)) {
                    CG_BlasterTrail(cent, ent.origin);
                    has_trail = true;
                }
                trap_R_AddLight(ent.origin, 200, 1, 1, 0);
            }
        } else if (effects & EF_HYPERBLASTER) {
            if (effects & EF_TRACKER)
                trap_R_AddLight(ent.origin, 200, 0, 1, 0);
            else
                trap_R_AddLight(ent.origin, 200, 1, 1, 0);
        } else if (effects & EF_GIB) {
            CG_DiminishingTrail(cent, ent.origin, DT_GIB);
            has_trail = true;
        } else if (effects & EF_GRENADE) {
            if (!(cl_disable_particles->integer & NOPART_GRENADE_TRAIL)) {
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
            } else if (cl_smooth_explosions->integer) {
                i = bfg_lightramp[Q_clip(ent.oldframe, 0, 5)] * ent.backlerp +
                    bfg_lightramp[Q_clip(ent.frame,    0, 5)] * (1.0f - ent.backlerp);
            } else {
                i = bfg_lightramp[Q_clip(s1->frame, 0, 5)];
            }
            trap_R_AddLight(ent.origin, i, 0, 1, 0);
        } else if (effects & EF_TRAP) {
            ent.origin[2] += 32;
            CG_TrapParticles(cent, ent.origin);
            i = (Com_SlowRand() % 100) + 100;
            trap_R_AddLight(ent.origin, i, 1, 0.8f, 0.1f);
        } else if (effects & EF_FLAG1) {
            CG_FlagTrail(cent, ent.origin, 242);
            trap_R_AddLight(ent.origin, 225, 1, 0.1f, 0.1f);
            has_trail = true;
        } else if (effects & EF_FLAG2) {
            CG_FlagTrail(cent, ent.origin, 115);
            trap_R_AddLight(ent.origin, 225, 0.1f, 0.1f, 1);
            has_trail = true;
        } else if (effects & EF_TAGTRAIL) {
            CG_TagTrail(cent, ent.origin, 220);
            trap_R_AddLight(ent.origin, 225, 1.0f, 1.0f, 0.0f);
            has_trail = true;
        } else if (effects & EF_TRACKERTRAIL) {
            if (effects & EF_TRACKER) {
                float intensity = 50 + (500 * (sinf(cl.time / 500.0f) + 1.0f));
                trap_R_AddLight(ent.origin, intensity, -1.0f, -1.0f, -1.0f);
            } else {
                CG_Tracker_Shell(cent, ent.origin);
                trap_R_AddLight(ent.origin, 155, -1.0f, -1.0f, -1.0f);
            }
        } else if (effects & EF_TRACKER) {
            CG_TrackerTrail(cent, ent.origin);
            trap_R_AddLight(ent.origin, 200, -1, -1, -1);
            has_trail = true;
        } else if (effects & EF_GREENGIB) {
            CG_DiminishingTrail(cent, ent.origin, DT_GREENGIB);
            has_trail = true;
        } else if (effects & EF_IONRIPPER) {
            CG_IonripperTrail(cent, ent.origin);
            trap_R_AddLight(ent.origin, 100, 1, 0.5f, 0.5f);
            has_trail = true;
        } else if (effects & EF_BLUEHYPERBLASTER) {
            trap_R_AddLight(ent.origin, 200, 0, 0, 1);
        } else if (effects & EF_PLASMA) {
            if (effects & EF_ANIM_ALLFAST) {
                CG_BlasterTrail(cent, ent.origin);
                has_trail = true;
            }
            trap_R_AddLight(ent.origin, 130, 1, 0.5f, 0.5f);
        }

skip:
        if (!has_trail)
            VectorCopy(ent.origin, cent->lerp_origin);
    }
}

static const centity_t *get_player_entity(void)
{
    const centity_t *ent = &cl_entities[cl.frame.ps.clientnum];

    if (ent->serverframe != cl.frame.number)
        return NULL;
    if (!ent->current.modelindex)
        return NULL;

    return ent;
}

static int shell_effect_hack(const centity_t *ent)
{
    int flags = 0;

    if (ent->current.effects & EF_PENT)
        flags |= RF_SHELL_RED;
    if (ent->current.effects & EF_QUAD)
        flags |= RF_SHELL_BLUE;
    if (ent->current.effects & EF_DOUBLE)
        flags |= RF_SHELL_DOUBLE;
    if (ent->current.effects & EF_HALF_DAMAGE)
        flags |= RF_SHELL_HALF_DAM;
    if (ent->current.morefx & EFX_DUALFIRE)
        flags |= RF_SHELL_LITE_GREEN;
    if (ent->current.effects & EF_COLOR_SHELL)
        flags |= ent->current.renderfx & RF_SHELL_MASK;

    return flags;
}

/*
==============
CG_AddViewWeapon
==============
*/
static void CG_AddViewWeapon(void)
{
    const centity_t *ent;
    const player_state_t *ps, *ops;
    entity_t    gun;        // view model
    int         i, flags;

    // allow the gun to be completely removed
    if (cl_gun->integer < 1) {
        return;
    }

    if (cl_gun->integer == 1) {
        // don't draw gun if in wide angle view
        if (cls.demo.playback && cls.demo.compat && cl.frame.ps.fov > 90) {
            return;
        }
        // don't draw gun if center handed
        if (info_hand->integer == 2) {
            return;
        }
    }

    // find states to interpolate between
    ps = CG_KEYPS;
    ops = CG_OLDKEYPS;

    memset(&gun, 0, sizeof(gun));

    if (gun_model) {
        gun.model = gun_model;  // development tool
    } else {
        gun.model = cl.model_draw[ps->gunindex & GUNINDEX_MASK];
        gun.skinnum = ps->gunindex >> GUNINDEX_BITS;
    }
    if (!gun.model) {
        return;
    }

    // set up gun position
    for (i = 0; i < 3; i++) {
        gun.origin[i] = cl.refdef.vieworg[i] + ops->gunoffset[i] +
                        CG_KEYLERPFRAC * (ps->gunoffset[i] - ops->gunoffset[i]);
        gun.angles[i] = cl.refdef.viewangles[i] + LerpAngle(ops->gunangles[i],
                        ps->gunangles[i], CG_KEYLERPFRAC);
    }

    VectorMA(gun.origin, cl_gun_y->value, cl.v_forward, gun.origin);
    VectorMA(gun.origin, cl_gun_x->value, cl.v_right, gun.origin);
    VectorMA(gun.origin, cl_gun_z->value, cl.v_up, gun.origin);

    VectorCopy(gun.origin, gun.oldorigin);      // don't lerp at all

    if (gun_frame) {
        gun.frame = gun_frame;  // development tool
        gun.oldframe = gun_frame;   // development tool
    } else {
        gun.frame = ps->gunframe;
        if (gun.frame == 0) {
            gun.oldframe = 0;   // just changed weapons, don't lerp from old
        } else {
            gun.oldframe = ops->gunframe;
            gun.backlerp = 1.0f - CG_KEYLERPFRAC;
        }
    }

    gun.flags = RF_MINLIGHT | RF_DEPTHHACK | RF_WEAPONMODEL;
    gun.alpha = Cvar_ClampValue(cl_gunalpha, 0.1f, 1.0f);

    ent = get_player_entity();

    // add alpha from cvar or player entity
    if (ent && gun.alpha == 1.0f)
        gun.alpha = lerp_entity_alpha(ent);

    if (gun.alpha != 1.0f)
        gun.flags |= RF_TRANSLUCENT;

    trap_R_AddEntity(&gun);

    // add shell effect from player entity
    if (ent && (flags = shell_effect_hack(ent))) {
        gun.alpha *= 0.30f;
        gun.flags |= flags | RF_TRANSLUCENT;
        trap_R_AddEntity(&gun);
    }

    // add muzzle flash
    if (!cl.weapon.muzzle.model)
        return;

    if (cl.time - cl.weapon.muzzle.time > 50) {
        cl.weapon.muzzle.model = 0;
        return;
    }

    gun.flags = RF_FULLBRIGHT | RF_DEPTHHACK | RF_WEAPONMODEL | RF_TRANSLUCENT;
    gun.alpha = 1.0f;
    gun.model = cl.weapon.muzzle.model;
    gun.skinnum = 0;
    gun.scale = cl.weapon.muzzle.scale;
    gun.backlerp = 0.0f;
    gun.frame = gun.oldframe = 0;

    vec3_t forward, right, up;
    AngleVectors(gun.angles, forward, right, up);

    VectorMA(gun.origin, cl.weapon.muzzle.offset[0], forward, gun.origin);
    VectorMA(gun.origin, cl.weapon.muzzle.offset[1], right, gun.origin);
    VectorMA(gun.origin, cl.weapon.muzzle.offset[2], up, gun.origin);

    VectorCopy(cl.refdef.viewangles, gun.angles);
    gun.angles[2] += cl.weapon.muzzle.roll;

    trap_R_AddEntity(&gun);
}

static void CG_SetupFirstPersonView(void)
{
    // add kick angles
    if (cl_kickangles->integer) {
        vec3_t kickangles;
        LerpAngles(CG_OLDKEYPS->kick_angles, CG_KEYPS->kick_angles, CG_KEYLERPFRAC, kickangles);
        VectorAdd(cl.refdef.viewangles, kickangles, cl.refdef.viewangles);
    }

    // add the weapon
    CG_AddViewWeapon();

    cl.thirdPersonView = false;
}

// need to interpolate bmodel positions, or third person view would be very jerky
static void CG_LerpedTrace(trace_t *tr, const vec3_t start, const vec3_t end,
                           const vec3_t mins, const vec3_t maxs, int contentmask)
{
    trace_t trace;
    const centity_t *ent;
    vec3_t org, ang;

    // check against world
    trap_BoxTrace(tr, start, end, mins, maxs, MODELINDEX_WORLD, contentmask);
    tr->entnum = ENTITYNUM_WORLD;
    if (tr->fraction == 0)
        return;     // blocked by the world

    // check all other solid models
    for (int i = 0; i < cl.numSolidEntities; i++) {
        ent = cl.solidEntities[i];

        // special value for bmodel
        if (ent->current.solid != PACKED_BSP)
            continue;

        LerpVector(ent->prev.origin, ent->current.origin, cl.lerpfrac, org);
        LerpAngles(ent->prev.angles, ent->current.angles, cl.lerpfrac, ang);

        trap_TransformedBoxTrace(&trace, start, end, mins, maxs,
                                 ent->current.modelindex, contentmask, org, ang);

        CM_ClipEntity(tr, &trace, ent->current.number);
    }
}

/*
===============
CG_SetupThirdPersionView
===============
*/
static void CG_SetupThirdPersionView(void)
{
    static const vec3_t mins = { -4, -4, -4 };
    static const vec3_t maxs = {  4,  4,  4 };
    vec3_t focus;
    float fscale, rscale;
    float dist, angle, range;
    trace_t trace;

    // if dead, set a nice view angle
    if (cl.frame.ps.stats[STAT_HEALTH] <= 0) {
        cl.refdef.viewangles[ROLL] = 0;
        cl.refdef.viewangles[PITCH] = 10;
    }

    VectorMA(cl.refdef.vieworg, 512, cl.v_forward, focus);
    cl.refdef.vieworg[2] += 8;

    cl.refdef.viewangles[PITCH] *= 0.5f;
    AngleVectors(cl.refdef.viewangles, cl.v_forward, cl.v_right, cl.v_up);

    angle = DEG2RAD(cl_thirdperson_angle->value);
    range = cl_thirdperson_range->value;
    fscale = cosf(angle);
    rscale = sinf(angle);
    VectorMA(cl.refdef.vieworg, -range * fscale, cl.v_forward, cl.refdef.vieworg);
    VectorMA(cl.refdef.vieworg, -range * rscale, cl.v_right, cl.refdef.vieworg);

    CG_LerpedTrace(&trace, cl.playerEntityOrigin, cl.refdef.vieworg, mins, maxs, CONTENTS_SOLID);
    VectorCopy(trace.endpos, cl.refdef.vieworg);
    cl.thirdPersonAlpha = trace.fraction;

    VectorSubtract(focus, cl.refdef.vieworg, focus);
    dist = sqrtf(focus[0] * focus[0] + focus[1] * focus[1]);

    cl.refdef.viewangles[PITCH] = -RAD2DEG(atan2f(focus[2], dist));
    cl.refdef.viewangles[YAW] -= cl_thirdperson_angle->value;

    cl.thirdPersonView = true;
}

static void CG_FinishViewValues(void)
{
    if (cl_thirdperson->integer && get_player_entity())
        CG_SetupThirdPersionView();
    else
        CG_SetupFirstPersonView();
}

static inline float lerp_client_fov(float ofov, float nfov, float lerp)
{
    if (cls.demo.playback && !cls.demo.compat) {
        int fov = info_fov->integer;

        if (fov < 1)
            fov = 90;
        else if (fov > 160)
            fov = 160;

        if (info_uf->integer & UF_LOCALFOV)
            return fov;

        if (!(info_uf->integer & UF_PLAYERFOV)) {
            if (ofov >= 90)
                ofov = fov;
            if (nfov >= 90)
                nfov = fov;
        }
    }

    return ofov + lerp * (nfov - ofov);
}

static inline void lerp_values(const void *from, const void *to, float lerp, void *out, int count)
{
    float backlerp = 1.0f - lerp;

    for (int i = 0; i < count; i++)
        ((float *)out)[i] = ((const float *)from)[i] * backlerp + ((const float *)to)[i] * lerp;
}

/*
===============
CG_CalcViewValues

Sets cl.refdef view values and sound spatialization params.
Usually called from CG_AddEntities, but may be directly called from the main
loop if rendering is disabled but sound is running.
===============
*/
void CG_CalcViewValues(void)
{
    const player_state_t *ps, *ops;
    vec3_t viewoffset;
    float lerp;

    if (!cl.frame.valid) {
        return;
    }

    // find states to interpolate between
    ps = &cl.frame.ps;
    ops = &cl.oldframe.ps;

    lerp = cl.lerpfrac;

    // calculate the origin
    if (!cls.demo.playback && cl_predict->integer && !(ps->pmove.pm_flags & PMF_NO_PREDICTION)) {
        // use predicted values
        unsigned delta = cls.realtime - cl.predicted_step_time;
        float backlerp = lerp - 1.0f;

        VectorMA(cl.predicted_origin, backlerp, cl.prediction_error, cl.refdef.vieworg);

        // smooth out stair climbing
        if (delta < 100) {
            cl.refdef.vieworg[2] -= cl.predicted_step * (100 - delta) * 0.01f;
        }
    } else {
        // just use interpolated values
        for (int i = 0; i < 3; i++) {
            cl.refdef.vieworg[i] = ops->pmove.origin[i] +
                lerp * (ps->pmove.origin[i] - ops->pmove.origin[i]);
        }
    }

    // if not running a demo or on a locked frame, add the local angle movement
    if (cls.demo.playback) {
        if (cls.key_dest == KEY_GAME && trap_Key_IsDown(K_SHIFT)) {
            VectorCopy(cl.viewangles, cl.refdef.viewangles);
        } else {
            LerpAngles(ops->viewangles, ps->viewangles, lerp,
                       cl.refdef.viewangles);
        }
    } else if (ps->pmove.pm_type < PM_DEAD) {
        // use predicted values
        VectorCopy(cl.predicted_angles, cl.refdef.viewangles);
    } else if (ops->pmove.pm_type < PM_DEAD) {
        // lerp from predicted angles, since enhanced servers
        // do not send viewangles each frame
        LerpAngles(cl.predicted_angles, ps->viewangles, lerp, cl.refdef.viewangles);
    } else {
        // just use interpolated values
        LerpAngles(ops->viewangles, ps->viewangles, lerp, cl.refdef.viewangles);
    }

    // interpolate blend
    if (ops->screen_blend[3])
        lerp_values(ops->screen_blend, ps->screen_blend, lerp, cl.refdef.screen_blend, 4);
    else
        Vector4Copy(ps->screen_blend, cl.refdef.screen_blend);

    if (ops->damage_blend[3])
        lerp_values(ops->damage_blend, ps->damage_blend, lerp, cl.refdef.damage_blend, 4);
    else
        Vector4Copy(ps->damage_blend, cl.refdef.damage_blend);

    // interpolate fog
    lerp_values(&ops->fog, &ps->fog, lerp,
                &cl.refdef.fog, sizeof(cl.refdef.fog) / sizeof(float));
    // no lerping if moved too far
    if (fabsf(ps->heightfog.start.dist - ops->heightfog.start.dist) > 512 ||
        fabsf(ps->heightfog.end  .dist - ops->heightfog.end  .dist) > 512)
        cl.refdef.heightfog = ps->heightfog;
    else
        lerp_values(&ops->heightfog, &ps->heightfog, lerp,
                    &cl.refdef.heightfog, sizeof(cl.refdef.heightfog) / sizeof(float));

#if USE_FPS
    ps = &cl.keyframe.ps;
    ops = &cl.oldkeyframe.ps;

    lerp = cl.keylerpfrac;
#endif

    // interpolate field of view
    cl.fov_x = lerp_client_fov(ops->fov, ps->fov, lerp);
    cl.fov_y = V_CalcFov(cl.fov_x, 4, 3);

    LerpVector(ops->viewoffset, ps->viewoffset, lerp, viewoffset);

    AngleVectors(cl.refdef.viewangles, cl.v_forward, cl.v_right, cl.v_up);

    VectorCopy(cl.refdef.vieworg, cl.playerEntityOrigin);
    VectorCopy(cl.refdef.viewangles, cl.playerEntityAngles);

    if (cl.playerEntityAngles[PITCH] > 180) {
        cl.playerEntityAngles[PITCH] -= 360;
    }

    cl.playerEntityAngles[PITCH] = cl.playerEntityAngles[PITCH] / 3;

    VectorAdd(cl.refdef.vieworg, viewoffset, cl.refdef.vieworg);

    VectorCopy(cl.refdef.vieworg, listener_origin);
    VectorCopy(cl.v_forward, listener_forward);
    VectorCopy(cl.v_right, listener_right);
    VectorCopy(cl.v_up, listener_up);
}

/*
===============
CG_AddEntities

Emits all entities, particles, and lights to the refresh
===============
*/
void CG_AddEntities(void)
{
    CG_CalcViewValues();
    CG_FinishViewValues();
    CG_AddPacketEntities();
    CG_AddTEnts();
    CG_AddParticles();
    CG_AddDLights();
    CG_AddLightStyles();
    LOC_AddLocationsToScene();
}

/*
===============
CG_GetEntitySoundOrigin

Called to get the sound spatialization origin
===============
*/
void CG_GetEntitySoundOrigin(unsigned entnum, vec3_t org)
{
    const centity_t *ent;

    if (entnum >= ENTITYNUM_WORLD)
        Com_Error(ERR_DROP, "%s: bad entity", __func__);

    if (entnum == listener_entnum) {
        // should this ever happen?
        VectorCopy(listener_origin, org);
        return;
    }

    // interpolate origin
    // FIXME: what should be the sound origin point for RF_BEAM entities?
    ent = &cl_entities[entnum];
    LerpVector(ent->prev.origin, ent->current.origin, cl.lerpfrac, org);

    // offset the origin for BSP models
    if (ent->current.solid == PACKED_BSP) {
        vec3_t mins, maxs, mid;
        trap_GetBrushModelBounds(ent->current.modelindex, mins, maxs);
        VectorAvg(mins, maxs, mid);
        VectorAdd(org, mid, org);
    }
}
