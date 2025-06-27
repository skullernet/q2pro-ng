/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (c) ZeniMax Media Inc.

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
// cg_view.c -- player rendering positioning

#include "cg_local.h"

static const centity_t *get_player_entity(void)
{
    const centity_t *ent = &cg_entities[cg.frame->ps.clientnum];

    if (ent->serverframe != cg.frame->number)
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
    if (cg_gun.integer < 1)
        return;

    // don't draw gun if center handed
    if (cg_gun.integer == 1 && info_hand.integer == 2)
        return;

    // find states to interpolate between
    ps = &cg.frame->ps;
    ops = &cg.oldframe->ps;

    memset(&gun, 0, sizeof(gun));

    gun.model = cg.model_draw[ps->gunindex];
    gun.skinnum = ps->gunskin;
    if (!gun.model)
        return;

    // set up gun position
    VectorCopy(cg.refdef.vieworg, gun.origin);
    VectorCopy(cg.refdef.viewangles, gun.angles);

    if (!(cg.frame->ps.rdflags & RDF_NO_WEAPON_BOB) && !cg_skip_view_modifiers.integer) {
        // gun angles from bobbing
        gun.angles[ROLL] += cg.xyspeed * cg.bobfracsin * 0.005f;
        gun.angles[YAW] += cg.xyspeed * cg.bobfracsin * 0.01f;
        gun.angles[PITCH] += cg.xyspeed * fabsf(cg.bobfracsin) * 0.005f;

        VectorAdd(cg.slow_view_angles, cg.viewangles_delta, cg.slow_view_angles);

        // gun angles from delta movement
        for (i = 0; i < 3; i++) {
            float d = cg.slow_view_angles[i];

            if (!d)
                continue;

            if (d > 180)
                d -= 360;
            if (d < -180)
                d += 360;

            d = Q_clipf(d, -45, 45);

            // [Sam-KEX] Apply only half-delta. Makes the weapons look less detached from the player.
            if (i == ROLL)
                gun.angles[i] += (0.1f * d) * 0.5f;
            else
                gun.angles[i] += (0.2f * d) * 0.5f;

            float reduction_factor = cg.viewangles_delta[i] ? 50 : 150;

            if (d > 0)
                d = max(0, d - cgs.frametime * reduction_factor);
            else if (d < 0)
                d = min(0, d + cgs.frametime * reduction_factor);

            cg.slow_view_angles[i] = d;
        }
    }

    VectorMA(gun.origin, cg_gun_y.value, cg.v_forward, gun.origin);
    VectorMA(gun.origin, cg_gun_x.value, cg.v_right, gun.origin);
    VectorMA(gun.origin, cg_gun_z.value, cg.v_up, gun.origin);

    VectorCopy(gun.origin, gun.oldorigin);      // don't lerp at all

    gun.frame = ps->gunframe;
    if (gun.frame == 0) {
        gun.oldframe = 0;   // just changed weapons, don't lerp from old
    } else {
        gun.oldframe = ops->gunframe;
        gun.backlerp = 1.0f - cg.lerpfrac;
    }

    gun.flags = RF_MINLIGHT | RF_DEPTHHACK | RF_WEAPONMODEL;
    gun.alpha = Q_clipf(cg_gunalpha.value, 0.1f, 1.0f);

    ent = get_player_entity();

    // add alpha from cvar or player entity
    if (ent && gun.alpha == 1.0f)
        gun.alpha = CG_LerpEntityAlpha(ent);

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
    if (!cg.weapon.muzzle.model)
        return;

    if (cg.time - cg.weapon.muzzle.time > 50) {
        cg.weapon.muzzle.model = 0;
        return;
    }

    gun.flags = RF_FULLBRIGHT | RF_DEPTHHACK | RF_WEAPONMODEL | RF_TRANSLUCENT;
    gun.alpha = 1.0f;
    gun.model = cg.weapon.muzzle.model;
    gun.skinnum = 0;
    gun.scale = cg.weapon.muzzle.scale;
    gun.backlerp = 0.0f;
    gun.frame = gun.oldframe = 0;

    vec3_t forward, right, up;
    AngleVectors(gun.angles, forward, right, up);

    VectorMA(gun.origin, cg.weapon.muzzle.offset[0], forward, gun.origin);
    VectorMA(gun.origin, cg.weapon.muzzle.offset[1], right, gun.origin);
    VectorMA(gun.origin, cg.weapon.muzzle.offset[2], up, gun.origin);

    VectorCopy(cg.refdef.viewangles, gun.angles);
    gun.angles[2] += cg.weapon.muzzle.roll;

    trap_R_AddEntity(&gun);
}

static void CG_RunViewKicks(int frame)
{
    vec3_t angles, origin;
    float ratio;

    // add angles based on weapon kick
    VectorCopy(cg.weapon.kick.angles, angles);
    VectorClear(cg.weapon.kick.angles);

    // add offset based on weapon kick
    VectorCopy(cg.weapon.kick.origin, origin);
    VectorClear(cg.weapon.kick.origin);

    // add angles based on damage kick
    if (cg.v_dmg_time > cg.time) {
        ratio = (float)(cg.v_dmg_time - cg.time) / DAMAGE_TIME;
        angles[PITCH] += ratio * cg.v_dmg_pitch;
        angles[ROLL] += ratio * cg.v_dmg_roll;
    }

    // add pitch based on fall kick
    if (cg.fall_time > cg.time) {
        ratio = (float)(cg.fall_time - cg.time) / FALL_TIME;
        angles[PITCH] += ratio * cg.fall_value;

        // add fall height
        origin[2] -= ratio * cg.fall_value * 0.4f;
    }

    VectorCopy(angles, cg.kick_angles[frame]);
    VectorCopy(origin, cg.kick_origin[frame]);
}

static void CG_CalcViewOffset(void)
{
    if (cg.frame->ps.pm_type != PM_NORMAL)
        return;
    if (cg_skip_view_modifiers.integer)
        return;

    // run kick angles at 10 Hz
    if (true) {
        int ofs = (cg.time / 100) & 1;
        float f = (cg.time % 100) * 0.01f;

        if (cg.kick_frame != ofs) {
            CG_RunViewKicks(ofs ^ 1);
            cg.kick_frame = ofs;
        }

        vec3_t kick;
        LerpVector(cg.kick_angles[ofs], cg.kick_angles[ofs ^ 1], f, kick);
        VectorAdd(cg.refdef.viewangles, kick, cg.refdef.viewangles);

        LerpVector(cg.kick_origin[ofs], cg.kick_origin[ofs ^ 1], f, kick);
        VectorAdd(cg.refdef.vieworg, kick, cg.refdef.vieworg);
    }

    // run earthquake angles at 40 Hz
    if (cg.quake_time > cg.time) {
        int ofs = (cg.time / 25) & 1;
        float f = (cg.time % 25) * 0.04f;

        if (cg.quake_frame != ofs) {
            VectorSet(cg.quake_angles[ofs ^ 1], crand(), crand(), crand());
            cg.quake_frame = ofs;
        }

        vec3_t kick;
        LerpVector(cg.quake_angles[ofs], cg.quake_angles[ofs ^ 1], f, kick);

        f = cg.quake_time * 0.25f / cg.time;
        VectorMA(cg.refdef.viewangles, f, kick, cg.refdef.viewangles);
    }

#define IS_CROUCHING(frame) \
    (((frame)->ps.pm_flags & (PMF_DUCKED | PMF_ON_GROUND)) == (PMF_DUCKED | PMF_ON_GROUND))

    float *angles = cg.refdef.viewangles;
    float crouch1 = IS_CROUCHING(cg.oldframe) ? 6 : 1;
    float crouch2 = IS_CROUCHING(cg.frame)    ? 6 : 1;
    float crouch_factor = crouch1 + cg.lerpfrac * (crouch2 - crouch1);
    float delta;

    // add angles based on velocity
    delta = DotProduct(cg.predicted_ps.velocity, cg.v_forward);
    angles[PITCH] += delta * cg_run_pitch.value;

    delta = DotProduct(cg.predicted_ps.velocity, cg.v_right);
    angles[ROLL] += delta * cg_run_roll.value;

    // add angles based on bob
    delta = fabsf(cg.bobfracsin) * cg_bob_pitch.value * cg.xyspeed * crouch_factor;
    angles[PITCH] += delta;

    delta = cg.bobfracsin * cg_bob_roll.value * cg.xyspeed * crouch_factor;
    angles[ROLL] += delta;

    // add bob height
    float bob = fabsf(cg.bobfracsin) * cg.xyspeed * cg_bob_up.value;
    if (bob > 6)
        bob = 6;
    cg.refdef.vieworg[2] += bob;
}

static void CG_SetupFirstPersonView(void)
{
    int delta = cg.frame->ps.bobtime - cg.oldframe->ps.bobtime;
    if (delta < -128)
        delta += 256;
    float bobtime = cg.oldframe->ps.bobtime + cg.lerpfrac * delta;

    cg.bobfracsin = sinf(bobtime * (M_PIf / 128));
    cg.xyspeed = Vector2Length(cg.predicted_ps.velocity);

    CG_CalcViewOffset();

    // add the weapon
    CG_AddViewWeapon();

    cg.thirdPersonView = false;
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
    for (int i = 0; i < cg.numSolidEntities; i++) {
        ent = cg.solidEntities[i];

        // special value for bmodel
        if (ent->current.solid != PACKED_BSP)
            continue;

        LerpVector(ent->prev.origin, ent->current.origin, cg.lerpfrac, org);
        LerpAngles(ent->prev.angles, ent->current.angles, cg.lerpfrac, ang);

        trap_TransformedBoxTrace(&trace, start, end, mins, maxs,
                                 ent->current.modelindex, contentmask, org, ang);

        trap_ClipEntity(tr, &trace, ent->current.number);
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
    if (cg.frame->ps.stats[STAT_HEALTH] <= 0) {
        cg.refdef.viewangles[ROLL] = 0;
        cg.refdef.viewangles[PITCH] = 10;
    }

    VectorMA(cg.refdef.vieworg, 512, cg.v_forward, focus);
    cg.refdef.vieworg[2] += 8;

    cg.refdef.viewangles[PITCH] *= 0.5f;
    AngleVectors(cg.refdef.viewangles, cg.v_forward, cg.v_right, cg.v_up);

    angle = DEG2RAD(cg_thirdperson_angle.value);
    range = cg_thirdperson_range.value;
    fscale = cosf(angle);
    rscale = sinf(angle);
    VectorMA(cg.refdef.vieworg, -range * fscale, cg.v_forward, cg.refdef.vieworg);
    VectorMA(cg.refdef.vieworg, -range * rscale, cg.v_right, cg.refdef.vieworg);

    CG_LerpedTrace(&trace, cg.playerEntityOrigin, cg.refdef.vieworg, mins, maxs, CONTENTS_SOLID);
    VectorCopy(trace.endpos, cg.refdef.vieworg);
    cg.thirdPersonAlpha = trace.fraction;

    VectorSubtract(focus, cg.refdef.vieworg, focus);
    dist = sqrtf(focus[0] * focus[0] + focus[1] * focus[1]);

    cg.refdef.viewangles[PITCH] = -RAD2DEG(atan2f(focus[2], dist));
    cg.refdef.viewangles[YAW] -= cg_thirdperson_angle.value;

    cg.thirdPersonView = true;
}

static void CG_FinishViewValues(void)
{
    if (cg_thirdperson.integer && get_player_entity())
        CG_SetupThirdPersionView();
    else
        CG_SetupFirstPersonView();
}

static inline float lerp_client_fov(float ofov, float nfov, float lerp)
{
    if (cgs.demoplayback) {
        int fov = info_fov.integer;

        if (fov < 1)
            fov = 90;
        else if (fov > 160)
            fov = 160;

        if (info_uf.integer & UF_LOCALFOV)
            return fov;

        if (!(info_uf.integer & UF_PLAYERFOV)) {
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

static void CG_ScreenEffects(void)
{
    // add for contents
    contents_t contents = CG_PointContents(cg.refdef.vieworg);

    if (contents & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER))
        cg.refdef.rdflags |= RDF_UNDERWATER;
    else
        cg.refdef.rdflags &= ~RDF_UNDERWATER;

    if (contents & (CONTENTS_SOLID | CONTENTS_LAVA))
        G_AddBlend(1.0f, 0.3f, 0.0f, 0.6f, cg.refdef.screen_blend);
    else if (contents & CONTENTS_SLIME)
        G_AddBlend(0.0f, 0.1f, 0.05f, 0.6f, cg.refdef.screen_blend);
    else if (contents & CONTENTS_WATER)
        G_AddBlend(0.5f, 0.3f, 0.2f, 0.4f, cg.refdef.screen_blend);
}

/*
===============
CG_CalcViewValues

Sets cg.refdef view values and sound spatialization params.
Usually called from CG_AddEntities, but may be directly called from the main
loop if rendering is disabled but sound is running.
===============
*/
static void CG_CalcViewValues(void)
{
    const player_state_t *ps, *ops;
    float lerp;

    // find states to interpolate between
    ps = &cg.frame->ps;
    ops = &cg.oldframe->ps;

    lerp = cg.lerpfrac;

    // calculate the origin
    if (!cgs.demoplayback && cg_predict.integer && !(ps->pm_flags & PMF_NO_PREDICTION)) {
        // use predicted values
        unsigned delta = cgs.realtime - cg.predicted_step_time;
        float backlerp = lerp - 1.0f;

        VectorMA(cg.predicted_ps.origin, backlerp, cg.prediction_error, cg.refdef.vieworg);

        // smooth out stair climbing
        if (delta < 100) {
            cg.refdef.vieworg[2] -= cg.predicted_step * (100 - delta) * 0.01f;
        }
    } else {
        // just use interpolated values
        for (int i = 0; i < 3; i++) {
            cg.refdef.vieworg[i] = ops->origin[i] + lerp * (ps->origin[i] - ops->origin[i]);
        }
    }

    // if not running a demo or on a locked frame, add the local angle movement
    if (cgs.demoplayback) {
        if (trap_Key_GetDest() == KEY_NONE && trap_Key_IsDown(K_SHIFT)) {
            CG_PredictAngles();
            VectorCopy(cg.predicted_ps.viewangles, cg.refdef.viewangles);
        } else {
            LerpAngles(ops->viewangles, ps->viewangles, lerp, cg.refdef.viewangles);
        }
    } else if (ps->pm_type < PM_DEAD) {
        // use predicted values
        VectorCopy(cg.predicted_ps.viewangles, cg.refdef.viewangles);
    } else if (ops->pm_type < PM_DEAD) {
        // lerp from predicted angles, since enhanced servers
        // do not send viewangles each frame
        LerpAngles(cg.predicted_ps.viewangles, ps->viewangles, lerp, cg.refdef.viewangles);
    } else {
        // just use interpolated values
        LerpAngles(ops->viewangles, ps->viewangles, lerp, cg.refdef.viewangles);
    }

    VectorSubtract(cg.oldviewangles, cg.refdef.viewangles, cg.viewangles_delta);
    VectorCopy(cg.refdef.viewangles, cg.oldviewangles);

    // interpolate blend
    if (ops->screen_blend[3])
        lerp_values(ops->screen_blend, ps->screen_blend, lerp, cg.refdef.screen_blend, 4);
    else
        Vector4Copy(ps->screen_blend, cg.refdef.screen_blend);

    if (ops->damage_blend[3])
        lerp_values(ops->damage_blend, ps->damage_blend, lerp, cg.refdef.damage_blend, 4);
    else
        Vector4Copy(ps->damage_blend, cg.refdef.damage_blend);

    // interpolate fog
    lerp_values(&ops->fog, &ps->fog, lerp,
                &cg.refdef.fog, sizeof(cg.refdef.fog) / sizeof(float));
    // no lerping if moved too far
    if (fabsf(ps->heightfog.start.dist - ops->heightfog.start.dist) > 512 ||
        fabsf(ps->heightfog.end  .dist - ops->heightfog.end  .dist) > 512)
        cg.refdef.heightfog = ps->heightfog;
    else
        lerp_values(&ops->heightfog, &ps->heightfog, lerp,
                    &cg.refdef.heightfog, sizeof(cg.refdef.heightfog) / sizeof(float));

    // interpolate field of view
    cg.fov_x = lerp_client_fov(ops->fov, ps->fov, lerp);
    cg.fov_y = V_CalcFov(cg.fov_x, 4, 3);

    AngleVectors(cg.refdef.viewangles, cg.v_forward, cg.v_right, cg.v_up);

    VectorCopy(cg.refdef.vieworg, cg.playerEntityOrigin);
    VectorCopy(cg.refdef.viewangles, cg.playerEntityAngles);

    if (cg.playerEntityAngles[PITCH] > 180) {
        cg.playerEntityAngles[PITCH] -= 360;
    }

    cg.playerEntityAngles[PITCH] = cg.playerEntityAngles[PITCH] / 3;

    // add view height
    cg.refdef.vieworg[2] += cg.predicted_ps.viewheight;
    if (cg.duck_time > cg.time)
        cg.refdef.vieworg[2] -= (cg.duck_time - cg.time) * cg.duck_factor;

    cg.refdef.rdflags = ps->rdflags;

    CG_ScreenEffects();

    vec3_t axis[3];
    AnglesToAxis(cg.refdef.viewangles, axis);
    trap_S_UpdateListener(cg.frame->ps.clientnum, cg.refdef.vieworg, axis, cg.refdef.rdflags & RDF_UNDERWATER);
}

/*
==================
CG_RenderView

==================
*/
void CG_RenderView(void)
{
    CG_ProcessFrames();

    if (!cg.frame)
        return;

    CG_PredictMovement();

    CG_CalcViewValues();

    CG_FinishViewValues();

    // build a refresh entity list
    CG_AddEntities();

    // never let it sit exactly on a node line, because a water plane can
    // disappear when viewed with the eye exactly on it.
    // the server protocol only specifies to 1/8 pixel, so add 1/16 in each axis
    cg.refdef.vieworg[0] += 1.0f / 16;
    cg.refdef.vieworg[1] += 1.0f / 16;
    cg.refdef.vieworg[2] += 1.0f / 16;

    cg.refdef.x = scr_vrect.x;
    cg.refdef.y = scr_vrect.y;
    cg.refdef.width = scr_vrect.width;
    cg.refdef.height = scr_vrect.height;

    // adjust for non-4/3 screens
    if (cg_adjustfov.integer) {
        cg.refdef.fov_y = cg.fov_y;
        cg.refdef.fov_x = V_CalcFov(cg.refdef.fov_y, cg.refdef.height, cg.refdef.width);
    } else {
        cg.refdef.fov_x = cg.fov_x;
        cg.refdef.fov_y = V_CalcFov(cg.refdef.fov_x, cg.refdef.width, cg.refdef.height);
    }

    cg.refdef.frametime = cgs.frametime;
    cg.refdef.time = cg.time * 0.001f;
    memcpy(cg.refdef.areabits, cg.frame->areabits, sizeof(cg.refdef.areabits));

    if (cg.custom_fog.density) {
        cg.refdef.fog = cg.custom_fog;
        cg.refdef.heightfog = (player_heightfog_t){ 0 };
    }

    trap_R_RenderScene(&cg.refdef);
}
