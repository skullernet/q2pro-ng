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

static const centity_t *CG_GetPlayerEntity(void)
{
    const centity_t *ent = &cg_entities[cg.frame->ps.clientnum];

    if (ent->serverframe != cg.frame->number)
        return NULL;
    if (!ent->current.modelindex)
        return NULL;

    return ent;
}

/*
==============
CG_AddViewWeapon
==============
*/
static void CG_AddViewWeapon(void)
{
    const centity_t *ent;
    const player_state_t *ps;
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

    memset(&gun, 0, sizeof(gun));

    gun.model = cgs.models.precache[ps->gunindex];
    gun.skinnum = ps->gunskin;
    if (!gun.model)
        return;

    // set up gun position
    gun.origin = cg.refdef.vieworg;
    gun.angles = cg.refdef.viewangles;

    if (!(ps->rdflags & RDF_NO_WEAPON_BOB) && !cg_skip_view_modifiers.integer) {
        // gun angles from bobbing
        gun.angles.roll += cg.xyspeed * cg.bobfracsin * 0.005f;
        gun.angles.yaw += cg.xyspeed * cg.bobfracsin * 0.01f;
        gun.angles.pitch += cg.xyspeed * fabsf(cg.bobfracsin) * 0.005f;

        cg.slow_view_angles = Vec3_Add(cg.slow_view_angles, cg.viewangles_delta);

        // gun angles from delta movement
        for (i = 0; i < 3; i++) {
            float d = cg.slow_view_angles.xyz[i];

            if (!d)
                continue;

            if (d > 180)
                d -= 360;
            if (d < -180)
                d += 360;

            d = Q_clipf(d, -45, 45);

            // [Sam-KEX] Apply only half-delta. Makes the weapons look less detached from the player.
            if (i == ROLL)
                gun.angles.xyz[i] += (0.1f * d) * 0.5f;
            else
                gun.angles.xyz[i] += (0.2f * d) * 0.5f;

            float reduction_factor = cg.viewangles_delta.xyz[i] ? 50 : 150;

            if (d > 0)
                d = max(0, d - cgs.frametime * reduction_factor);
            else if (d < 0)
                d = min(0, d + cgs.frametime * reduction_factor);

            cg.slow_view_angles.xyz[i] = d;
        }
    }

    gun.origin = Vec3_MA(gun.origin, cg_gun_y.value, cg.v_forward);
    gun.origin = Vec3_MA(gun.origin, cg_gun_x.value, cg.v_right);
    gun.origin = Vec3_MA(gun.origin, cg_gun_z.value, cg.v_up);

    gun.frame = ps->gunframe;
    if (gun.frame == 0) {
        gun.oldframe = 0;   // just changed weapons, don't lerp from old
    } else {
        // run alias model animation
        if (cg.weapon.prev_frame != ps->gunframe) {
            int delta = cg.time - cg.weapon.anim_start;
            int frametime = BASE_FRAMETIME >> ps->gunrate;
            float frac;

            if (delta > frametime) {
                cg.weapon.prev_frame = ps->gunframe;
                frac = 1;
            } else if (delta > 0) {
                frac = (float)delta / frametime;
            } else {
                frac = 0;
            }

            gun.oldframe = cg.weapon.prev_frame;
            gun.backlerp = 1.0f - frac;
        } else {
            gun.oldframe = ps->gunframe;
        }
    }

    gun.flags = RF_MINLIGHT | RF_DEPTHHACK | RF_WEAPONMODEL;
    gun.alpha = Q_clipf(cg_gunalpha.value, 0.1f, 1.0f);

    ent = CG_GetPlayerEntity();

    // add alpha from cvar or player entity
    if (ent && gun.alpha == 1.0f)
        gun.alpha = CG_LerpEntityAlpha(ent);

    if (gun.alpha != 1.0f)
        gun.flags |= RF_TRANSLUCENT;

    if (cg_gunfov.value > 0) {
        float fov_x, fov_y;

        fov_x = Q_clipf(cg_gunfov.value, 30, 160);
        if (cg_adjustfov.integer) {
            fov_y = V_CalcFov(fov_x, 4, 3);
            fov_x = V_CalcFov(fov_y, scr_vrect.height, scr_vrect.width);
        } else {
            fov_y = V_CalcFov(fov_x, scr_vrect.width, scr_vrect.height);
        }

        gun.oldorigin.x = fov_x;
        gun.oldorigin.y = fov_y;
        gun.flags |= RF_FOVHACK;
    }

    if ((info_hand.integer == 1 && cg_gun.integer == 1) || cg_gun.integer == 3)
        gun.flags |= RF_LEFTHAND;

    trap_R_AddEntity(&gun);

    // add shell effect from player entity
    if (ent && (flags = CG_EntityShellEffect(&ent->current))) {
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

    gun.flags &= RF_FOVHACK | RF_LEFTHAND;
    gun.flags |= RF_FULLBRIGHT | RF_DEPTHHACK | RF_WEAPONMODEL | RF_TRANSLUCENT;
    gun.alpha = 1.0f;
    gun.model = cg.weapon.muzzle.model;
    gun.skinnum = 0;
    gun.scale = cg.weapon.muzzle.scale;
    gun.backlerp = 0.0f;
    gun.frame = gun.oldframe = 0;

    vec3_t forward, right, up;
    AngleVectors(gun.angles, &forward, &right, &up);

    gun.origin = Vec3_MA(gun.origin, cg.weapon.muzzle.offset.forward, forward);
    gun.origin = Vec3_MA(gun.origin, cg.weapon.muzzle.offset.right, right);
    gun.origin = Vec3_MA(gun.origin, cg.weapon.muzzle.offset.up, up);

    gun.angles = cg.refdef.viewangles;
    gun.angles.roll += cg.weapon.muzzle.roll;

    trap_R_AddEntity(&gun);
}

static void CG_RunViewKicks(int frame)
{
    vec3_t angles, origin;
    float ratio;

    // add angles based on weapon kick
    angles = cg.weapon.kick.angles;
    cg.weapon.kick.angles = vec3_origin;

    // add offset based on weapon kick
    origin = cg.weapon.kick.origin;
    cg.weapon.kick.origin = vec3_origin;

    // add angles based on damage kick
    if (cg.v_dmg_time > cg.time) {
        ratio = (float)(cg.v_dmg_time - cg.time) / DAMAGE_TIME;
        angles.pitch += ratio * cg.v_dmg_pitch;
        angles.roll += ratio * cg.v_dmg_roll;
    }

    // add pitch based on fall kick
    if (cg.fall_time > cg.time) {
        ratio = (float)(cg.fall_time - cg.time) / FALL_TIME;
        angles.pitch += ratio * cg.fall_value;

        // add fall height
        origin.z -= ratio * cg.fall_value * 0.4f;
    }

    cg.kick_angles[frame] = angles;
    cg.kick_origin[frame] = origin;
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

        vec3_t kick = Vec3_Lerp(cg.kick_angles[ofs], cg.kick_angles[ofs ^ 1], f);
        cg.refdef.viewangles = Vec3_Add(cg.refdef.viewangles, kick);

        kick = Vec3_Lerp(cg.kick_origin[ofs], cg.kick_origin[ofs ^ 1], f);
        cg.refdef.vieworg = Vec3_Add(cg.refdef.vieworg, kick);
    }

    // run earthquake angles at 40 Hz
    if (cg.quake_time > cg.time) {
        int ofs = (cg.time / 25) & 1;
        float f = (cg.time % 25) * 0.04f;

        if (cg.quake_frame != ofs) {
            cg.quake_angles[ofs ^ 1] = Vec3_CenterRandom();
            cg.quake_frame = ofs;
        }

        vec3_t kick = Vec3_Lerp(cg.quake_angles[ofs], cg.quake_angles[ofs ^ 1], f);

        f = cg.quake_time * 0.25f / cg.time;
        cg.refdef.viewangles = Vec3_MA(cg.refdef.viewangles, f, kick);
    }

    // add angles based on velocity
    float delta = Vec3_Dot(cg.slowvelocity, cg.v_forward);
    cg.refdef.viewangles.pitch += delta * cg_run_pitch.value;

    delta = Vec3_Dot(cg.slowvelocity, cg.v_right);
    cg.refdef.viewangles.roll += delta * cg_run_roll.value;

    float factor = 1;
    if ((cg.predicted_ps.pm_flags & (PMF_DUCKED | PMF_ON_GROUND)) == (PMF_DUCKED | PMF_ON_GROUND))
        factor = 6;
    CG_AdvanceValue(&cg.bob_factor, factor, 50);

    // add angles based on bob
    delta = fabsf(cg.bobfracsin) * cg_bob_pitch.value * cg.xyspeed * cg.bob_factor;
    cg.refdef.viewangles.pitch += delta;

    delta = cg.bobfracsin * cg_bob_roll.value * cg.xyspeed * cg.bob_factor;
    cg.refdef.viewangles.roll += delta;

    // add bob height
    float bob = fabsf(cg.bobfracsin) * cg.xyspeed * cg_bob_up.value;
    if (bob > 6)
        bob = 6;
    cg.refdef.vieworg.z += bob;
}

static void CG_SetupFirstPersonView(void)
{
    // smooth abrupt stops
    for (int i = 0; i < 3; i++)
        CG_AdvanceValue(&cg.slowvelocity.xyz[i], cg.predicted_ps.velocity.xyz[i], 1000);

    cg.bobfracsin = sinf(cg.predicted_ps.bobtime * (M_PIf / 128));
    cg.xyspeed = Vec2_Length(Vec2_FromVec3(cg.slowvelocity));

    CG_CalcViewOffset();

    // add the weapon
    CG_AddViewWeapon();

    cg.third_person_view = false;
}

// need to interpolate bmodel positions, or third person view would be very jerky
static void CG_LerpedTrace(trace_t *tr, const trace_args_t *args)
{
    trace_t trace;
    const centity_t *ent;
    vec3_t org, ang;

    // check against world
    trap_BoxTrace(tr, args, MODELINDEX_WORLD);
    tr->entnum = ENTITYNUM_WORLD;
    if (tr->fraction == 0)
        return;     // blocked by the world

    // check all other solid models
    for (int i = 0; i < cg.num_solid_entities; i++) {
        ent = cg.solid_entities[i];

        // special value for bmodel
        if (ent->current.solid != PACKED_BSP)
            continue;

        org = Vec3_Lerp(ent->prev.origin, ent->current.origin, cg.lerpfrac);
        ang = Vec3_LerpAngles(ent->prev.angles, ent->current.angles, cg.lerpfrac);

        trap_TransformedBoxTrace(&trace, args, ent->current.modelindex, org, ang);

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
    vec3_t focus;
    float fscale, rscale;
    float dist, angle, range;
    trace_t trace;

    // if dead, set a nice view angle
    if (cg.frame->ps.stats[STAT_HEALTH] <= 0) {
        cg.refdef.viewangles.roll = 0;
        cg.refdef.viewangles.pitch = 10;
    }

    focus = Vec3_MA(cg.refdef.vieworg, 512, cg.v_forward);
    cg.refdef.vieworg.z += 8;

    cg.refdef.viewangles.pitch *= 0.5f;
    AngleVectors(cg.refdef.viewangles, &cg.v_forward, &cg.v_right, &cg.v_up);

    angle = DEG2RAD(cg_thirdperson_angle.value);
    range = cg_thirdperson_range.value;
    fscale = cosf(angle);
    rscale = sinf(angle);
    cg.refdef.vieworg = Vec3_MA(cg.refdef.vieworg, -range * fscale, cg.v_forward);
    cg.refdef.vieworg = Vec3_MA(cg.refdef.vieworg, -range * rscale, cg.v_right);

    trace_args_t args = {
        .start = cg.player_entity_origin,
        .end = cg.refdef.vieworg,
        .box = Box3_FromRadius(4),
        .entnum = ENTITYNUM_NONE,
        .mask = CONTENTS_SOLID
    };

    CG_LerpedTrace(&trace, &args);
    cg.refdef.vieworg = trace.endpos;
    cg.third_person_alpha = trace.fraction;

    focus = Vec3_Sub(focus, cg.refdef.vieworg);
    dist = Vec2_Length(Vec2_FromVec3(focus));

    cg.refdef.viewangles.pitch = -RAD2DEG(atan2f(focus.z, dist));
    cg.refdef.viewangles.yaw -= cg_thirdperson_angle.value;

    cg.third_person_view = true;
}

static void CG_FinishViewValues(void)
{
    if (cg_thirdperson.integer && CG_GetPlayerEntity())
        CG_SetupThirdPersionView();
    else
        CG_SetupFirstPersonView();
}

static float CG_LerpFov(float ofov, float nfov, float lerp)
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

static void CG_ScreenEffects(void)
{
    // add for contents
    contents_t contents = CG_PointContents(cg.refdef.vieworg);

    if (contents & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER))
        cg.refdef.rdflags |= RDF_UNDERWATER;
    else
        cg.refdef.rdflags &= ~RDF_UNDERWATER;

    if (contents & (CONTENTS_SOLID | CONTENTS_LAVA))
        BG_AddBlend(1.0f, 0.3f, 0.0f, 0.6f, &cg.refdef.screen_blend);
    else if (contents & CONTENTS_SLIME)
        BG_AddBlend(0.0f, 0.1f, 0.05f, 0.6f, &cg.refdef.screen_blend);
    else if (contents & CONTENTS_WATER)
        BG_AddBlend(0.5f, 0.3f, 0.2f, 0.4f, &cg.refdef.screen_blend);
}

#define S 0.57735f

static const vec3_t size_probe_dirs[SIZE_PROBES] = {
    { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 },
    {-1, 0, 0 }, { 0,-1, 0 }, { 0, 0,-1 },
    { S, S, S }, {-S, S, S }, { S,-S, S }, {-S,-S, S },
    { S, S,-S }, {-S, S,-S }, { S,-S,-S }, {-S,-S,-S },
};

#undef S

static void CG_RunSizeProbes(void)
{
    if (cg.time < cg.size_probe_time)
        return;

    vec3_t end = Vec3_MA(cg.refdef.vieworg, 8192, size_probe_dirs[cg.size_probe_index]);
    trace_t tr = CG_TraceLine(cg.refdef.vieworg, end, ENTITYNUM_NONE, MASK_SOLID);
    cg.size_probes[cg.size_probe_index] = Vec3_Sub(tr.endpos, cg.refdef.vieworg);

    if (cg.size_probe_index == 2 && (tr.surface_flags & SURF_SKY))
        cg.size_probes[cg.size_probe_index].z += 4096;
    if (cg.size_probe_index == 5)
        cg.size_probe_ground_surf = tr.surface_id;

    cg.size_probe_index = (cg.size_probe_index + 1) % SIZE_PROBES;
    cg.size_probe_time = cg.time + SIZE_PROBE_TIME;
}

static void CG_UpdateReverb(void)
{
    reverb_preset_t preset;
    surface_info_t info;
    box3_t box;
    float len;

    if (!s_reverb.integer)
        return;

    CG_RunSizeProbes();

    if (cg.time < cg.reverb_time) {
        if (cg.reverb_lerp) {
            float lerp = 1.0f - (float)(cg.reverb_time - cg.time) / REVERB_TIME;
            cg.listener.reverb[cg.reverb_index    ].gain = lerp;
            cg.listener.reverb[cg.reverb_index ^ 1].gain = 1.0f - lerp;
        }
        return;
    }

    box = Box3_FromPoints(cg.size_probes, SIZE_PROBES);
    len = Box3_Diameter(box);

    // hardcoded, not going to parse json shit for this
    if (len <= 200) {
        preset = REVERB_PRESET_SEWERPIPE;
    } else if (len <= 500) {
        trap_GetSurfaceInfo(cg.size_probe_ground_surf, &info);
        if (!strcmp(info.material, "boot") ||
            !strcmp(info.material, "mech") ||
            !strcmp(info.material, "clank"))
            preset = REVERB_PRESET_STONEROOM;
        else if (!strcmp(info.material, "grass") ||
                 !strcmp(info.material, "flesh") ||
                 !strcmp(info.material, "water") ||
                 !strcmp(info.material, "snow"))
            preset = REVERB_PRESET_ROOM;
        else
            preset = REVERB_PRESET_LIVINGROOM;
    } else if (len <= 900) {
        trap_GetSurfaceInfo(cg.size_probe_ground_surf, &info);
        if (!strcmp(info.material, "grass") ||
            !strcmp(info.material, "flesh") ||
            !strcmp(info.material, "water") ||
            !strcmp(info.material, "snow"))
            preset = REVERB_PRESET_ARENA;
        else
            preset = REVERB_PRESET_HANGAR;
    } else if (len <= 1200) {
        preset = REVERB_PRESET_AUDITORIUM;
    } else if (len <= 1600) {
        preset = REVERB_PRESET_CONCERTHALL;
    } else if (len <= 2000) {
        preset = REVERB_PRESET_CITY;
    } else {
        preset = REVERB_PRESET_PLAIN;
    }

    cg.reverb_lerp = false;
    cg.listener.reverb[cg.reverb_index    ].gain   = 1.0f;
    cg.listener.reverb[cg.reverb_index ^ 1].preset = REVERB_PRESET_NONE;

    if (preset != cg.listener.reverb[cg.reverb_index].preset) {
        cg.reverb_lerp = true;
        cg.reverb_index ^= 1;
        cg.listener.reverb[cg.reverb_index].gain   = 0.0f;
        cg.listener.reverb[cg.reverb_index].preset = preset;
    }

    cg.reverb_time = cg.time + REVERB_TIME;
}

/*
===============
CG_CalcViewValues

Sets cg.refdef view values and sound spatialization params.
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

    unsigned steptime = cgs.realtime - cg.predicted_step_time;

    // calculate the origin
    if (CG_PredictionEnabled()) {
        // use predicted values
        float backlerp = lerp - 1.0f;
        cg.refdef.vieworg = Vec3_MA(cg.predicted_ps.origin, backlerp, cg.prediction_error);

        // smooth out stair climbing
        if (steptime < STEP_TIME)
            cg.refdef.vieworg.z -= cg.predicted_step * (STEP_TIME - steptime);
    } else {
        // just use interpolated values
        cg.refdef.vieworg = Vec3_Lerp(ops->origin, ps->origin, lerp);
        cg.predicted_ps.velocity = Vec3_Lerp(ops->velocity, ps->velocity, lerp);
        cg.predicted_ps.viewheight = ps->viewheight;

        int delta = ps->bobtime - ops->bobtime;
        if (delta < -128)
            delta += 256;
        cg.predicted_ps.bobtime = ops->bobtime + cg.lerpfrac * delta;
        cg.predicted_ps.pm_flags = ps->pm_flags;

        // smooth out stair climbing
        if (steptime < STEP_TIME)
            cg.refdef.vieworg.z = ps->origin.z - cg.predicted_step * (STEP_TIME - steptime);
    }

    // if not running a demo or on a locked frame, add the local angle movement
    if (cgs.demoplayback) {
        if (trap_Key_GetDest() == KEY_NONE && trap_Key_IsDown(K_SHIFT)) {
            cg.refdef.viewangles = cg.predicted_ps.viewangles;
        } else {
            cg.refdef.viewangles = Vec3_LerpAngles(ops->viewangles, ps->viewangles, lerp);
        }
    } else if (ps->pm_type < PM_DEAD) {
        // use predicted values
        cg.refdef.viewangles = cg.predicted_ps.viewangles;
    } else if (ops->pm_type < PM_DEAD) {
        // lerp from predicted angles, since enhanced servers
        // do not send viewangles each frame
        cg.refdef.viewangles = Vec3_LerpAngles(cg.predicted_ps.viewangles, ps->viewangles, lerp);
    } else {
        // just use interpolated values
        cg.refdef.viewangles = Vec3_LerpAngles(ops->viewangles, ps->viewangles, lerp);
    }

    cg.viewangles_delta = Vec3_Sub(cg.oldviewangles, cg.refdef.viewangles);
    cg.oldviewangles = cg.refdef.viewangles;

    // interpolate blend
    if (ops->screen_blend.a)
        cg.refdef.screen_blend = Vec4_Lerp(ops->screen_blend, ps->screen_blend, lerp);
    else
        cg.refdef.screen_blend = ps->screen_blend;

    if (ops->damage_blend.a)
        cg.refdef.damage_blend = Vec4_Lerp(ops->damage_blend, ps->damage_blend, lerp);
    else
        cg.refdef.damage_blend = ps->damage_blend;

    // interpolate fog
    cg.refdef.fog = BG_LerpFog(ops->fog, ps->fog, lerp);

    // no lerping if moved too far
    if (fabsf(ps->heightfog.start.dist - ops->heightfog.start.dist) > 512 ||
        fabsf(ps->heightfog.end  .dist - ops->heightfog.end  .dist) > 512)
        cg.refdef.heightfog = ps->heightfog;
    else
        cg.refdef.heightfog = BG_LerpHeightFog(ops->heightfog, ps->heightfog, lerp);

    // interpolate field of view
    cg.fov_x = CG_LerpFov(ops->fov, ps->fov, lerp);
    cg.fov_y = V_CalcFov(cg.fov_x, 4, 3);

    AngleVectors(cg.refdef.viewangles, &cg.v_forward, &cg.v_right, &cg.v_up);

    cg.player_entity_origin = cg.refdef.vieworg;
    cg.player_entity_angles = cg.refdef.viewangles;

    if (cg.player_entity_angles.pitch > 180) {
        cg.player_entity_angles.pitch -= 360;
    }

    cg.player_entity_angles.pitch = cg.player_entity_angles.pitch / 3;

    // add view height
    cg.refdef.vieworg.z += cg.predicted_ps.viewheight;
    if (cg.duck_time > cg.time)
        cg.refdef.vieworg.z -= (cg.duck_time - cg.time) * cg.duck_factor;

    cg.refdef.rdflags = ps->rdflags;

    CG_ScreenEffects();

    // update listener
    cg.listener.entnum = ps->clientnum;
    cg.listener.origin = cg.refdef.vieworg;
    cg.listener.velocity = cg.predicted_ps.velocity;
    cg.listener.v_forward = cg.v_forward;
    cg.listener.v_up = cg.v_up;
    cg.listener.underwater = cg.refdef.rdflags & RDF_UNDERWATER;
    CG_UpdateReverb();

    trap_S_UpdateListener(&cg.listener);
}

/*
==================
CG_RenderView

==================
*/
void CG_RenderView(void)
{
    CG_PredictMovement();

    CG_CalcViewValues();

    CG_FinishViewValues();

    // build a refresh entity list
    CG_AddEntities();

#if USE_DEBUG
    if (cg.test_model.model)
        trap_R_AddEntity(&cg.test_model);
    if (cg.test_muzzle.model)
        trap_R_AddEntity(&cg.test_muzzle);
#endif

    // never let it sit exactly on a node line, because a water plane can
    // disappear when viewed with the eye exactly on it.
    // the server protocol only specifies to 1/8 pixel, so add 1/16 in each axis
    cg.refdef.vieworg.x += 1.0f / 16;
    cg.refdef.vieworg.y += 1.0f / 16;
    cg.refdef.vieworg.z += 1.0f / 16;

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

    if (cg_custom_fog.density) {
        cg.refdef.fog = cg_custom_fog;
        cg.refdef.heightfog = (player_heightfog_t){ 0 };
    }

    trap_R_RenderScene(&cg.refdef);
}
