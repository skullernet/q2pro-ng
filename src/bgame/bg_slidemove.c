// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "shared/shared.h"
#include "bgame/bg_local.h"
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
                                        int ignore, contents_t mask, trace_func_t trace_func)
{
    trace_t tr;

    trace_func(&tr, origin, own_mins, own_maxs, origin, ignore, mask);
    if (!tr.startsolid)
        return GOOD_POSITION;

    good_position_t good_positions[NUM_SIDE_CHECKS];
    int num_good_positions = 0;

    for (int sn = 0; sn < NUM_SIDE_CHECKS; sn++) {
        const side_check_t *side = &side_checks[sn];

        vec3_t start = VectorInit(origin);
        vec3_t mins = { 0 }, maxs = { 0 };

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

                vec3_t ep_start = VectorInit(start);
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

        const side_check_t *other_side = &side_checks[sn ^ 1];
        vec3_t opposite_start = VectorInit(origin);

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
        // push us very slightly away from the wall
        vec3_t end;
        VectorMA(tr.endpos, 0.125f, side->normal, end);

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
void PM_ClipVelocity(const vec3_t in, const vec3_t normal, vec3_t out, float overbounce)
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

void PM_RecordTrace(touch_list_t *touch, const trace_t *tr)
{
    if (!touch || touch->num == MAXTOUCH)
        return;

    for (int i = 0; i < touch->num; i++)
        if (touch->traces[i].entnum == tr->entnum)
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

#define MAX_CLIP_PLANES     5

// [Paril-KEX] made generic so you can run this without needing a pml/pm
void PM_StepSlideMove_Generic(vec3_t origin, vec3_t velocity, float frametime, const vec3_t mins, const vec3_t maxs,
                              int passent, contents_t mask, touch_list_t *touch, bool has_time, trace_func_t trace_func)
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
