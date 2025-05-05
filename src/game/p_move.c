// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"
#include <float.h>

#define NUM_SIDE_CHECKS 6

typedef struct {
    int8_t normal[3];
    int8_t mins[3];
    int8_t maxs[3];
} side_check_t;

typedef struct {
    vec3_t origin;
    float dist;
} good_position_t;

static const side_check_t side_checks[NUM_SIDE_CHECKS] = {
    { { 0, 0, 1 }, {-1,-1, 0 }, { 1, 1, 0 } },
    { { 0, 0,-1 }, {-1,-1, 0 }, { 1, 1, 0 } },
    { { 1, 0, 0 }, { 0,-1,-1 }, { 0, 1, 1 } },
    { {-1, 0, 0 }, { 0,-1,-1 }, { 0, 1, 1 } },
    { { 0, 1, 0 }, {-1, 0,-1 }, { 1, 0, 1 } },
    { { 0,-1, 0 }, {-1, 0,-1 }, { 1, 0, 1 } },
};

// [Paril-KEX] generic code to detect & fix a stuck object
stuck_result_t G_FixStuckObject_Generic(vec3_t origin, const vec3_t own_mins, const vec3_t own_maxs,
                                        edict_t *ignore, contents_t mask, trace_func_t trace_func)
{
    trace_t tr;

    trace_func(&tr, origin, own_mins, own_maxs, origin, ignore, mask);
    if (!tr.startsolid)
        return GOOD_POSITION;

    good_position_t good_positions[NUM_SIDE_CHECKS];
    int num_good_positions = 0;

    for (int sn = 0; sn < NUM_SIDE_CHECKS; sn++) {
        const side_check_t *side = &side_checks[sn];

        vec3_t start, mins = { 0 }, maxs = { 0 };
        VectorCopy(origin, start);

        for (int n = 0; n < 3; n++) {
            if (side->normal[n] < 0)
                start[n] += own_mins[n];
            else if (side->normal[n] > 0)
                start[n] += own_maxs[n];

            if (side->mins[n] == -1)
                mins[n] = own_mins[n];
            else if (side->mins[n] == 1)
                mins[n] = own_maxs[n];

            if (side->maxs[n] == -1)
                maxs[n] = own_mins[n];
            else if (side->maxs[n] == 1)
                maxs[n] = own_maxs[n];
        }

        int needed_epsilon_fix = -1;
        int needed_epsilon_dir = 0;

        trace_func(&tr, start, mins, maxs, start, ignore, mask);

        if (tr.startsolid) {
            for (int e = 0; e < 3; e++) {
                if (side->normal[e] != 0)
                    continue;

                vec3_t ep_start;
                VectorCopy(start, ep_start);
                ep_start[e] += 1;

                trace_func(&tr, ep_start, mins, maxs, ep_start, ignore, mask);

                if (!tr.startsolid) {
                    VectorCopy(ep_start, start);
                    needed_epsilon_fix = e;
                    needed_epsilon_dir = 1;
                    break;
                }

                ep_start[e] -= 2;
                trace_func(&tr, ep_start, mins, maxs, ep_start, ignore, mask);

                if (!tr.startsolid) {
                    VectorCopy(ep_start, start);
                    needed_epsilon_fix = e;
                    needed_epsilon_dir = -1;
                    break;
                }
            }
        }

        // no good
        if (tr.startsolid)
            continue;

        vec3_t opposite_start;
        VectorCopy(origin, opposite_start);

        const side_check_t *other_side = &side_checks[sn ^ 1];

        for (int n = 0; n < 3; n++) {
            if (other_side->normal[n] < 0)
                opposite_start[n] += own_mins[n];
            else if (other_side->normal[n] > 0)
                opposite_start[n] += own_maxs[n];
        }

        if (needed_epsilon_fix >= 0)
            opposite_start[needed_epsilon_fix] += needed_epsilon_dir;

        // potentially a good side; start from our center, push back to the opposite side
        // to find how much clearance we have
        trace_func(&tr, start, mins, maxs, opposite_start, ignore, mask);

        // ???
        if (tr.startsolid)
            continue;

        // check the delta
        vec3_t end;
        VectorCopy(tr.endpos, end);

        // push us very slightly away from the wall
        VectorMA(end, 0.125f, side->normal, end);

        // calculate delta
        vec3_t delta, new_origin;
        VectorSubtract(end, opposite_start, delta);
        VectorAdd(origin, delta, new_origin);

        if (needed_epsilon_fix >= 0)
            new_origin[needed_epsilon_fix] += needed_epsilon_dir;

        trace_func(&tr, new_origin, own_mins, own_maxs, new_origin, ignore, mask);

        // bad
        if (tr.startsolid)
            continue;

        good_position_t *good = &good_positions[num_good_positions++];
        VectorCopy(new_origin, good->origin);
        good->dist = VectorLengthSquared(delta);
    }

    if (num_good_positions) {
        float best_dist = FLT_MAX;
        int best = 0;

        for (int i = 0; i < num_good_positions; i++) {
            good_position_t *good = &good_positions[i];
            if (good->dist < best_dist) {
                best_dist = good->dist;
                best = i;
            }
        }

        VectorCopy(good_positions[best].origin, origin);
        return STUCK_FIXED;
    }

    return NO_GOOD_POSITION;
}

/*
==================
PM_ClipVelocity

Slide off of the impacting object
returns the blocked flags (1 = floor, 2 = step / wall)
==================
*/
static void PM_ClipVelocity(const vec3_t in, const vec3_t normal, vec3_t out, float overbounce)
{
    float backoff;
    float change;
    int   i;

    backoff = DotProduct(in, normal) * overbounce;

    for (i = 0; i < 3; i++) {
        change = normal[i] * backoff;
        out[i] = in[i] - change;
        if (out[i] > -STOP_EPSILON && out[i] < STOP_EPSILON)
            out[i] = 0;
    }
}

static void PM_RecordTrace(touch_list_t *touch, const trace_t *tr)
{
    if (!touch || touch->num == MAXTOUCH)
        return;

    for (int i = 0; i < touch->num; i++)
        if (touch->traces[i].ent == tr->ent)
            return;

    touch->traces[touch->num++] = *tr;
}

/*
==================
PM_StepSlideMove

Each intersection will try to step over the obstruction instead of
sliding along it.

Returns a new origin, velocity, and contact entity
Does not modify any world state?
==================
*/

#define MIN_STEP_NORMAL     0.7f // can't step up onto very steep slopes
#define MAX_CLIP_PLANES     5

// [Paril-KEX] made generic so you can run this without needing a pml/pm
void PM_StepSlideMove_Generic(vec3_t origin, vec3_t velocity, float frametime, const vec3_t mins, const vec3_t maxs,
                              edict_t *passent, contents_t mask, touch_list_t *touch, bool has_time, trace_func_t trace_func)
{
    vec3_t  dir;
    float   d;
    int     numplanes;
    vec3_t  planes[MAX_CLIP_PLANES];
    vec3_t  primal_velocity;
    int     i, j;
    trace_t trace;
    vec3_t  end;
    float   time_left;

    if (touch)
        touch->num = 0;

    VectorCopy(velocity, primal_velocity);
    numplanes = 0;

    time_left = frametime;

    for (int bumpcount = 0; bumpcount < 4; bumpcount++) {
        VectorMA(origin, time_left, velocity, end);

        trace_func(&trace, origin, mins, maxs, end, passent, mask);
        if (trace.allsolid) {
            // entity is trapped in another solid
            velocity[2] = 0; // don't build up falling damage

            // save entity for contact
            PM_RecordTrace(touch, &trace);
            return;
        }

        // [Paril-KEX] experimental attempt to fix stray collisions on curved
        // surfaces; easiest to see on q2dm1 by running/jumping against the sides
        // of the curved map.
        if (trace.surface2) {
            vec3_t clipped_a, clipped_b;
            PM_ClipVelocity(velocity, trace.plane.normal, clipped_a, 1.01f);
            PM_ClipVelocity(velocity, trace.plane2.normal, clipped_b, 1.01f);

            bool better = false;

            for (int i = 0; i < 3; i++) {
                if (fabsf(clipped_a[i]) < fabsf(clipped_b[i])) {
                    better = true;
                    break;
                }
            }

            if (better) {
                trace.plane = trace.plane2;
                trace.surface = trace.surface2;
            }
        }

        if (trace.fraction > 0) {
            // actually covered some distance
            VectorCopy(trace.endpos, origin);
            numplanes = 0;
        }

        if (trace.fraction == 1)
            break; // moved the entire distance

        // save entity for contact
        PM_RecordTrace(touch, &trace);

        time_left -= time_left * trace.fraction;

        // slide along this plane
        if (numplanes >= MAX_CLIP_PLANES) {
            // this shouldn't really happen
            VectorClear(velocity);
            break;
        }

        //
        // if this is the same plane we hit before, nudge origin
        // out along it, which fixes some epsilon issues with
        // non-axial planes (xswamp, q2dm1 sometimes...)
        //
        for (i = 0; i < numplanes; i++) {
            if (DotProduct(trace.plane.normal, planes[i]) > 0.99f) {
                origin[0] += trace.plane.normal[0] * 0.01f;
                origin[1] += trace.plane.normal[1] * 0.01f;
                G_FixStuckObject_Generic(origin, mins, maxs, passent, mask, trace_func);
                break;
            }
        }

        if (i < numplanes)
            continue;

        VectorCopy(trace.plane.normal, planes[numplanes]);
        numplanes++;

        //
        // modify original_velocity so it parallels all of the clip planes
        //
        for (i = 0; i < numplanes; i++) {
            PM_ClipVelocity(velocity, planes[i], velocity, 1.01f);
            for (j = 0; j < numplanes; j++)
                if ((j != i) && DotProduct(velocity, planes[j]) < 0)
                    break; // not ok
            if (j == numplanes)
                break;
        }

        if (i != numplanes) {
            // go along this plane
        } else {
            // go along the crease
            if (numplanes != 2) {
                VectorClear(velocity);
                break;
            }
            CrossProduct(planes[0], planes[1], dir);
            d = DotProduct(dir, velocity);
            VectorScale(dir, d, velocity);
        }

        //
        // if velocity is against the original velocity, stop dead
        // to avoid tiny oscillations in sloping corners
        //
        if (DotProduct(velocity, primal_velocity) <= 0) {
            VectorClear(velocity);
            break;
        }
    }

    if (has_time)
        VectorCopy(primal_velocity, velocity);
}


// all of the locals will be zeroed before each
// pmove, just to make damn sure we don't have
// any differences when running on client or server

typedef struct {
    vec3_t origin;   // full float precision
    vec3_t velocity; // full float precision

    vec3_t forward, right, up;
    float  frametime;

    csurface_t *groundsurface;
    int         groundcontents;

    vec3_t previous_origin;
    vec3_t start_velocity;

    contents_t clipmask;
} pml_t;

pm_config_t pm_config;

static pmove_t *pm;
static pml_t    pml;

// movement parameters
static const float pm_stopspeed = 100;
static const float pm_maxspeed = 300;
static const float pm_duckspeed = 100;
static const float pm_accelerate = 10;
static const float pm_wateraccelerate = 10;
static const float pm_friction = 6;
static const float pm_waterfriction = 1;
static const float pm_waterspeed = 400;
static const float pm_laddermod = 0.5f;

// walking up a step should kill some velocity

static contents_t PM_TraceMask(void)
{
    contents_t mask = CONTENTS_NONE;

    if (pm->s.pm_type == PM_DEAD || pm->s.pm_type == PM_GIB)
        mask = MASK_DEADSOLID;
    else if (pm->s.pm_type == PM_SPECTATOR)
        mask = MASK_SOLID;
    else
        mask = MASK_PLAYERSOLID;

    if (pm->s.pm_flags & PMF_IGNORE_PLAYER_COLLISION)
        mask &= ~CONTENTS_PLAYER;

    return mask;
}

static void PM_Trace(trace_t *tr, const vec3_t start, const vec3_t mins,
                     const vec3_t maxs, const vec3_t end, contents_t mask)
{
    if (pm->s.pm_type == PM_SPECTATOR)
        pm->clip(tr, start, mins, maxs, end, MASK_SOLID);
    else
        pm->trace(tr, start, mins, maxs, end, pm->player, mask);
}

static inline void PM_StepSlideMove_(void)
{
    PM_StepSlideMove_Generic(pml.origin, pml.velocity, pml.frametime, pm->mins, pm->maxs,
                             pm->player, pml.clipmask, &pm->touch, pm->s.pm_time, pm->trace);
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

    VectorCopy(pml.origin, start_o);
    VectorCopy(pml.velocity, start_v);

    PM_StepSlideMove_();

    VectorCopy(pml.origin, down_o);
    VectorCopy(pml.velocity, down_v);

    VectorCopy(start_o, up);
    up[2] += STEPSIZE;

    PM_Trace(&trace, start_o, pm->mins, pm->maxs, up, pml.clipmask);
    if (trace.allsolid)
        return; // can't step up

    float step_size = trace.endpos[2] - start_o[2];

    // try sliding above
    VectorCopy(trace.endpos, pml.origin);
    VectorCopy(start_v, pml.velocity);

    PM_StepSlideMove_();

    // push down the final amount
    VectorCopy(pml.origin, down);
    down[2] -= step_size;

    // [Paril-KEX] jitspoe suggestion for stair clip fix; store
    // the old down position, and pick a better spot for downwards
    // trace if the start origin's Z position is lower than the down end pt.
    vec3_t original_down;
    VectorCopy(down, original_down);

    if (start_o[2] < down[2])
        down[2] = start_o[2] - 1.0f;

    PM_Trace(&trace, pml.origin, pm->mins, pm->maxs, down, pml.clipmask);
    if (!trace.allsolid) {
        // [Paril-KEX] from above, do the proper trace now
        trace_t real_trace;
        PM_Trace(&real_trace, pml.origin, pm->mins, pm->maxs, original_down, pml.clipmask);
        VectorCopy(real_trace.endpos, pml.origin);

        // only an upwards jump is a stair clip
        if (pml.velocity[2] > 0)
            pm->step_clip = true;
    }

    VectorCopy(pml.origin, up);

    // decide which one went farther
    down_dist = (down_o[0] - start_o[0]) * (down_o[0] - start_o[0]) + (down_o[1] - start_o[1]) * (down_o[1] - start_o[1]);
    up_dist = (up[0] - start_o[0]) * (up[0] - start_o[0]) + (up[1] - start_o[1]) * (up[1] - start_o[1]);

    if (down_dist > up_dist || trace.plane.normal[2] < MIN_STEP_NORMAL) {
        VectorCopy(down_o, pml.origin);
        VectorCopy(down_v, pml.velocity);
    } else {
        //!! Special case
        // if we were walking along a plane, then we need to copy the Z over
        pml.velocity[2] = down_v[2];
    }

    // Paril: step down stairs/slopes
    if ((pm->s.pm_flags & PMF_ON_GROUND) && !(pm->s.pm_flags & PMF_ON_LADDER) &&
        (pm->waterlevel < WATER_WAIST || (!(pm->cmd.buttons & BUTTON_JUMP) && pml.velocity[2] <= 0))) {
        VectorCopy(pml.origin, down);
        down[2] -= STEPSIZE;
        PM_Trace(&trace, pml.origin, pm->mins, pm->maxs, down, pml.clipmask);
        if (trace.fraction < 1.0f)
            VectorCopy(trace.endpos, pml.origin);
    }
}

/*
==================
PM_Friction

Handles both ground friction and water friction
==================
*/
static void PM_Friction(void)
{
    float *vel;
    float  speed, newspeed, control;
    float  friction;
    float  drop;

    vel = pml.velocity;

    speed = sqrtf(vel[0] * vel[0] + vel[1] * vel[1] + vel[2] * vel[2]);
    if (speed < 1) {
        vel[0] = 0;
        vel[1] = 0;
        return;
    }

    drop = 0;

    // apply ground friction
    if ((pm->groundentity && pml.groundsurface && !(pml.groundsurface->flags & SURF_SLICK)) || (pm->s.pm_flags & PMF_ON_LADDER)) {
        friction = pm_friction;
        control = speed < pm_stopspeed ? pm_stopspeed : speed;
        drop += control * friction * pml.frametime;
    }

    // apply water friction
    if (pm->waterlevel && !(pm->s.pm_flags & PMF_ON_LADDER))
        drop += speed * pm_waterfriction * pm->waterlevel * pml.frametime;

    // scale the velocity
    newspeed = speed - drop;
    if (newspeed < 0)
        newspeed = 0;
    newspeed /= speed;

    VectorScale(vel, newspeed, vel);
}

/*
==============
PM_Accelerate

Handles user intended acceleration
==============
*/
static void PM_Accelerate(const vec3_t wishdir, float wishspeed, float accel)
{
    int   i;
    float addspeed, accelspeed, currentspeed;

    currentspeed = DotProduct(pml.velocity, wishdir);
    addspeed = wishspeed - currentspeed;
    if (addspeed <= 0)
        return;
    accelspeed = accel * pml.frametime * wishspeed;
    if (accelspeed > addspeed)
        accelspeed = addspeed;

    for (i = 0; i < 3; i++)
        pml.velocity[i] += accelspeed * wishdir[i];
}

static void PM_AirAccelerate(const vec3_t wishdir, float wishspeed, float accel)
{
    int   i;
    float addspeed, accelspeed, currentspeed, wishspd = wishspeed;

    if (wishspd > 30)
        wishspd = 30;
    currentspeed = DotProduct(pml.velocity, wishdir);
    addspeed = wishspd - currentspeed;
    if (addspeed <= 0)
        return;
    accelspeed = accel * wishspeed * pml.frametime;
    if (accelspeed > addspeed)
        accelspeed = addspeed;

    for (i = 0; i < 3; i++)
        pml.velocity[i] += accelspeed * wishdir[i];
}

/*
=============
PM_AddCurrents
=============
*/
static void PM_AddCurrents(vec3_t wishvel)
{
    vec3_t v;
    float  s;

    //
    // account for ladders
    //

    if (pm->s.pm_flags & PMF_ON_LADDER) {
        if (pm->cmd.buttons & (BUTTON_JUMP | BUTTON_CROUCH)) {
            // [Paril-KEX]: if we're underwater, use full speed on ladders
            float ladder_speed = pm->waterlevel >= WATER_WAIST ? pm_maxspeed : 200;

            if (pm->cmd.buttons & BUTTON_JUMP)
                wishvel[2] = ladder_speed;
            else if (pm->cmd.buttons & BUTTON_CROUCH)
                wishvel[2] = -ladder_speed;
        } else if (pm->cmd.forwardmove) {
            // [Paril-KEX] clamp the speed a bit so we're not too fast
            float ladder_speed = Q_clipf(pm->cmd.forwardmove, -200, 200);

            if (pm->cmd.forwardmove > 0) {
                if (pm->viewangles[PITCH] < 15)
                    wishvel[2] = ladder_speed;
                else
                    wishvel[2] = -ladder_speed;
            // [Paril-KEX] allow using "back" arrow to go down on ladder
            } else if (pm->cmd.forwardmove < 0) {
                // if we haven't touched ground yet, remove x/y so we don't
                // slide off of the ladder
                if (!pm->groundentity)
                    wishvel[0] = wishvel[1] = 0;

                wishvel[2] = ladder_speed;
            }
        } else
            wishvel[2] = 0;

        // limit horizontal speed when on a ladder
        // [Paril-KEX] unless we're on the ground
        if (!pm->groundentity) {
            // [Paril-KEX] instead of left/right not doing anything,
            // have them move you perpendicular to the ladder plane
            if (pm->cmd.sidemove) {
                // clamp side speed so it's not jarring...
                float ladder_speed = Q_clipf(pm->cmd.sidemove, -150, 150);

                if (pm->waterlevel < WATER_WAIST)
                    ladder_speed *= pm_laddermod;

                // check for ladder
                vec3_t flatforward, spot;
                flatforward[0] = pml.forward[0];
                flatforward[1] = pml.forward[1];
                flatforward[2] = 0;
                VectorNormalize(flatforward);

                VectorMA(pml.origin, 1, flatforward, spot);
                trace_t trace;
                PM_Trace(&trace, pml.origin, pm->mins, pm->maxs, spot, CONTENTS_LADDER);

                if (trace.fraction != 1.0f && (trace.contents & CONTENTS_LADDER)) {
                    vec3_t right = {
                        trace.plane.normal[1],
                        -trace.plane.normal[0],
                    };

                    wishvel[0] = wishvel[1] = 0;
                    VectorMA(wishvel, -ladder_speed, right, wishvel);
                }
            } else {
                wishvel[0] = Q_clipf(wishvel[0], -25, 25);
                wishvel[1] = Q_clipf(wishvel[1], -25, 25);
            }
        }
    }

    //
    // add water currents
    //

    if (pm->watertype & MASK_CURRENT) {
        VectorClear(v);

        if (pm->watertype & CONTENTS_CURRENT_0)
            v[0] += 1;
        if (pm->watertype & CONTENTS_CURRENT_90)
            v[1] += 1;
        if (pm->watertype & CONTENTS_CURRENT_180)
            v[0] -= 1;
        if (pm->watertype & CONTENTS_CURRENT_270)
            v[1] -= 1;
        if (pm->watertype & CONTENTS_CURRENT_UP)
            v[2] += 1;
        if (pm->watertype & CONTENTS_CURRENT_DOWN)
            v[2] -= 1;

        s = pm_waterspeed;
        if ((pm->waterlevel == WATER_FEET) && (pm->groundentity))
            s /= 2;

        VectorMA(wishvel, s, v, wishvel);
    }

    //
    // add conveyor belt velocities
    //

    if (pm->groundentity) {
        VectorClear(v);

        if (pml.groundcontents & CONTENTS_CURRENT_0)
            v[0] += 1;
        if (pml.groundcontents & CONTENTS_CURRENT_90)
            v[1] += 1;
        if (pml.groundcontents & CONTENTS_CURRENT_180)
            v[0] -= 1;
        if (pml.groundcontents & CONTENTS_CURRENT_270)
            v[1] -= 1;
        if (pml.groundcontents & CONTENTS_CURRENT_UP)
            v[2] += 1;
        if (pml.groundcontents & CONTENTS_CURRENT_DOWN)
            v[2] -= 1;

        VectorMA(wishvel, 100, v, wishvel);
    }
}

/*
===================
PM_WaterMove
===================
*/
static void PM_WaterMove(void)
{
    int    i;
    vec3_t wishvel;
    float  wishspeed;
    vec3_t wishdir;

    //
    // user intentions
    //
    for (i = 0; i < 3; i++)
        wishvel[i] = pml.forward[i] * pm->cmd.forwardmove + pml.right[i] * pm->cmd.sidemove;

    if (!pm->cmd.forwardmove && !pm->cmd.sidemove &&
        !(pm->cmd.buttons & (BUTTON_JUMP | BUTTON_CROUCH))) {
        if (!pm->groundentity)
            wishvel[2] -= 60; // drift towards bottom
    } else {
        if (pm->cmd.buttons & BUTTON_CROUCH)
            wishvel[2] -= pm_waterspeed * 0.5f;
        else if (pm->cmd.buttons & BUTTON_JUMP)
            wishvel[2] += pm_waterspeed * 0.5f;
    }

    PM_AddCurrents(wishvel);

    wishspeed = VectorNormalize2(wishvel, wishdir);

    if (wishspeed > pm_maxspeed)
        wishspeed = pm_maxspeed;
    wishspeed *= 0.5f;

    if ((pm->s.pm_flags & PMF_DUCKED) && wishspeed > pm_duckspeed)
        wishspeed = pm_duckspeed;

    PM_Accelerate(wishdir, wishspeed, pm_wateraccelerate);

    PM_StepSlideMove();
}

/*
===================
PM_AirMove
===================
*/
static void PM_AirMove(void)
{
    int    i;
    vec3_t wishvel;
    float  fmove, smove;
    vec3_t wishdir;
    float  wishspeed;
    float  maxspeed;

    fmove = pm->cmd.forwardmove;
    smove = pm->cmd.sidemove;

    for (i = 0; i < 2; i++)
        wishvel[i] = pml.forward[i] * fmove + pml.right[i] * smove;
    wishvel[2] = 0;

    PM_AddCurrents(wishvel);

    wishspeed = VectorNormalize2(wishvel, wishdir);

    //
    // clamp to server defined max speed
    //
    maxspeed = (pm->s.pm_flags & PMF_DUCKED) ? pm_duckspeed : pm_maxspeed;

    if (wishspeed > maxspeed)
        wishspeed = maxspeed;

    if (pm->s.pm_flags & PMF_ON_LADDER) {
        PM_Accelerate(wishdir, wishspeed, pm_accelerate);
        if (!wishvel[2]) {
            if (pml.velocity[2] > 0) {
                pml.velocity[2] -= pm->s.gravity * pml.frametime;
                if (pml.velocity[2] < 0)
                    pml.velocity[2] = 0;
            } else {
                pml.velocity[2] += pm->s.gravity * pml.frametime;
                if (pml.velocity[2] > 0)
                    pml.velocity[2] = 0;
            }
        }
        PM_StepSlideMove();
    } else if (pm->groundentity) {
        // walking on ground
        pml.velocity[2] = 0; //!!! this is before the accel
        PM_Accelerate(wishdir, wishspeed, pm_accelerate);

        // PGM  -- fix for negative trigger_gravity fields
        if (pm->s.gravity > 0)
            pml.velocity[2] = 0;
        else
            pml.velocity[2] -= pm->s.gravity * pml.frametime;
        // PGM

        if (!pml.velocity[0] && !pml.velocity[1])
            return;
        PM_StepSlideMove();
    } else {
        // not on ground, so little effect on velocity
        if (pm_config.airaccel)
            PM_AirAccelerate(wishdir, wishspeed, pm_config.airaccel);
        else
            PM_Accelerate(wishdir, wishspeed, 1);

        // add gravity
        if (pm->s.pm_type != PM_GRAPPLE)
            pml.velocity[2] -= pm->s.gravity * pml.frametime;

        PM_StepSlideMove();
    }
}

static void PM_GetWaterLevel(const vec3_t position, water_level_t *level, contents_t *type)
{
    //
    // get waterlevel, accounting for ducking
    //
    *level = WATER_NONE;
    *type = CONTENTS_NONE;

    int sample2 = pm->s.viewheight - pm->mins[2];
    int sample1 = sample2 / 2;

    vec3_t point;
    VectorCopy(position, point);
    point[2] += pm->mins[2] + 1;

    contents_t cont = pm->pointcontents(point);

    if (cont & MASK_WATER) {
        *type = cont;
        *level = WATER_FEET;
        point[2] = pml.origin[2] + pm->mins[2] + sample1;
        cont = pm->pointcontents(point);
        if (cont & MASK_WATER) {
            *level = WATER_WAIST;
            point[2] = pml.origin[2] + pm->mins[2] + sample2;
            cont = pm->pointcontents(point);
            if (cont & MASK_WATER)
                *level = WATER_UNDER;
        }
    }
}

/*
=============
PM_CategorizePosition
=============
*/
static void PM_CategorizePosition(void)
{
    vec3_t     point;
    trace_t    trace;

    // if the player hull point one unit down is solid, the player
    // is on ground

    // see if standing on something solid
    point[0] = pml.origin[0];
    point[1] = pml.origin[1];
    point[2] = pml.origin[2] - 0.25f;

    if (pml.velocity[2] > 180 || pm->s.pm_type == PM_GRAPPLE) { //!!ZOID changed from 100 to 180 (ramp accel)
        pm->s.pm_flags &= ~PMF_ON_GROUND;
        pm->groundentity = NULL;
    } else {
        PM_Trace(&trace, pml.origin, pm->mins, pm->maxs, point, pml.clipmask);
        pm->groundplane = trace.plane;
        pml.groundsurface = trace.surface;
        pml.groundcontents = trace.contents;

        // [Paril-KEX] to attempt to fix edge cases where you get stuck
        // wedged between a slope and a wall (which is irrecoverable
        // most of the time), we'll allow the player to "stand" on
        // slopes if they are right up against a wall
        bool slanted_ground = !trace.startsolid && trace.fraction < 1.0f && trace.plane.normal[2] < 0.7f;

        if (slanted_ground) {
            VectorAdd(pml.origin, trace.plane.normal, point);
            trace_t slant;
            PM_Trace(&slant, pml.origin, pm->mins, pm->maxs, point, pml.clipmask);

            if (slant.fraction < 1.0f && !slant.startsolid && slant.plane.normal[2] > -0.01f)
                slanted_ground = false;
        }

        if (trace.fraction == 1.0f || slanted_ground) {
            pm->groundentity = NULL;
            pm->s.pm_flags &= ~PMF_ON_GROUND;
        } else {
            pm->groundentity = trace.ent;

            // hitting solid ground will end a waterjump
            if (pm->s.pm_flags & PMF_TIME_WATERJUMP) {
                pm->s.pm_flags &= ~(PMF_TIME_WATERJUMP | PMF_TIME_LAND | PMF_TIME_TELEPORT | PMF_TIME_TRICK);
                pm->s.pm_time = 0;
            }

            if (!(pm->s.pm_flags & PMF_ON_GROUND)) {
                // just hit the ground

                // [Paril-KEX]
                if (!pm_config.n64_physics && pml.velocity[2] >= 100.0f && pm->groundplane.normal[2] >= 0.9f && !(pm->s.pm_flags & PMF_DUCKED)) {
                    pm->s.pm_flags |= PMF_TIME_TRICK;
                    pm->s.pm_time = 64;
                }

                // [Paril-KEX] calculate impact delta; this also fixes triple jumping
                vec3_t clipped_velocity;
                PM_ClipVelocity(pml.velocity, pm->groundplane.normal, clipped_velocity, 1.01f);

                pm->impact_delta = pml.start_velocity[2] - clipped_velocity[2];

                pm->s.pm_flags |= PMF_ON_GROUND;

                if (pm_config.n64_physics || (pm->s.pm_flags & PMF_DUCKED)) {
                    pm->s.pm_flags |= PMF_TIME_LAND;
                    pm->s.pm_time = 128;
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
    if (pm->s.pm_flags & PMF_TIME_LAND) {
        // hasn't been long enough since landing to jump again
        return;
    }

    if (!(pm->cmd.buttons & BUTTON_JUMP)) {
        // not holding jump
        pm->s.pm_flags &= ~PMF_JUMP_HELD;
        return;
    }

    // must wait for jump to be released
    if (pm->s.pm_flags & PMF_JUMP_HELD)
        return;

    if (pm->s.pm_type == PM_DEAD)
        return;

    if (pm->waterlevel >= WATER_WAIST) {
        // swimming, not jumping
        pm->groundentity = NULL;
        return;
    }

    if (pm->groundentity == NULL)
        return; // in air, so no effect

    pm->s.pm_flags |= PMF_JUMP_HELD;
    pm->jump_sound = true;
    pm->groundentity = NULL;
    pm->s.pm_flags &= ~PMF_ON_GROUND;

    float jump_height = 270.0f;

    pml.velocity[2] += jump_height;
    if (pml.velocity[2] < jump_height)
        pml.velocity[2] = jump_height;
}

/*
=============
PM_CheckSpecialMovement
=============
*/
static void PM_CheckSpecialMovement(void)
{
    vec3_t  spot;
    vec3_t  flatforward;
    trace_t trace;

    if (pm->s.pm_time)
        return;

    pm->s.pm_flags &= ~PMF_ON_LADDER;

    // check for ladder
    flatforward[0] = pml.forward[0];
    flatforward[1] = pml.forward[1];
    flatforward[2] = 0;
    VectorNormalize(flatforward);

    VectorMA(pml.origin, 1, flatforward, spot);
    PM_Trace(&trace, pml.origin, pm->mins, pm->maxs, spot, CONTENTS_LADDER);
    if ((trace.fraction < 1) && (trace.contents & CONTENTS_LADDER) && pm->waterlevel < WATER_WAIST)
        pm->s.pm_flags |= PMF_ON_LADDER;

    if (!pm->s.gravity)
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
    vec3_t point;
    VectorMA(pml.origin, 40, flatforward, point);
    PM_Trace(&trace, pml.origin, pm->mins, pm->maxs, point, MASK_SOLID);

    // we aren't blocked, or what we're blocked by is something we can walk up
    if (trace.fraction == 1.0f || trace.plane.normal[2] >= 0.7f)
        return;

    // [Paril-KEX] improved waterjump
    vec3_t waterjump_vel;
    VectorScale(flatforward, 50, waterjump_vel);
    waterjump_vel[2] = 350;

    // simulate what would happen if we jumped out here, and
    // if we land on a dry spot we're good!
    // simulate 1 sec worth of movement
    float time = 0.1f;
    bool has_time = true;
    int steps = 10 * (800.0f / pm->s.gravity);

    vec3_t waterjump_origin;
    VectorCopy(pml.origin, waterjump_origin);
    for (int i = 0; i < min(50, steps); i++) {
        waterjump_vel[2] -= pm->s.gravity * time;

        if (waterjump_vel[2] < 0)
            has_time = false;

        PM_StepSlideMove_Generic(waterjump_origin, waterjump_vel, time, pm->mins, pm->maxs,
                                 pm->player, pml.clipmask, NULL, has_time, pm->trace);
    }

    // snap down to ground
    vec3_t down;
    VectorCopy(waterjump_origin, down);
    down[2] -= 2;
    PM_Trace(&trace, waterjump_origin, pm->mins, pm->maxs, down, MASK_SOLID);

    // can't stand here
    if (trace.fraction == 1.0f || trace.plane.normal[2] < 0.7f || trace.endpos[2] < pml.origin[2])
        return;

    // we're currently standing on ground, and the snapped position is a step
    if (pm->groundentity && fabsf(pml.origin[2] - trace.endpos[2]) <= STEPSIZE)
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
    VectorScale(flatforward, 50, pml.velocity);
    pml.velocity[2] = 350;

    pm->s.pm_flags |= PMF_TIME_WATERJUMP;
    pm->s.pm_time = 2048;
}

/*
===============
PM_FlyMove
===============
*/
static void PM_FlyMove(bool doclip)
{
    float   speed, drop, friction, control, newspeed;
    float   currentspeed, addspeed, accelspeed;
    int     i;
    vec3_t  wishvel;
    float   fmove, smove;
    vec3_t  wishdir;
    float   wishspeed;

    pm->s.viewheight = doclip ? 0 : 22;

    // friction

    speed = VectorLength(pml.velocity);
    if (speed < 1) {
        VectorClear(pml.velocity);
    } else {
        drop = 0;

        friction = pm_friction * 1.5f; // extra friction
        control = speed < pm_stopspeed ? pm_stopspeed : speed;
        drop += control * friction * pml.frametime;

        // scale the velocity
        newspeed = speed - drop;
        if (newspeed < 0)
            newspeed = 0;
        newspeed /= speed;

        VectorScale(pml.velocity, newspeed, pml.velocity);
    }

    // accelerate
    fmove = pm->cmd.forwardmove;
    smove = pm->cmd.sidemove;

    VectorNormalize(pml.forward);
    VectorNormalize(pml.right);

    for (i = 0; i < 3; i++)
        wishvel[i] = pml.forward[i] * fmove + pml.right[i] * smove;

    if (pm->cmd.buttons & BUTTON_JUMP)
        wishvel[2] += (pm_waterspeed * 0.5f);
    if (pm->cmd.buttons & BUTTON_CROUCH)
        wishvel[2] -= (pm_waterspeed * 0.5f);

    wishspeed = VectorNormalize2(wishvel, wishdir);

    //
    // clamp to server defined max speed
    //
    if (wishspeed > pm_maxspeed)
        wishspeed = pm_maxspeed;

    // Paril: newer clients do this
    wishspeed *= 2;

    currentspeed = DotProduct(pml.velocity, wishdir);
    addspeed = wishspeed - currentspeed;

    if (addspeed > 0) {
        accelspeed = pm_accelerate * pml.frametime * wishspeed;
        if (accelspeed > addspeed)
            accelspeed = addspeed;

        for (i = 0; i < 3; i++)
            pml.velocity[i] += accelspeed * wishdir[i];
    }

    if (doclip) {
        /*for (i = 0; i < 3; i++)
            end[i] = pml.origin[i] + pml.frametime * pml.velocity[i];

        trace = PM_Trace(pml.origin, pm->mins, pm->maxs, end);

        pml.origin = trace.endpos;*/
        PM_StepSlideMove();
    } else {
        // move
        VectorMA(pml.origin, pml.frametime, pml.velocity, pml.origin);
    }
}

static void PM_SetDimensions(void)
{
    pm->mins[0] = -16;
    pm->mins[1] = -16;

    pm->maxs[0] = 16;
    pm->maxs[1] = 16;

    if (pm->s.pm_type == PM_GIB) {
        pm->mins[2] = 0;
        pm->maxs[2] = 16;
        pm->s.viewheight = 8;
        return;
    }

    pm->mins[2] = -24;

    if ((pm->s.pm_flags & PMF_DUCKED) || pm->s.pm_type == PM_DEAD) {
        pm->maxs[2] = 4;
        pm->s.viewheight = -2;
    } else {
        pm->maxs[2] = 32;
        pm->s.viewheight = 22;
    }
}

static bool PM_AboveWater(void)
{
    trace_t tr;
    vec3_t below;

    VectorCopy(pml.origin, below);
    below[2] -= 8;

    pm->trace(&tr, pml.origin, pm->mins, pm->maxs, below, pm->player, MASK_SOLID);
    if (tr.fraction < 1.0f)
        return false;

    pm->trace(&tr, pml.origin, pm->mins, pm->maxs, below, pm->player, MASK_WATER);
    if (tr.fraction < 1.0f)
        return true;

    return false;
}

/*
==============
PM_CheckDuck

Sets mins, maxs, and pm->viewheight
==============
*/
static bool PM_CheckDuck(void)
{
    if (pm->s.pm_type == PM_GIB)
        return false;

    trace_t trace;
    bool flags_changed = false;

    if (pm->s.pm_type == PM_DEAD) {
        if (!(pm->s.pm_flags & PMF_DUCKED)) {
            pm->s.pm_flags |= PMF_DUCKED;
            flags_changed = true;
        }
    } else if ((pm->cmd.buttons & BUTTON_CROUCH) &&
               (pm->groundentity || (pm->waterlevel <= WATER_FEET && !PM_AboveWater())) &&
               !(pm->s.pm_flags & PMF_ON_LADDER) &&
               !pm_config.n64_physics) {
        // duck
        if (!(pm->s.pm_flags & PMF_DUCKED)) {
            // check that duck won't be blocked
            vec3_t check_maxs = { pm->maxs[0], pm->maxs[1], 4 };
            PM_Trace(&trace, pml.origin, pm->mins, check_maxs, pml.origin, pml.clipmask);
            if (!trace.allsolid) {
                pm->s.pm_flags |= PMF_DUCKED;
                flags_changed = true;
            }
        }
    } else {
        // stand up if possible
        if (pm->s.pm_flags & PMF_DUCKED) {
            // try to stand up
            vec3_t check_maxs = { pm->maxs[0], pm->maxs[1], 32 };
            PM_Trace(&trace, pml.origin, pm->mins, check_maxs, pml.origin, pml.clipmask);
            if (!trace.allsolid) {
                pm->s.pm_flags &= ~PMF_DUCKED;
                flags_changed = true;
            }
        }
    }

    if (!flags_changed)
        return false;

    PM_SetDimensions();
    return true;
}

/*
==============
PM_DeadMove
==============
*/
static void PM_DeadMove(void)
{
    float forward;

    if (!pm->groundentity)
        return;

    // extra friction

    forward = VectorLength(pml.velocity);
    forward -= 20;
    if (forward <= 0) {
        VectorClear(pml.velocity);
    } else {
        VectorNormalize(pml.velocity);
        VectorScale(pml.velocity, forward, pml.velocity);
    }
}

static bool PM_GoodPosition(void)
{
    if (pm->s.pm_type == PM_NOCLIP)
        return true;

    trace_t trace;
    PM_Trace(&trace, pm->s.origin, pm->mins, pm->maxs, pm->s.origin, pml.clipmask);

    return !trace.allsolid;
}

/*
================
PM_SnapPosition

On exit, the origin will have a value that is pre-quantized to the PMove
precision of the network channel and in a valid position.
================
*/
static void PM_SnapPosition(void)
{
    VectorCopy(pml.velocity, pm->s.velocity);
    VectorCopy(pml.origin, pm->s.origin);

    if (PM_GoodPosition())
        return;

    if (G_FixStuckObject_Generic(pm->s.origin, pm->mins, pm->maxs, pm->player, pml.clipmask, pm->trace) == NO_GOOD_POSITION) {
        VectorCopy(pml.previous_origin, pm->s.origin);
        return;
    }
}

/*
================
PM_InitialSnapPosition
================
*/
static void PM_InitialSnapPosition(void)
{
    int                 x, y, z;
    vec3_t              base;
    static const float  offset[3] = { 0, -1, 1 };

    VectorCopy(pm->s.origin, base);

    for (z = 0; z < 3; z++) {
        pm->s.origin[2] = base[2] + offset[z];
        for (y = 0; y < 3; y++) {
            pm->s.origin[1] = base[1] + offset[y];
            for (x = 0; x < 3; x++) {
                pm->s.origin[0] = base[0] + offset[x];
                if (PM_GoodPosition()) {
                    VectorCopy(pm->s.origin, pml.origin);
                    VectorCopy(pm->s.origin, pml.previous_origin);
                    return;
                }
            }
        }
    }
}

/*
================
PM_ClampAngles
================
*/
static void PM_ClampAngles(void)
{
    if (pm->s.pm_flags & PMF_TIME_TELEPORT) {
        pm->viewangles[YAW] = SHORT2ANGLE(pm->cmd.angles[YAW] + pm->s.delta_angles[YAW]);
        pm->viewangles[PITCH] = 0;
        pm->viewangles[ROLL] = 0;
    } else {
        // circularly clamp the angles with deltas
        for (int i = 0; i < 3; i++)
            pm->viewangles[i] = SHORT2ANGLE((short)(pm->cmd.angles[i] + pm->s.delta_angles[i]));

        // don't let the player look up or down more than 90 degrees
        pm->viewangles[PITCH] = Q_clipf(pm->viewangles[PITCH], -89, 89);
    }
    AngleVectors(pm->viewangles, pml.forward, pml.right, pml.up);
}

// [Paril-KEX]
static void PM_ScreenEffects(void)
{
    // add for contents
    vec3_t vieworg;
    VectorAdd(pml.origin, pm->viewoffset, vieworg);
    vieworg[2] += pm->s.viewheight;
    contents_t contents = pm->pointcontents(vieworg);

    if (contents & (CONTENTS_LAVA | CONTENTS_SLIME | CONTENTS_WATER))
        pm->rdflags |= RDF_UNDERWATER;
    else
        pm->rdflags &= ~RDF_UNDERWATER;

    if (contents & (CONTENTS_SOLID | CONTENTS_LAVA))
        G_AddBlend(1.0f, 0.3f, 0.0f, 0.6f, pm->screen_blend);
    else if (contents & CONTENTS_SLIME)
        G_AddBlend(0.0f, 0.1f, 0.05f, 0.6f, pm->screen_blend);
    else if (contents & CONTENTS_WATER)
        G_AddBlend(0.5f, 0.3f, 0.2f, 0.4f, pm->screen_blend);
}

/*
================
Pmove

Can be called by either the server or the client
================
*/
void Pmove(pmove_t *pmove)
{
    pm = pmove;

    // clear results
    pm->touch.num = 0;
    VectorClear(pm->viewangles);
    pm->s.viewheight = 0;
    pm->groundentity = NULL;
    pm->watertype = CONTENTS_NONE;
    pm->waterlevel = WATER_NONE;
    Vector4Clear(pm->screen_blend);
    pm->rdflags = RDF_NONE;
    pm->jump_sound = false;
    pm->step_clip = false;
    pm->impact_delta = 0;

    // clear all pmove local vars
    pml = (pml_t){ 0 };

    // convert origin and velocity to float values
    VectorCopy(pm->s.origin, pml.origin);
    VectorCopy(pm->s.velocity, pml.velocity);

    VectorCopy(pml.velocity, pml.start_velocity);

    // save old org in case we get stuck
    VectorCopy(pm->s.origin, pml.previous_origin);

    pml.frametime = pm->cmd.msec * 0.001f;
    pml.clipmask = PM_TraceMask();

    PM_ClampAngles();

    if (pm->s.pm_type == PM_SPECTATOR || pm->s.pm_type == PM_NOCLIP) {
        pm->s.pm_flags = PMF_NONE;

        if (pm->s.pm_type == PM_SPECTATOR) {
            VectorSet(pm->mins, -8, -8, -8);
            VectorSet(pm->maxs,  8,  8,  8);
        }

        PM_FlyMove(pm->s.pm_type == PM_SPECTATOR);
        PM_SnapPosition();
        return;
    }

    if (pm->s.pm_type >= PM_DEAD) {
        pm->cmd.forwardmove = 0;
        pm->cmd.sidemove = 0;
        pm->cmd.buttons &= ~(BUTTON_JUMP | BUTTON_CROUCH);
    }

    if (pm->s.pm_type == PM_FREEZE)
        return; // no movement at all

    // set mins, maxs, and viewheight
    PM_SetDimensions();

    // catagorize for ducking
    PM_CategorizePosition();

    if (pm->snapinitial)
        PM_InitialSnapPosition();

    // set groundentity, watertype, and waterlevel
    if (PM_CheckDuck())
        PM_CategorizePosition();

    if (pm->s.pm_type == PM_DEAD)
        PM_DeadMove();

    PM_CheckSpecialMovement();

    // drop timing counter
    if (pm->s.pm_time) {
        if (pm->cmd.msec >= pm->s.pm_time) {
            pm->s.pm_flags &= ~(PMF_TIME_WATERJUMP | PMF_TIME_LAND | PMF_TIME_TELEPORT | PMF_TIME_TRICK);
            pm->s.pm_time = 0;
        } else
            pm->s.pm_time -= pm->cmd.msec;
    }

    if (pm->s.pm_flags & PMF_TIME_TELEPORT) {
        // teleport pause stays exactly in place
    } else if (pm->s.pm_flags & PMF_TIME_WATERJUMP) {
        // waterjump has no control, but falls
        pml.velocity[2] -= pm->s.gravity * pml.frametime;
        if (pml.velocity[2] < 0) {
            // cancel as soon as we are falling down again
            pm->s.pm_flags &= ~(PMF_TIME_WATERJUMP | PMF_TIME_LAND | PMF_TIME_TELEPORT | PMF_TIME_TRICK);
            pm->s.pm_time = 0;
        }

        PM_StepSlideMove();
    } else {
        PM_CheckJump();

        PM_Friction();

        if (pm->waterlevel >= WATER_WAIST)
            PM_WaterMove();
        else {
            vec3_t angles;

            VectorCopy(pm->viewangles, angles);
            if (angles[PITCH] > 180)
                angles[PITCH] = angles[PITCH] - 360;
            angles[PITCH] /= 3;

            AngleVectors(angles, pml.forward, pml.right, pml.up);

            PM_AirMove();
        }
    }

    // set groundentity, watertype, and waterlevel for final spot
    PM_CategorizePosition();

    // trick jump
    if (pm->s.pm_flags & PMF_TIME_TRICK)
        PM_CheckJump();

    // [Paril-KEX]
    PM_ScreenEffects();

    PM_SnapPosition();
}
