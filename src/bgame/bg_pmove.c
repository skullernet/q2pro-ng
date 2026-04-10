// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "shared/shared.h"
#include "bgame/bg_local.h"

// all of the locals will be zeroed before each
// pmove, just to make damn sure we don't have
// any differences when running on client or server

typedef struct {
    vec3_t origin;
    vec3_t velocity;

    vec3_t forward, right, up;
    float  frametime;

    cplane_t groundplane;
    int groundsurface_flags;
    int groundcontents;

    contents_t clipmask;
} pml_t;

// movement parameters
typedef struct {
    float stopspeed;
    float flystopspeed;
    float maxspeed;
    float duckspeed;
    float accelerate;
    float wateraccelerate;
    float friction;
    float flyfriction;
    float waterfriction;
    float waterspeed;
    float laddermod;
    float jumpvel;
} pmp_t;

static const pmp_t pmp_default = {
    .stopspeed = 100,
    .flystopspeed = 100,
    .maxspeed = 300,
    .duckspeed = 100,
    .accelerate = 10,
    .wateraccelerate = 10,
    .friction = 6,
    .flyfriction = 9,
    .waterfriction = 1,
    .waterspeed = 400,
    .laddermod = 0.75f,
    .jumpvel = 270.0f,
};

static const pmp_t pmp_psxscale = {
    .stopspeed = 100 * PSX_PHYSICS_SCALAR,
    .flystopspeed = 100,
    .maxspeed = 300,
    .duckspeed = 100 * 1.25f,
    .accelerate = 10,
    .wateraccelerate = 10,
    .friction = 6 * PSX_PHYSICS_SCALAR,
    .flyfriction = 9,
    .waterfriction = 1 * PSX_PHYSICS_SCALAR,
    .waterspeed = 400,
    .laddermod = 0.5f,
    .jumpvel = 270.0f * PSX_PHYSICS_SCALAR * 1.15f,
};

pm_config_t pm_config;

static pmove_t      *pm;
static pml_t         pml;
static const pmp_t  *pmp;

// walking up a step should kill some velocity

static contents_t PM_TraceMask(void)
{
    contents_t mask = CONTENTS_NONE;

    if (pm->s->pm_type == PM_DEAD || pm->s->pm_type == PM_GIB)
        mask = MASK_DEADSOLID;
    else if (pm->s->pm_type == PM_SPECTATOR)
        mask = MASK_SOLID;
    else
        mask = MASK_PLAYERSOLID;

    if (pm->s->pm_flags & PMF_IGNORE_PLAYER_COLLISION)
        mask &= ~CONTENTS_PLAYER;

    return mask;
}

static inline trace_t PM_Trace(vec3_t start, vec3_t end, box3_t box, contents_t mask)
{
    trace_t tr;

    if (pm->s->pm_type == PM_SPECTATOR)
        pm->clip(&tr, &(trace_args_t){ start, end, box, ENTITYNUM_WORLD, MASK_SOLID });
    else
        pm->trace(&tr, &(trace_args_t){ start, end, box, pm->s->clientnum, mask });

    return tr;
}

static inline void PM_StepSlideMove_(void)
{
    PM_StepSlideMove_Generic(&pml.origin, &pml.velocity, pml.frametime, pm->box,
                             pm->s->clientnum, pml.clipmask, &pm->touch, pm->s->pm_time, pm->trace);
}

/*
==================
PM_StepSlideMove

==================
*/
static void PM_StepSlideMove(void)
{
    vec3_t  start_o, start_v;
    vec3_t  down_o, down_v;
    trace_t trace;
    float   down_dist, up_dist;
    vec3_t up, down;

    start_o = pml.origin;
    start_v = pml.velocity;

    PM_StepSlideMove_();

    if (!PM_AllowStepUp() && !(pm->s->pm_flags & PMF_ON_GROUND))
        return; // no step up

    down_o = pml.origin;
    down_v = pml.velocity;

    up = start_o;
    up.z += STEPSIZE;

    trace = PM_Trace(start_o, up, pm->box, pml.clipmask);
    if (trace.allsolid)
        return; // can't step up

    float step_size = trace.endpos.z - start_o.z;

    // try sliding above
    pml.origin = trace.endpos;
    pml.velocity = start_v;

    PM_StepSlideMove_();

    // push down the final amount
    down = pml.origin;
    down.z -= step_size;

    // [Paril-KEX] jitspoe suggestion for stair clip fix; store
    // the old down position, and pick a better spot for downwards
    // trace if the start origin's Z position is lower than the down end pt.
    vec3_t original_down = down;

    if (start_o.z < down.z)
        down.z = start_o.z - 1.0f;

    trace = PM_Trace(pml.origin, down, pm->box, pml.clipmask);
    if (!trace.allsolid)
        // [Paril-KEX] from above, do the proper trace now
        pml.origin = PM_Trace(pml.origin, original_down, pm->box, pml.clipmask).endpos;

    up = pml.origin;

    // decide which one went farther
    down_dist = Vec2_LengthSquared(Vec2_FromVec3(Vec3_Sub(down_o, start_o)));
    up_dist   = Vec2_LengthSquared(Vec2_FromVec3(Vec3_Sub(up,     start_o)));

    if (down_dist > up_dist || trace.plane.normal.z < MIN_STEP_NORMAL) {
        pml.origin = down_o;
        pml.velocity = down_v;
    } else {
        //!! Special case
        // if we were walking along a plane, then we need to copy the Z over
        pml.velocity.z = down_v.z;
    }

    // Paril: step down stairs/slopes
    if ((pm->s->pm_flags & PMF_ON_GROUND) && !(pm->s->pm_flags & PMF_ON_LADDER) &&
        (pm->waterlevel < WATER_WAIST || (!(pm->cmd.buttons & BUTTON_JUMP) && pml.velocity.z <= 0))) {
        down = pml.origin;
        down.z -= STEPSIZE;
        trace = PM_Trace(pml.origin, down, pm->box, pml.clipmask);
        if (trace.fraction < 1.0f)
            pml.origin = trace.endpos;
    }

    // export step height
    if (memcmp(&pml.groundplane, &trace.plane, sizeof(pml.groundplane)))
        pm->step_height = truncf(pml.origin.z - down_o.z);
}

/*
==================
PM_Friction

Handles both ground friction and water friction
==================
*/
static void PM_Friction(void)
{
    float  speed, newspeed, control;
    float  friction;
    float  drop;

    speed = Vec3_Length(pml.velocity);
    if (speed < 1) {
        pml.velocity.x = 0;
        pml.velocity.y = 0;
        return;
    }

    drop = 0;

    // apply ground friction
    if ((pm->groundentitynum != ENTITYNUM_NONE && !(pml.groundsurface_flags & SURF_SLICK)) || (pm->s->pm_flags & PMF_ON_LADDER)) {
        friction = pmp->friction;
        control = speed < pmp->stopspeed ? pmp->stopspeed : speed;
        drop += control * friction * pml.frametime;
    }

    // apply water friction
    if (pm->waterlevel && !(pm->s->pm_flags & PMF_ON_LADDER))
        drop += speed * pmp->waterfriction * pm->waterlevel * pml.frametime;

    // scale the velocity
    newspeed = speed - drop;
    if (newspeed < 0)
        newspeed = 0;
    newspeed /= speed;

    pml.velocity = Vec3_Scale(pml.velocity, newspeed);
}

/*
==============
PM_Accelerate

Handles user intended acceleration
==============
*/
static void PM_Accelerate(vec3_t wishdir, float wishspeed, float accel)
{
    float addspeed, accelspeed, currentspeed;

    if (pm_config.physics_flags & PHYSICS_PSX_SCALE) {
        wishspeed *= PSX_PHYSICS_SCALAR;
        accel *= PSX_PHYSICS_SCALAR;
    }

    currentspeed = Vec3_Dot(pml.velocity, wishdir);
    addspeed = wishspeed - currentspeed;
    if (addspeed <= 0)
        return;
    accelspeed = accel * pml.frametime * wishspeed;
    if (accelspeed > addspeed)
        accelspeed = addspeed;

    pml.velocity = Vec3_MA(pml.velocity, accelspeed, wishdir);
}

static void PM_AirAccelerate(vec3_t wishdir, float wishspeed, float accel)
{
    float addspeed, accelspeed, currentspeed, wishspd = wishspeed;

    if (wishspd > 30)
        wishspd = 30;
    currentspeed = Vec3_Dot(pml.velocity, wishdir);
    addspeed = wishspd - currentspeed;
    if (addspeed <= 0)
        return;
    accelspeed = accel * wishspeed * pml.frametime;
    if (accelspeed > addspeed)
        accelspeed = addspeed;

    pml.velocity = Vec3_MA(pml.velocity, accelspeed, wishdir);
}

/*
=============
PM_AddCurrents
=============
*/
static vec3_t PM_AddCurrents(vec3_t wishvel)
{
    vec3_t v;
    float  s;

    //
    // account for ladders
    //

    if (pm->s->pm_flags & PMF_ON_LADDER) {
        if (pm->cmd.buttons & (BUTTON_JUMP | BUTTON_CROUCH)) {
            // [Paril-KEX]: if we're underwater, use full speed on ladders
            float ladder_speed = pm->waterlevel >= WATER_WAIST ? pmp->maxspeed : 200;

            if (pm->cmd.buttons & BUTTON_JUMP)
                wishvel.z = ladder_speed;
            else if (pm->cmd.buttons & BUTTON_CROUCH)
                wishvel.z = -ladder_speed;
        } else if (pm->cmd.forwardmove) {
            // [Paril-KEX] clamp the speed a bit so we're not too fast
            float ladder_speed = Q_clipf(pm->cmd.forwardmove, -200, 200);

            if (pm->cmd.forwardmove > 0) {
                if (pm->s->viewangles.pitch < 15)
                    wishvel.z = ladder_speed;
                else
                    wishvel.z = -ladder_speed;
            // [Paril-KEX] allow using "back" arrow to go down on ladder
            } else if (pm->cmd.forwardmove < 0) {
                // if we haven't touched ground yet, remove x/y so we don't
                // slide off of the ladder
                if (pm->groundentitynum == ENTITYNUM_NONE)
                    wishvel.x = wishvel.y = 0;

                wishvel.z = ladder_speed;
            }
        } else
            wishvel.z = 0;

        // limit horizontal speed when on a ladder
        // [Paril-KEX] unless we're on the ground
        if (pm->groundentitynum == ENTITYNUM_NONE) {
            // [Paril-KEX] instead of left/right not doing anything,
            // have them move you perpendicular to the ladder plane
            if (pm->cmd.sidemove) {
                // clamp side speed so it's not jarring...
                float ladder_speed = Q_clipf(pm->cmd.sidemove, -150, 150);

                if (pm->waterlevel < WATER_WAIST)
                    ladder_speed *= pmp->laddermod;

                // check for ladder
                vec3_t flatforward = { pml.forward.x, pml.forward.y };
                flatforward = Vec3_Normalize(flatforward);

                vec3_t spot = Vec3_MA(pml.origin, 1, flatforward);
                trace_t trace = PM_Trace(pml.origin, spot, pm->box, CONTENTS_LADDER);

                if (trace.fraction != 1.0f && (trace.contents & CONTENTS_LADDER)) {
                    vec3_t right = { -trace.plane.normal.y, trace.plane.normal.x };
                    wishvel.x = wishvel.y = 0;
                    wishvel = Vec3_MA(wishvel, ladder_speed, right);
                }
            } else {
                wishvel.x = Q_clipf(wishvel.x, -25, 25);
                wishvel.y = Q_clipf(wishvel.y, -25, 25);
            }
        }
    }

    //
    // add water currents
    //

    if (pm->watertype & MASK_CURRENT) {
        v = vec3_origin;

        if (pm->watertype & CONTENTS_CURRENT_0)
            v.x += 1;
        if (pm->watertype & CONTENTS_CURRENT_90)
            v.y += 1;
        if (pm->watertype & CONTENTS_CURRENT_180)
            v.x -= 1;
        if (pm->watertype & CONTENTS_CURRENT_270)
            v.y -= 1;
        if (pm->watertype & CONTENTS_CURRENT_UP)
            v.z += 1;
        if (pm->watertype & CONTENTS_CURRENT_DOWN)
            v.z -= 1;

        s = pmp->waterspeed;
        if ((pm->waterlevel == WATER_FEET) && (pm->groundentitynum != ENTITYNUM_NONE))
            s /= 2;

        wishvel = Vec3_MA(wishvel, s, v);
    }

    //
    // add conveyor belt velocities
    //

    if (pm->groundentitynum != ENTITYNUM_NONE) {
        v = vec3_origin;

        if (pml.groundcontents & CONTENTS_CURRENT_0)
            v.x += 1;
        if (pml.groundcontents & CONTENTS_CURRENT_90)
            v.y += 1;
        if (pml.groundcontents & CONTENTS_CURRENT_180)
            v.x -= 1;
        if (pml.groundcontents & CONTENTS_CURRENT_270)
            v.y -= 1;
        if (pml.groundcontents & CONTENTS_CURRENT_UP)
            v.z += 1;
        if (pml.groundcontents & CONTENTS_CURRENT_DOWN)
            v.z -= 1;

        wishvel = Vec3_MA(wishvel, 100, v);
    }

    return wishvel;
}

/*
===================
PM_WaterMove
===================
*/
static void PM_WaterMove(void)
{
    vec3_t wishvel;
    float  wishspeed;
    vec3_t wishdir;

    //
    // user intentions
    //
    wishvel = Vec3_Mix(pml.forward, pml.right, pm->cmd.forwardmove, pm->cmd.sidemove);

    if (!pm->cmd.forwardmove && !pm->cmd.sidemove &&
        !(pm->cmd.buttons & (BUTTON_JUMP | BUTTON_CROUCH))) {
        if (pm->groundentitynum == ENTITYNUM_NONE)
            wishvel.z -= 60; // drift towards bottom
    } else {
        if (pm->cmd.buttons & BUTTON_CROUCH)
            wishvel.z -= pmp->waterspeed * 0.5f;
        else if (pm->cmd.buttons & BUTTON_JUMP)
            wishvel.z += pmp->waterspeed * 0.5f;
    }

    wishvel = PM_AddCurrents(wishvel);

    wishdir = Vec3_NormalizeLength(wishvel, &wishspeed);

    if (wishspeed > pmp->maxspeed)
        wishspeed = pmp->maxspeed;
    wishspeed *= 0.5f;

    if ((pm->s->pm_flags & PMF_DUCKED) && wishspeed > pmp->duckspeed)
        wishspeed = pmp->duckspeed;

    PM_Accelerate(wishdir, wishspeed, pmp->wateraccelerate);

    PM_StepSlideMove();
}

/*
===================
PM_AirMove
===================
*/
static void PM_AirMove(void)
{
    vec3_t wishvel;
    vec3_t wishdir;
    float  wishspeed;
    float  maxspeed;

    wishvel = Vec3_Mix(pml.forward, pml.right, pm->cmd.forwardmove, pm->cmd.sidemove);
    wishvel.z = 0;

    wishvel = PM_AddCurrents(wishvel);

    wishdir = Vec3_NormalizeLength(wishvel, &wishspeed);

    //
    // clamp to server defined max speed
    //
    maxspeed = (pm->s->pm_flags & PMF_DUCKED) ? pmp->duckspeed : pmp->maxspeed;

    if (wishspeed > maxspeed)
        wishspeed = maxspeed;

    if (pm->s->pm_flags & PMF_ON_LADDER) {
        PM_Accelerate(wishdir, wishspeed, pmp->accelerate);
        if (!wishvel.z) {
            if (pml.velocity.z > 0) {
                pml.velocity.z -= pm->s->gravity * pml.frametime;
                if (pml.velocity.z < 0)
                    pml.velocity.z = 0;
            } else {
                pml.velocity.z += pm->s->gravity * pml.frametime;
                if (pml.velocity.z > 0)
                    pml.velocity.z = 0;
            }
        }
        PM_StepSlideMove();
    } else if (pm->groundentitynum != ENTITYNUM_NONE) {
        // walking on ground
        pml.velocity.z = 0; //!!! this is before the accel
        PM_Accelerate(wishdir, wishspeed, pmp->accelerate);

        // PGM  -- fix for negative trigger_gravity fields
        if (pm->s->gravity > 0)
            pml.velocity.z = 0;
        else
            pml.velocity.z -= pm->s->gravity * pml.frametime;
        // PGM

        if (!pml.velocity.x && !pml.velocity.y)
            return;
        PM_StepSlideMove();
    } else {
        // not on ground, so little effect on velocity
        if (pm_config.airaccel)
            PM_AirAccelerate(wishdir, wishspeed, pm_config.airaccel);
        else
            PM_Accelerate(wishdir, wishspeed, 1);

        // add gravity
        if (pm->s->pm_type != PM_GRAPPLE)
            pml.velocity.z -= pm->s->gravity * pml.frametime;

        PM_StepSlideMove();
    }
}

static void PM_GetWaterLevel(vec3_t position, water_level_t *level, contents_t *type)
{
    //
    // get waterlevel, accounting for ducking
    //
    vec3_t point = position;
    point.z += pm->box.mins.z + 1;

    contents_t cont = pm->pointcontents(point);
    if (cont & MASK_WATER) {
        *level = WATER_FEET;
        *type = cont;

        int sample2 = pm->s->viewheight - pm->box.mins.z;
        int sample1 = sample2 / 2;

        point.z = position.z + pm->box.mins.z + sample1;
        cont = pm->pointcontents(point);
        if (cont & MASK_WATER) {
            *level = WATER_WAIST;
            point.z = position.z + pm->box.mins.z + sample2;
            cont = pm->pointcontents(point);
            if (cont & MASK_WATER)
                *level = WATER_UNDER;
        }
    } else {
        *level = WATER_NONE;
        *type = CONTENTS_NONE;
    }
}

/*
=============
PM_CategorizePosition
=============
*/
static void PM_CategorizePosition(void)
{
    // if the player hull point one unit down is solid, the player
    // is on ground

    // see if standing on something solid
    if ((pm->s->pm_flags & PMF_NO_GROUND_SEEK) || pml.velocity.z > 180 || pm->s->pm_type == PM_GRAPPLE) { //!!ZOID changed from 100 to 180 (ramp accel)
        pm->s->pm_flags &= ~PMF_ON_GROUND;
        pm->groundentitynum = ENTITYNUM_NONE;
    } else {
        vec3_t point = pml.origin;
        point.z -= 0.25f;

        trace_t trace = PM_Trace(pml.origin, point, pm->box, pml.clipmask);
        pml.groundplane = trace.plane;
        pml.groundsurface_flags = trace.surface_flags;
        pml.groundcontents = trace.contents;

        // [Paril-KEX] to attempt to fix edge cases where you get stuck
        // wedged between a slope and a wall (which is irrecoverable
        // most of the time), we'll allow the player to "stand" on
        // slopes if they are right up against a wall
        bool slanted_ground = !trace.startsolid && trace.fraction < 1.0f && trace.plane.normal.z < 0.7f;

        if (slanted_ground) {
            point = Vec3_Add(pml.origin, trace.plane.normal);
            trace_t slant = PM_Trace(pml.origin, point, pm->box, pml.clipmask);

            if (slant.fraction < 1.0f && !slant.startsolid && slant.plane.normal.z > -0.01f)
                slanted_ground = false;
        }

        if (trace.fraction == 1.0f || slanted_ground) {
            pm->groundentitynum = ENTITYNUM_NONE;
            pm->s->pm_flags &= ~PMF_ON_GROUND;
        } else {
            pm->groundentitynum = trace.entnum;

            // hitting solid ground will end a waterjump
            if (pm->s->pm_flags & PMF_TIME_WATERJUMP) {
                pm->s->pm_flags &= ~(PMF_TIME_WATERJUMP | PMF_TIME_LAND | PMF_TIME_TELEPORT | PMF_TIME_TRICK);
                pm->s->pm_time = 0;
            }

            if (!(pm->s->pm_flags & PMF_ON_GROUND)) {
                // just hit the ground

                // [Paril-KEX]
                if (PM_AllowTrickJump() && pml.velocity.z >= 100.0f && pml.groundplane.normal.z >= 0.9f && !(pm->s->pm_flags & PMF_DUCKED)) {
                    pm->s->pm_flags |= PMF_TIME_TRICK;
                    pm->s->pm_time = 64;
                }

                // [Paril-KEX] calculate impact delta; this also fixes triple jumping
                vec3_t clipped_velocity = PM_ClipVelocity(pml.velocity, pml.groundplane.normal, 1.01f);

                pm->impact_delta = pm->s->velocity.z - clipped_velocity.z;

                if (pm_config.physics_flags & PHYSICS_PSX_SCALE)
                    pm->impact_delta *= 1.0f / PSX_PHYSICS_SCALAR;

                pm->s->pm_flags |= PMF_ON_GROUND;

                if (PM_NeedsLandTime() || (pm->s->pm_flags & PMF_DUCKED)) {
                    pm->s->pm_flags |= PMF_TIME_LAND;
                    pm->s->pm_time = 128;
                    if (pm_config.physics_flags & PHYSICS_PSX_SCALE)
                        pm->s->pm_time /= 2;
                }
            }
        }

        PM_RecordTrace(&pm->touch, &trace);
    }

    //
    // get waterlevel, accounting for ducking
    //
    PM_GetWaterLevel(pml.origin, &pm->waterlevel, &pm->watertype);
}

/*
=============
PM_CheckJump
=============
*/
static void PM_CheckJump(void)
{
    if (pm->s->pm_flags & PMF_TIME_LAND) {
        // hasn't been long enough since landing to jump again
        return;
    }

    if (!(pm->cmd.buttons & BUTTON_JUMP)) {
        // not holding jump
        pm->s->pm_flags &= ~PMF_JUMP_HELD;
        return;
    }

    // must wait for jump to be released
    if (pm->s->pm_flags & PMF_JUMP_HELD)
        return;

    if (pm->s->pm_type == PM_DEAD)
        return;

    if (pm->waterlevel >= WATER_WAIST) {
        // swimming, not jumping
        pm->groundentitynum = ENTITYNUM_NONE;
        return;
    }

    if (pm->groundentitynum == ENTITYNUM_NONE)
        return; // in air, so no effect

    pm->s->pm_flags |= PMF_JUMP_HELD;
    pm->jump_sound = true;
    pm->groundentitynum = ENTITYNUM_NONE;
    pm->s->pm_flags &= ~PMF_ON_GROUND;

    pml.velocity.z += pmp->jumpvel;
    if (pml.velocity.z < pmp->jumpvel)
        pml.velocity.z = pmp->jumpvel;
}

/*
=============
PM_CheckSpecialMovement
=============
*/
static void PM_CheckSpecialMovement(void)
{
    if (pm->s->pm_time)
        return;

    pm->s->pm_flags &= ~PMF_ON_LADDER;

    // check for ladder
    vec3_t flatforward = { pml.forward.x, pml.forward.y };
    flatforward = Vec3_Normalize(flatforward);

    vec3_t spot = Vec3_MA(pml.origin, 1, flatforward);
    trace_t trace = PM_Trace(pml.origin, spot, pm->box, CONTENTS_LADDER);
    if ((trace.fraction < 1) && (trace.contents & CONTENTS_LADDER) && pm->waterlevel < WATER_WAIST)
        pm->s->pm_flags |= PMF_ON_LADDER;

    if (pm->s->gravity <= 0)
        return;

    // check for water jump
    // [Paril-KEX] don't try waterjump if we're moving against where we'll hop
    if (!(pm->cmd.buttons & BUTTON_JUMP) && pm->cmd.forwardmove <= 0)
        return;

    if (pm->waterlevel != WATER_WAIST)
        return;

    // [Paril-KEX]
    if (pm->watertype & CONTENTS_NO_WATERJUMP)
        return;

    // quick check that something is even blocking us forward
    vec3_t point = Vec3_MA(pml.origin, 40, flatforward);
    trace = PM_Trace(pml.origin, point, pm->box, MASK_SOLID);

    // we aren't blocked, or what we're blocked by is something we can walk up
    if (trace.fraction == 1.0f || trace.plane.normal.z >= 0.7f)
        return;

    // [Paril-KEX] improved waterjump
    vec3_t waterjump_vel = Vec3_Scale(flatforward, 50);
    waterjump_vel.z = 350;

    // simulate what would happen if we jumped out here, and
    // if we land on a dry spot we're good!
    // simulate 1 sec worth of movement
    float time = 0.1f;
    bool has_time = true;
    int steps = 10 * min(800 / pm->s->gravity, 5);

    vec3_t waterjump_origin = pml.origin;
    for (int i = 0; i < steps; i++) {
        waterjump_vel.z -= pm->s->gravity * time;

        if (waterjump_vel.z < 0)
            has_time = false;

        PM_StepSlideMove_Generic(&waterjump_origin, &waterjump_vel, time, pm->box,
                                 pm->s->clientnum, pml.clipmask, NULL, has_time, pm->trace);
    }

    // snap down to ground
    vec3_t down = waterjump_origin;
    down.z -= 2;
    trace = PM_Trace(waterjump_origin, down, pm->box, MASK_SOLID);

    // can't stand here
    if (trace.fraction == 1.0f || trace.plane.normal.z < 0.7f || trace.endpos.z < pml.origin.z)
        return;

    // we're currently standing on ground, and the snapped position is a step
    if (pm->groundentitynum != ENTITYNUM_NONE && fabsf(pml.origin.z - trace.endpos.z) <= STEPSIZE)
        return;

    water_level_t level;
    contents_t type;

    PM_GetWaterLevel(trace.endpos, &level, &type);

    // the water jump spot will be under water, so we're
    // probably hitting something weird that isn't important
    if (level >= WATER_WAIST)
        return;

    // valid waterjump!
    // jump out of water
    pml.velocity = Vec3_Scale(flatforward, 50);
    pml.velocity.z = 350;

    pm->s->pm_flags |= PMF_TIME_WATERJUMP;
    pm->s->pm_time = 2048;
}

/*
===============
PM_FlyMove
===============
*/
static void PM_FlyMove(void)
{
    float   speed, drop, friction, control, newspeed;
    float   currentspeed, addspeed, accelspeed;
    vec3_t  wishvel;
    vec3_t  wishdir;
    float   wishspeed;

    // friction

    speed = Vec3_Length(pml.velocity);
    if (speed < 1) {
        pml.velocity = vec3_origin;
    } else {
        drop = 0;

        friction = pmp->flyfriction; // extra friction
        control = speed < pmp->flystopspeed ? pmp->flystopspeed : speed;
        drop += control * friction * pml.frametime;

        // scale the velocity
        newspeed = speed - drop;
        if (newspeed < 0)
            newspeed = 0;
        newspeed /= speed;

        pml.velocity = Vec3_Scale(pml.velocity, newspeed);
    }

    // accelerate
    wishvel = Vec3_Mix(pml.forward, pml.right, pm->cmd.forwardmove, pm->cmd.sidemove);

    if (pm->cmd.buttons & BUTTON_JUMP)
        wishvel.z += (pmp->waterspeed * 0.5f);
    if (pm->cmd.buttons & BUTTON_CROUCH)
        wishvel.z -= (pmp->waterspeed * 0.5f);

    wishdir = Vec3_NormalizeLength(wishvel, &wishspeed);

    //
    // clamp to server defined max speed
    //
    if (wishspeed > pmp->maxspeed)
        wishspeed = pmp->maxspeed;

    // Paril: newer clients do this
    wishspeed *= 2;

    currentspeed = Vec3_Dot(pml.velocity, wishdir);
    addspeed = wishspeed - currentspeed;

    if (addspeed > 0) {
        accelspeed = pmp->accelerate * pml.frametime * wishspeed;
        if (accelspeed > addspeed)
            accelspeed = addspeed;

        pml.velocity = Vec3_MA(pml.velocity, accelspeed, wishdir);
    }

    // move
    if (pm->s->pm_type == PM_NOCLIP)
        pml.origin = Vec3_MA(pml.origin, pml.frametime, pml.velocity);
    else
        PM_StepSlideMove();
}

static void PM_SetDimensions(void)
{
    if (pm->s->pm_type == PM_NOCLIP) {
        pm->box = box3_origin;
        pm->s->viewheight = 22;
        return;
    }

    pm->box.mins.x = -16;
    pm->box.mins.y = -16;

    pm->box.maxs.x = 16;
    pm->box.maxs.y = 16;

    if (pm->s->pm_type == PM_GIB) {
        pm->box.mins.z = 0;
        pm->box.maxs.z = 16;
        pm->s->viewheight = 8;
        return;
    }

    pm->box.mins.z = -24;

    if ((pm->s->pm_flags & PMF_DUCKED) || pm->s->pm_type == PM_DEAD) {
        pm->box.maxs.z = 4;
        pm->s->viewheight = -2;
    } else {
        pm->box.maxs.z = 32;
        pm->s->viewheight = 22;
    }
}

static bool PM_AboveWater(void)
{
    vec3_t below = pml.origin;
    below.z -= 8;

    if (PM_Trace(pml.origin, below, pm->box, MASK_SOLID).fraction < 1.0f)
        return false;

    if (PM_Trace(pml.origin, below, pm->box, MASK_WATER).fraction < 1.0f)
        return true;

    return false;
}

/*
==============
PM_CheckDuck

Returns true if flags changed
==============
*/
static bool PM_CheckDuck(void)
{
    if (pm->s->pm_type >= PM_DEAD)
        return false;

    if ((pm->cmd.buttons & BUTTON_CROUCH) &&
        ((pm->s->pm_flags & PMF_ON_GROUND) || (pm->waterlevel <= WATER_FEET && !PM_AboveWater())) &&
        !(pm->s->pm_flags & PMF_ON_LADDER) && !PM_CrouchingDisabled()) {
        // duck
        if (!(pm->s->pm_flags & PMF_DUCKED)) {
            // check that duck won't be blocked
            box3_t check = pm->box;
            check.maxs.z = 4;
            if (!PM_Trace(pml.origin, pml.origin, check, pml.clipmask).allsolid) {
                pm->s->pm_flags |= PMF_DUCKED;
                return true;
            }
        }
    } else {
        // stand up if possible
        if (pm->s->pm_flags & PMF_DUCKED) {
            // try to stand up
            box3_t check = pm->box;
            check.maxs.z = 32;
            if (!PM_Trace(pml.origin, pml.origin, check, pml.clipmask).allsolid) {
                pm->s->pm_flags &= ~PMF_DUCKED;
                return true;
            }
        }
    }

    return false;
}

/*
==============
PM_DeadMove
==============
*/
static void PM_DeadMove(void)
{
    float forward;

    if (pm->groundentitynum == ENTITYNUM_NONE)
        return;

    // extra friction

    forward = Vec3_Length(pml.velocity);
    forward -= 20;
    if (forward <= 0) {
        pml.velocity = vec3_origin;
    } else {
        pml.velocity = Vec3_Normalize(pml.velocity);
        pml.velocity = Vec3_Scale(pml.velocity, forward);
    }
}

static bool PM_GoodPosition(void)
{
    if (pm->s->pm_type == PM_NOCLIP)
        return true;

    if (!PM_Trace(pml.origin, pml.origin, pm->box, pml.clipmask).startsolid)
        return true;

    if (PM_FixStuckObject_Generic(&pml.origin, pm->box, pm->s->clientnum, pml.clipmask, pm->trace) == NO_GOOD_POSITION)
        return false;

    return true;
}

/*
================
PM_SnapPosition

On exit, the origin will have a value that is in a valid position.
================
*/
static void PM_SnapPosition(void)
{
    pm->s->origin = pml.origin;
    pm->s->velocity = pml.velocity;
}

/*
================
PM_ClampAngles

Can be called externally to just set the angles
================
*/
void PM_ClampAngles(player_state_t *s, const usercmd_t *cmd)
{
    vec3_t angles = Vec3_FromAngles16(cmd->angles);

    if (s->pm_flags & PMF_TIME_TELEPORT) {
        s->viewangles.yaw = AngleMod(angles.yaw + s->delta_angles.yaw);
        s->viewangles.pitch = s->viewangles.roll = 0;
    } else {
        // circularly clamp the angles with deltas
        s->viewangles = Vec3_AngleMod(Vec3_Add(angles, s->delta_angles));

        // don't let the player look up or down more than 90 degrees
        s->viewangles.pitch = Q_clipf(s->viewangles.pitch, -89, 89);
    }
}

// calculate speed and cycle to be used for all cyclic walking effects
static void PM_SetBobTime(void)
{
    float xyspeed = truncf(Vec2_Length(Vec2_FromVec3(pml.velocity)));
    int bobmove;

    if (xyspeed < 5) {
        // start at beginning of cycle again
        if (pm->s->bobtime) {
            if (pm->s->bobtime < 128) {
                pm->s->bobtime -= 160 * pml.frametime + 0.5f;
                if (pm->s->bobtime < 0)
                    pm->s->bobtime = 0;
            } else {
                pm->s->bobtime += 160 * pml.frametime + 0.5f;
                if (pm->s->bobtime > 256)
                    pm->s->bobtime = 256;
            }
            pm->s->bobtime &= 255;
        }
    } else if (pm->s->pm_flags & PMF_ON_GROUND) {
        // so bobbing only cycles when on ground
        if (xyspeed > 210)
            bobmove = 320;
        else if (xyspeed > 100)
            bobmove = 160;
        else
            bobmove = 80;
        if (pm->s->pm_flags & PMF_DUCKED)
            bobmove = min(bobmove * 4, 320);
        bobmove = bobmove * pml.frametime + 0.5f;
        pm->s->bobtime = (pm->s->bobtime + bobmove) & 255;
    }

    if (xyspeed > 225)
        pm->step_sound = true;
}

/*
================
BG_Pmove

Can be called by either the server or the client
================
*/
void BG_Pmove(pmove_t *pmove)
{
    pm = pmove;

    if (pm_config.physics_flags & PHYSICS_PSX_SCALE)
        pmp = &pmp_psxscale;
    else
        pmp = &pmp_default;

    // clear results
    pm->touch.num = 0;
    pm->groundentitynum = ENTITYNUM_NONE;
    pm->watertype = CONTENTS_NONE;
    pm->waterlevel = WATER_NONE;
    pm->jump_sound = false;
    pm->step_sound = false;
    pm->step_height = 0;
    pm->impact_delta = 0;

    // clear all pmove local vars
    pml = (pml_t){ 0 };

    pml.origin = pm->s->origin;
    pml.velocity = pm->s->velocity;

    pml.frametime = pm->cmd.msec * 0.001f;
    pml.clipmask = PM_TraceMask();

    PM_ClampAngles(pm->s, &pm->cmd);
    AngleVectors(pm->s->viewangles, &pml.forward, &pml.right, &pml.up);

    if (pm->s->pm_type == PM_SPECTATOR || pm->s->pm_type == PM_NOCLIP) {
        pm->s->pm_flags = PMF_NONE;
        pm->s->pm_time = 0;
        PM_SetDimensions();
        if (PM_GoodPosition()) {
            PM_FlyMove();
            PM_SnapPosition();
        }
        return;
    }

    if (pm->s->pm_type >= PM_DEAD) {
        pm->cmd.forwardmove = 0;
        pm->cmd.sidemove = 0;
        pm->cmd.buttons &= ~(BUTTON_JUMP | BUTTON_CROUCH);
    }

    if (pm->s->pm_type == PM_FREEZE)
        return; // no movement at all

    // set mins, maxs, and viewheight
    PM_SetDimensions();

    // check if we are stuck and try to fix
    if (!PM_GoodPosition())
        return;

    // categorize for ducking
    PM_CategorizePosition();

    // set groundentity, watertype, and waterlevel
    if (PM_CheckDuck()) {
        PM_SetDimensions();
        PM_CategorizePosition();
    }

    if (pm->s->pm_type == PM_DEAD)
        PM_DeadMove();

    PM_CheckSpecialMovement();

    // drop timing counter
    if (pm->s->pm_time) {
        if (pm->cmd.msec >= pm->s->pm_time) {
            pm->s->pm_flags &= ~(PMF_TIME_WATERJUMP | PMF_TIME_LAND | PMF_TIME_TELEPORT | PMF_TIME_TRICK);
            pm->s->pm_time = 0;
        } else
            pm->s->pm_time -= pm->cmd.msec;
    }

    if (pm->s->pm_flags & PMF_TIME_TELEPORT) {
        // teleport pause stays exactly in place
    } else if (pm->s->pm_flags & PMF_TIME_WATERJUMP) {
        // waterjump has no control, but falls
        pml.velocity.z -= pm->s->gravity * pml.frametime;
        if (pml.velocity.z < 0) {
            // cancel as soon as we are falling down again
            pm->s->pm_flags &= ~(PMF_TIME_WATERJUMP | PMF_TIME_LAND | PMF_TIME_TELEPORT | PMF_TIME_TRICK);
            pm->s->pm_time = 0;
        }

        PM_StepSlideMove();
    } else {
        PM_CheckJump();

        PM_Friction();

        if (pm->waterlevel >= WATER_WAIST)
            PM_WaterMove();
        else {
            vec3_t angles = pm->s->viewangles;
            angles.pitch /= 3;

            AngleVectors(angles, &pml.forward, &pml.right, &pml.up);

            PM_AirMove();
        }
    }

    // set groundentity, watertype, and waterlevel for final spot
    PM_CategorizePosition();

    if (pm->s->pm_type == PM_NORMAL)
        PM_SetBobTime();

    // trick jump
    if (pm->s->pm_flags & PMF_TIME_TRICK)
        PM_CheckJump();

    PM_SnapPosition();
}
