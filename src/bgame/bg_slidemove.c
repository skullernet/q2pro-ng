// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "shared/shared.h"
#include "bgame/bg_local.h"

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
stuck_result_t PM_FixStuckObject_Generic(vec3_t *origin, box3_t own, int ignore,
                                         contents_t mask, trace_func_t trace_func)
{
    trace_t tr;

    trace_func(&tr, &(trace_args_t){ *origin, *origin, own, ignore, mask });
    if (!tr.startsolid)
        return GOOD_POSITION;

    good_position_t good_positions[NUM_SIDE_CHECKS];
    int num_good_positions = 0;

    for (int sn = 0; sn < NUM_SIDE_CHECKS; sn++) {
        const side_check_t *side = &side_checks[sn];

        vec3_t start = *origin;
        box3_t box = box3_origin;

        for (int n = 0; n < 3; n++) {
            if (side->normal[n] < 0)
                start.xyz[n] += own.mins.xyz[n];
            else if (side->normal[n] > 0)
                start.xyz[n] += own.maxs.xyz[n];

            if (side->mins[n] == -1)
                box.mins.xyz[n] = own.mins.xyz[n];
            else if (side->mins[n] == 1)
                box.mins.xyz[n] = own.maxs.xyz[n];

            if (side->maxs[n] == -1)
                box.maxs.xyz[n] = own.mins.xyz[n];
            else if (side->maxs[n] == 1)
                box.maxs.xyz[n] = own.maxs.xyz[n];
        }

        int needed_epsilon_fix = -1;
        int needed_epsilon_dir = 0;

        trace_func(&tr, &(trace_args_t){ start, start, box, ignore, mask });

        if (tr.startsolid) {
            for (int e = 0; e < 3; e++) {
                if (side->normal[e] != 0)
                    continue;

                vec3_t ep_start = start;
                ep_start.xyz[e] += 1;

                trace_func(&tr, &(trace_args_t){ ep_start, ep_start, box, ignore, mask });

                if (!tr.startsolid) {
                    start = ep_start;
                    needed_epsilon_fix = e;
                    needed_epsilon_dir = 1;
                    break;
                }

                ep_start.xyz[e] -= 2;
                trace_func(&tr, &(trace_args_t){ ep_start, ep_start, box, ignore, mask });

                if (!tr.startsolid) {
                    start = ep_start;
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
        vec3_t opposite_start = *origin;

        for (int n = 0; n < 3; n++) {
            if (other_side->normal[n] < 0)
                opposite_start.xyz[n] += own.mins.xyz[n];
            else if (other_side->normal[n] > 0)
                opposite_start.xyz[n] += own.maxs.xyz[n];
        }

        if (needed_epsilon_fix >= 0)
            opposite_start.xyz[needed_epsilon_fix] += needed_epsilon_dir;

        // potentially a good side; start from our center, push back to the opposite side
        // to find how much clearance we have
        trace_func(&tr, &(trace_args_t){ start, opposite_start, box, ignore, mask });

        // ???
        if (tr.startsolid)
            continue;

        // check the delta
        // push us very slightly away from the wall
        vec3_t end = Vec3_MA(tr.endpos, 0.125f, Vec3_Load(side->normal));

        // calculate delta
        vec3_t delta = Vec3_Sub(end, opposite_start);
        vec3_t new_origin = Vec3_Add(*origin, delta);

        if (needed_epsilon_fix >= 0)
            new_origin.xyz[needed_epsilon_fix] += needed_epsilon_dir;

        trace_func(&tr, &(trace_args_t){ new_origin, new_origin, own, ignore, mask });

        // bad
        if (tr.startsolid)
            continue;

        good_position_t *good = &good_positions[num_good_positions++];
        good->origin = new_origin;
        good->dist = Vec3_LengthSquared(delta);
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

        *origin = good_positions[best].origin;
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
vec3_t PM_ClipVelocity(vec3_t in, vec3_t normal, float overbounce)
{
    float  backoff;
    float  change;
    int    i;
    vec3_t out;

    backoff = Vec3_Dot(in, normal) * overbounce;

    for (i = 0; i < 3; i++) {
        change = normal.xyz[i] * backoff;
        out.xyz[i] = in.xyz[i] - change;
        if (out.xyz[i] > -STOP_EPSILON && out.xyz[i] < STOP_EPSILON)
            out.xyz[i] = 0;
    }

    return out;
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
void PM_StepSlideMove_Generic(vec3_t *origin, vec3_t *velocity, float frametime, box3_t box, int passent,
                              contents_t mask, touch_list_t *touch, bool has_time, trace_func_t trace_func)
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

    primal_velocity = *velocity;
    numplanes = 0;

    time_left = frametime;

    for (int bumpcount = 0; bumpcount < 4; bumpcount++) {
        end = Vec3_MA(*origin, time_left, *velocity);

        trace_func(&trace, &(trace_args_t){ *origin, end, box, passent, mask });
        if (trace.allsolid) {
            // entity is trapped in another solid
            velocity->z = 0; // don't build up falling damage

            // save entity for contact
            PM_RecordTrace(touch, &trace);
            return;
        }

        if (trace.fraction > 0) {
            // actually covered some distance
            *origin = trace.endpos;
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
            *velocity = vec3_origin;
            break;
        }

        //
        // if this is the same plane we hit before, nudge origin
        // out along it, which fixes some epsilon issues with
        // non-axial planes (xswamp, q2dm1 sometimes...)
        //
        for (i = 0; i < numplanes; i++) {
            if (Vec3_Dot(trace.plane.normal, planes[i]) > 0.99f) {
                origin->x += trace.plane.normal.x * 0.01f;
                origin->y += trace.plane.normal.y * 0.01f;
                PM_FixStuckObject_Generic(origin, box, passent, mask, trace_func);
                break;
            }
        }

        if (i < numplanes)
            continue;

        planes[numplanes++] = trace.plane.normal;

        //
        // modify original_velocity so it parallels all of the clip planes
        //
        for (i = 0; i < numplanes; i++) {
            *velocity = PM_ClipVelocity(*velocity, planes[i], 1.01f);
            for (j = 0; j < numplanes; j++)
                if ((j != i) && Vec3_Dot(*velocity, planes[j]) < 0)
                    break; // not ok
            if (j == numplanes)
                break;
        }

        if (i != numplanes) {
            // go along this plane
        } else {
            // go along the crease
            if (numplanes != 2) {
                *velocity = vec3_origin;
                break;
            }
            dir = Vec3_Cross(planes[0], planes[1]);
            d = Vec3_Dot(dir, *velocity);
            *velocity = Vec3_Scale(dir, d);
        }

        //
        // if velocity is against the original velocity, stop dead
        // to avoid tiny oscillations in sloping corners
        //
        if (Vec3_Dot(*velocity, primal_velocity) <= 0) {
            *velocity = vec3_origin;
            break;
        }
    }

    if (has_time)
        *velocity = primal_velocity;
}
