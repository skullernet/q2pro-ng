// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// m_move.c -- monster movement

#include "g_local.h"

/*
=============
M_CheckBottom

Returns false if any part of the bottom of the entity is off an edge that
is not a staircase.

=============
*/
bool M_CheckBottom_Fast_Generic(const vec3_t absmins, const vec3_t absmaxs, bool ceiling)
{
    // PGM
    //  FIXME - this will only handle 0,0,1 and 0,0,-1 gravity vectors
    vec3_t start;

    start[2] = absmins[2] - 1;
    if (ceiling)
        start[2] = absmaxs[2] + 1;
    // PGM

    for (int x = 0; x <= 1; x++)
        for (int y = 0; y <= 1; y++) {
            start[0] = x ? absmaxs[0] : absmins[0];
            start[1] = y ? absmaxs[1] : absmins[1];
            if (trap_PointContents(start) != CONTENTS_SOLID)
                return false;
        }

    return true; // we got out easy
}

bool M_CheckBottom_Slow_Generic(const vec3_t origin, const vec3_t mins, const vec3_t maxs, int ignore, contents_t mask, bool ceiling, bool allow_any_step_height)
{
    //
    // check it for real...
    //
    vec3_t step_quadrant_size = {
        (maxs[0] - mins[0]) * 0.5f,
        (maxs[1] - mins[1]) * 0.5f,
    };

    vec3_t half_step_quadrant;
    vec3_t half_step_quadrant_mins;

    VectorScale(step_quadrant_size, 0.5f, half_step_quadrant);
    VectorNegate(half_step_quadrant, half_step_quadrant_mins);

    vec3_t start, stop;

    start[0] = stop[0] = origin[0];
    start[1] = stop[1] = origin[1];

    // PGM
    if (!ceiling) {
        start[2] = origin[2] + mins[2];
        stop[2] = start[2] - STEPSIZE * 2;
    } else {
        start[2] = origin[2] + maxs[2];
        stop[2] = start[2] + STEPSIZE * 2;
    }
    // PGM

    vec3_t mins_no_z = { mins[0], mins[1] };
    vec3_t maxs_no_z = { maxs[0], maxs[1] };

    trace_t trace;
    trap_Trace(&trace, start, mins_no_z, maxs_no_z, stop, ignore, mask);

    if (trace.fraction == 1.0f)
        return false;

    // [Paril-KEX]
    if (allow_any_step_height)
        return true;

    start[0] = stop[0] = origin[0] + ((mins[0] + maxs[0]) * 0.5f);
    start[1] = stop[1] = origin[1] + ((mins[1] + maxs[1]) * 0.5f);

    float mid = trace.endpos[2];

    // the corners must be within 16 of the midpoint
    for (int x = 0; x <= 1; x++)
        for (int y = 0; y <= 1; y++) {
            vec3_t quadrant_start, quadrant_end;

            VectorCopy(start, quadrant_start);

            if (x)
                quadrant_start[0] += half_step_quadrant[0];
            else
                quadrant_start[0] -= half_step_quadrant[0];

            if (y)
                quadrant_start[1] += half_step_quadrant[1];
            else
                quadrant_start[1] -= half_step_quadrant[1];

            VectorCopy(quadrant_start, quadrant_end);
            quadrant_end[2] = stop[2];

            trap_Trace(&trace, quadrant_start, half_step_quadrant_mins,
                       half_step_quadrant, quadrant_end, ignore, mask);

            // PGM
            //  FIXME - this will only handle 0,0,1 and 0,0,-1 gravity vectors
            if (ceiling) {
                if (trace.fraction == 1.0f || trace.endpos[2] - mid > (STEPSIZE))
                    return false;
            } else {
                if (trace.fraction == 1.0f || mid - trace.endpos[2] > (STEPSIZE))
                    return false;
            }
            // PGM
        }

    return true;
}

bool M_CheckBottom(edict_t *ent)
{
    vec3_t mins, maxs;

    VectorAdd(ent->s.origin, ent->r.mins, mins);
    VectorAdd(ent->s.origin, ent->r.maxs, maxs);

    // if all of the points under the corners are solid world, don't bother
    // with the tougher checks

    if (M_CheckBottom_Fast_Generic(mins, maxs, ent->gravityVector[2] > 0))
        return true; // we got out easy

    contents_t mask = (ent->r.svflags & SVF_MONSTER) ? MASK_MONSTERSOLID : (MASK_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER);
    return M_CheckBottom_Slow_Generic(ent->s.origin, ent->r.mins, ent->r.maxs,
                                      ent->s.number, mask, ent->gravityVector[2] > 0,
                                      ent->spawnflags & SPAWNFLAG_MONSTER_SUPER_STEP);
}

//============
// ROGUE
static bool IsBadAhead(edict_t *self, edict_t *bad, const vec3_t move)
{
    vec3_t dir;
    vec3_t forward;
    float  dp_bad, dp_move;

    VectorSubtract(bad->s.origin, self->s.origin, dir);
    AngleVectors(self->s.angles, forward, NULL, NULL);
    dp_bad = DotProduct(forward, dir);
    dp_move = DotProduct(forward, move);

    if ((dp_bad < 0) && (dp_move < 0))
        return true;
    if ((dp_bad > 0) && (dp_move > 0))
        return true;

    return false;
}

static void G_IdealHoverPosition(edict_t *ent, vec3_t pos)
{
    if ((!ent->enemy && !(ent->monsterinfo.aiflags & AI_MEDIC)) || (ent->monsterinfo.aiflags & (AI_COMBAT_POINT | AI_SOUND_TARGET | AI_HINT_PATH | AI_PATHING))) {
        VectorClear(pos); // go right for the center
        return;
    }

    // pick random direction
    float theta = frandom1(2 * M_PIf);
    float phi;

    // buzzards pick half sphere
    if (ent->monsterinfo.fly_above)
        phi = acosf(0.7f + frandom1(0.3f));
    else if (ent->monsterinfo.fly_buzzard || (ent->monsterinfo.aiflags & AI_MEDIC))
        phi = acosf(frandom());
    // non-buzzards pick a level around the center
    else
        phi = acosf(crandom() * 0.7f);

    vec3_t d = {
        sinf(phi) * cosf(theta),
        sinf(phi) * sinf(theta),
        cosf(phi)
    };

    float scale = frandom2(ent->monsterinfo.fly_min_distance, ent->monsterinfo.fly_max_distance);
    VectorScale(d, scale, pos);
}

static bool SV_flystep_testvisposition(vec3_t wanted_pos, bool bottom, edict_t *ent)
{
    vec3_t start, end;

    VectorCopy(ent->s.origin, start);
    VectorCopy(ent->s.origin, end);

    if (bottom) {
        start[2] += ent->r.mins[2];
        end[2] += ent->r.mins[2] - ent->monsterinfo.fly_acceleration;
    } else {
        start[2] += ent->r.maxs[2];
        end[2] += ent->r.maxs[2] + ent->monsterinfo.fly_acceleration;
    }

    trace_t tr;
    trap_Trace(&tr, start, NULL, NULL, wanted_pos, ent->s.number,
               MASK_SOLID | CONTENTS_MONSTERCLIP);

    if (tr.fraction == 1.0f) {
        trap_Trace(&tr, ent->s.origin, ent->r.mins, ent->r.maxs, end,
                   ent->s.number, MASK_SOLID | CONTENTS_MONSTERCLIP);

        if (tr.fraction == 1.0f)
            return true;
    }

    return false;
}

void slerp(const vec3_t from, const vec3_t to, float t, vec3_t out)
{
    float dot = DotProduct(from, to);
    float aFactor;
    float bFactor;

    if (fabsf(dot) > 0.9995f) {
        aFactor = 1.0f - t;
        bFactor = t;
    } else {
        float ang = acosf(dot);
        float sinOmega = sinf(ang);
        float sinAOmega = sinf((1.0f - t) * ang);
        float sinBOmega = sinf(t * ang);
        aFactor = sinAOmega / sinOmega;
        bFactor = sinBOmega / sinOmega;
    }

    LerpVector2(from, to, aFactor, bFactor, out);
}

static bool SV_alternate_flystep(edict_t *ent, vec3_t move, bool relink, edict_t *current_bad)
{
    // swimming monsters just follow their velocity in the air
    if ((ent->flags & FL_SWIM) && ent->waterlevel < WATER_UNDER)
        return true;

    if (ent->monsterinfo.fly_position_time <= level.time ||
        (ent->enemy && ent->monsterinfo.fly_pinned && !visible(ent, ent->enemy))) {
        ent->monsterinfo.fly_pinned = false;
        ent->monsterinfo.fly_position_time = level.time + random_time_sec(3, 10);
        G_IdealHoverPosition(ent, ent->monsterinfo.fly_ideal_position);
    }

    vec3_t towards_origin, towards_velocity = { 0 };

    float current_speed;
    vec3_t dir;

    current_speed = VectorNormalize2(ent->velocity, dir);

    // FIXME
    if (isnan(dir[0]) || isnan(dir[1]) || isnan(dir[2]))
        return false;

    if (ent->monsterinfo.aiflags & AI_PATHING) {
        if (ent->monsterinfo.nav_path.returnCode == PathReturnCode_TraversalPending)
            VectorCopy(ent->monsterinfo.nav_path.secondMovePoint, towards_origin);
        else
            VectorCopy(ent->monsterinfo.nav_path.firstMovePoint, towards_origin);
    } else if (ent->enemy && !(ent->monsterinfo.aiflags & (AI_COMBAT_POINT | AI_SOUND_TARGET | AI_LOST_SIGHT))) {
        VectorCopy(ent->enemy->s.origin, towards_origin);
        VectorCopy(ent->enemy->velocity, towards_velocity);
    } else if (ent->goalentity) {
        VectorCopy(ent->goalentity->s.origin, towards_origin);
    } else { // what we're going towards probably died or something
        // change speed
        if (current_speed) {
            if (current_speed > 0)
                current_speed = max(0.0f, current_speed - ent->monsterinfo.fly_acceleration);
            else if (current_speed < 0)
                current_speed = min(0.0f, current_speed + ent->monsterinfo.fly_acceleration);

            VectorScale(dir, current_speed, ent->velocity);
        }

        return true;
    }

    vec3_t wanted_pos;

    if (ent->monsterinfo.fly_pinned)
        VectorCopy(ent->monsterinfo.fly_ideal_position, wanted_pos);
    else if (ent->monsterinfo.aiflags & (AI_PATHING | AI_COMBAT_POINT | AI_SOUND_TARGET | AI_LOST_SIGHT))
        VectorCopy(towards_origin, wanted_pos);
    else {
        VectorAdd(towards_origin, ent->monsterinfo.fly_ideal_position, wanted_pos);
        VectorMA(wanted_pos, 0.25f, towards_velocity, wanted_pos);
    }

    static const vec3_t trace_mins = { -8, -8, -8 };
    static const vec3_t trace_maxs = { 8, 8, 8 };

    // find a place we can fit in from here
    trace_t tr;
    trap_Trace(&tr, towards_origin, trace_mins, trace_maxs, wanted_pos,
               ent->s.number, MASK_SOLID | CONTENTS_MONSTERCLIP);

    if (!tr.allsolid)
        VectorCopy(tr.endpos, wanted_pos);

    vec3_t dest_diff;

    VectorSubtract(wanted_pos, ent->s.origin, dest_diff);

    if (dest_diff[2] > ent->r.mins[2] && dest_diff[2] < ent->r.maxs[2])
        dest_diff[2] = 0;

    float dist_to_wanted;
    vec3_t wanted_dir;

    dist_to_wanted = VectorNormalize2(dest_diff, wanted_dir);

    if (!(ent->monsterinfo.aiflags & AI_MANUAL_STEERING)) {
        vec3_t d;
        VectorSubtract(towards_origin, ent->s.origin, d);
        ent->ideal_yaw = vectoyaw(d);
    }

    // check if we're blocked from moving this way from where we are
    vec3_t end;
    VectorMA(ent->s.origin, ent->monsterinfo.fly_acceleration, wanted_dir, end);
    trap_Trace(&tr, ent->s.origin, ent->r.mins, ent->r.maxs, end, ent->s.number,
               MASK_SOLID | CONTENTS_MONSTERCLIP);

    vec3_t aim_fwd, aim_rgt, aim_up;
    vec3_t yaw_angles = { 0, ent->s.angles[1], 0 };

    AngleVectors(yaw_angles, aim_fwd, aim_rgt, aim_up);

    // it's a fairly close block, so we may want to shift more dramatically
    if (tr.fraction < 0.25f) {
        bool bottom_visible = SV_flystep_testvisposition(wanted_pos, true, ent);
        bool top_visible = SV_flystep_testvisposition(wanted_pos, false, ent);

        // top & bottom are same, so we need to try right/left
        if (bottom_visible == top_visible) {
            vec3_t a, b;

            for (int i = 0; i < 3; i++) {
                a[i] = ent->s.origin[i] + aim_fwd[i] * ent->r.maxs[i] - aim_rgt[i] * ent->r.maxs[i];
                b[i] = ent->s.origin[i] + aim_fwd[i] * ent->r.maxs[i] + aim_rgt[i] * ent->r.maxs[i];
            }

            trace_t tra, trb;
            trap_Trace(&tra, a, NULL, NULL, wanted_pos, ent->s.number, MASK_SOLID | CONTENTS_MONSTERCLIP);
            trap_Trace(&trb, b, NULL, NULL, wanted_pos, ent->s.number, MASK_SOLID | CONTENTS_MONSTERCLIP);

            bool left_visible = tra.fraction == 1.0f;
            bool right_visible = trb.fraction == 1.0f;

            if (left_visible != right_visible) {
                if (right_visible)
                    VectorAdd(wanted_dir, aim_rgt, wanted_dir);
                else
                    VectorSubtract(wanted_dir, aim_rgt, wanted_dir);
            } else
                // we're probably stuck, push us directly away
                VectorCopy(tr.plane.normal, wanted_dir);
        } else {
            if (top_visible)
                VectorAdd(wanted_dir, aim_up, wanted_dir);
            else
                VectorSubtract(wanted_dir, aim_up, wanted_dir);
        }

        VectorNormalize(wanted_dir);
    }

    // the closer we are to zero, the more we can change dir.
    // if we're pushed past our max speed we shouldn't
    // turn at all.
    bool following_paths = ent->monsterinfo.aiflags & (AI_PATHING | AI_COMBAT_POINT | AI_LOST_SIGHT);
    float turn_factor;

    if (((ent->monsterinfo.fly_thrusters && !ent->monsterinfo.fly_pinned) || following_paths) && DotProduct(dir, wanted_dir) > 0.0f)
        turn_factor = 0.45f;
    else
        turn_factor = min(1.0f, 0.84f + (0.08f * (current_speed / ent->monsterinfo.fly_speed)));

    vec3_t final_dir;
    if (VectorEmpty(dir))
        VectorCopy(wanted_dir, final_dir);
    else
        VectorCopy(dir, final_dir);

    // FIXME
    if (isnan(final_dir[0]) || isnan(final_dir[1]) || isnan(final_dir[2]))
        return false;

    // swimming monsters don't exit water voluntarily, and
    // flying monsters don't enter water voluntarily (but will
    // try to leave it)
    bool bad_movement_direction = false;

    //if (!(ent->monsterinfo.aiflags & AI_COMBAT_POINT))
    {
        VectorMA(ent->s.origin, current_speed, wanted_dir, end);
        if (ent->flags & FL_SWIM)
            bad_movement_direction = !(trap_PointContents(end) & CONTENTS_WATER);
        else if ((ent->flags & FL_FLY) && ent->waterlevel < WATER_UNDER)
            bad_movement_direction = trap_PointContents(end) & CONTENTS_WATER;
    }

    if (bad_movement_direction) {
        if (ent->monsterinfo.fly_recovery_time < level.time) {
            crandom_vec(ent->monsterinfo.fly_recovery_dir, 1.0f);
            VectorNormalize(ent->monsterinfo.fly_recovery_dir);
            ent->monsterinfo.fly_recovery_time = level.time + SEC(1);
        }

        VectorCopy(ent->monsterinfo.fly_recovery_dir, wanted_dir);
    }

    if (!VectorEmpty(dir) && turn_factor > 0) {
        slerp(dir, wanted_dir, 1.0f - turn_factor, final_dir);
        VectorNormalize(final_dir);
    }

    // the closer we are to the wanted position, we want to slow
    // down so we don't fly past it.
    float speed_factor;

    if (!ent->enemy || (ent->monsterinfo.fly_thrusters && !ent->monsterinfo.fly_pinned) || following_paths) {
        // Paril: only do this correction if we are following paths. we want to move backwards
        // away from players.
        if (following_paths && DotProduct(aim_fwd, wanted_dir) < -0.25f && !VectorEmpty(dir))
            speed_factor = 0;
        else
            speed_factor = 1;
    } else
        speed_factor = min(1.0f, dist_to_wanted / ent->monsterinfo.fly_speed);

    if (bad_movement_direction)
        speed_factor = -speed_factor;

    float accel = ent->monsterinfo.fly_acceleration;

    // if we're flying away from our destination, apply reverse thrusters
    if (DotProduct(final_dir, wanted_dir) < 0.25f)
        accel *= 2.0f;

    float wanted_speed = ent->monsterinfo.fly_speed * speed_factor;

    if (ent->monsterinfo.aiflags & AI_MANUAL_STEERING)
        wanted_speed = 0;

    // change speed
    if (current_speed > wanted_speed)
        current_speed = max(wanted_speed, current_speed - accel);
    else if (current_speed < wanted_speed)
        current_speed = min(wanted_speed, current_speed + accel);

    // FIXME
    if (isnan(final_dir[0]) || isnan(final_dir[1]) || isnan(final_dir[2]) || isnan(current_speed))
        return false;

    // commit
    VectorScale(final_dir, current_speed, ent->velocity);

    // for buzzards, set their pitch
    if (ent->enemy && (ent->monsterinfo.fly_buzzard || (ent->monsterinfo.aiflags & AI_MEDIC))) {
        vec3_t d;
        VectorSubtract(ent->s.origin, towards_origin, d);
        vectoangles(d, d);
        ent->s.angles[PITCH] = LerpAngle(ent->s.angles[PITCH], -d[PITCH], FRAME_TIME_SEC * 4.0f);
    } else
        ent->s.angles[PITCH] = 0;

    return true;
}

// flying monsters don't step up
static bool SV_flystep(edict_t *ent, vec3_t move, bool relink, edict_t *current_bad)
{
    if (ent->monsterinfo.aiflags & AI_ALTERNATE_FLY)
        if (SV_alternate_flystep(ent, move, relink, current_bad))
            return true;

    // try the move
    vec3_t oldorg, neworg;
    VectorCopy(ent->s.origin, oldorg);
    VectorAdd(ent->s.origin, move, neworg);

    // fixme: move to monsterinfo
    // we want the carrier to stay a certain distance off the ground, to help prevent him
    // from shooting his fliers, who spawn in below him
    float minheight;

    if (!strcmp(ent->classname, "monster_carrier"))
        minheight = 104;
    else
        minheight = 40;

    // try one move with vertical motion, then one without
    for (int i = 0; i < 2; i++) {
        vec3_t new_move;
        VectorCopy(move, new_move);

        if (i == 0 && ent->enemy) {
            if (!ent->goalentity)
                ent->goalentity = ent->enemy;

            float goal_position_z = (ent->monsterinfo.aiflags & AI_PATHING) ?
                ent->monsterinfo.nav_path.firstMovePoint[2] : ent->goalentity->s.origin[2];

            float dz = ent->s.origin[2] - goal_position_z;
            float dist = VectorLength(move);

            if (ent->goalentity->client) {
                if (dz > minheight) {
                    //  pmm
                    VectorScale(new_move, 0.5f, new_move);
                    new_move[2] -= dist;
                }
                if (!((ent->flags & FL_SWIM) && (ent->waterlevel < WATER_WAIST)))
                    if (dz < (minheight - 10)) {
                        VectorScale(new_move, 0.5f, new_move);
                        new_move[2] += dist;
                    }
            } else {
                // RAFAEL
                if (strcmp(ent->classname, "monster_fixbot") == 0) {
                    if (ent->s.frame >= 105 && ent->s.frame <= 120) {
                        if (dz > 12)
                            new_move[2]--;
                        else if (dz < -12)
                            new_move[2]++;
                    } else if (ent->s.frame >= 31 && ent->s.frame <= 88) {
                        if (dz > 12)
                            new_move[2] -= 12;
                        else if (dz < -12)
                            new_move[2] += 12;
                    } else {
                        if (dz > 12)
                            new_move[2] -= 8;
                        else if (dz < -12)
                            new_move[2] += 8;
                    }
                } else {
                // RAFAEL
                    if (dz > 0) {
                        VectorScale(new_move, 0.5f, new_move);
                        new_move[2] -= min(dist, dz);
                    } else if (dz < 0) {
                        VectorScale(new_move, 0.5f, new_move);
                        new_move[2] += -max(-dist, dz);
                    }
                // RAFAEL
                }
                // RAFAEL
            }
        }

        VectorAdd(ent->s.origin, new_move, neworg);

        trace_t trace;
        trap_Trace(&trace, ent->s.origin, ent->r.mins, ent->r.maxs, neworg,
                   ent->s.number, MASK_MONSTERSOLID);

        // fly monsters don't enter water voluntarily
        if (ent->flags & FL_FLY) {
            if (!ent->waterlevel) {
                vec3_t test;
                VectorCopy(trace.endpos, test);
                test[2] += ent->r.mins[2] + 1;
                contents_t contents = trap_PointContents(test);
                if (contents & MASK_WATER)
                    return false;
            }
        }

        // swim monsters don't exit water voluntarily
        if (ent->flags & FL_SWIM) {
            if (ent->waterlevel < WATER_WAIST) {
                vec3_t test;
                VectorCopy(trace.endpos, test);
                test[2] += ent->r.mins[2] + 1;
                contents_t contents = trap_PointContents(test);
                if (!(contents & MASK_WATER))
                    return false;
            }
        }

        // ROGUE
        if ((trace.fraction == 1) && (!trace.allsolid) && (!trace.startsolid)) {
        // ROGUE
            VectorCopy(trace.endpos, ent->s.origin);
            //=====
            // PGM
            if (!current_bad && CheckForBadArea(ent))
                VectorCopy(oldorg, ent->s.origin);
            else {
                if (relink) {
                    trap_LinkEntity(ent);
                    G_TouchTriggers(ent);
                }

                return true;
            }
            // PGM
            //=====
        }

        G_Impact(ent, &trace);

        if (!ent->enemy)
            break;
    }

    return false;
}

/*
=============
SV_movestep

Called by monster program code.
The move will be adjusted for slopes and stairs, but if the move isn't
possible, no move is done, false is returned, and
pr_global_struct->trace_normal is set to the normal of the blocking wall
=============
*/
// FIXME since we need to test end position contents here, can we avoid doing
// it again later in catagorize position?
static bool SV_movestep(edict_t *ent, vec3_t move, bool relink)
{
    //======
    // PGM
    edict_t *current_bad = NULL;

    // PMM - who cares about bad areas if you're dead?
    if (ent->health > 0) {
        current_bad = CheckForBadArea(ent);
        if (current_bad) {
            ent->bad_area = current_bad;

            if (ent->enemy && !strcmp(ent->enemy->classname, "tesla_mine")) {
                // if the tesla is in front of us, back up...
                if (IsBadAhead(ent, current_bad, move))
                    VectorInverse(move);
            }
        } else if (ent->bad_area) {
            // if we're no longer in a bad area, get back to business.
            ent->bad_area = NULL;
            if (ent->oldenemy) { // && ent->bad_area->owner == ent->enemy)
                ent->enemy = ent->oldenemy;
                ent->goalentity = ent->oldenemy;
                FoundTarget(ent);
            }
        }
    }
    // PGM
    //======

    // flying monsters don't step up
    if (ent->flags & (FL_SWIM | FL_FLY))
        return SV_flystep(ent, move, relink, current_bad);

    // try the move
    vec3_t oldorg;
    VectorCopy(ent->s.origin, oldorg);

    float stepsize;

    // push down from a step height above the wished position
    if (ent->spawnflags & SPAWNFLAG_MONSTER_SUPER_STEP && ent->health > 0)
        stepsize = 64;
    else if (!(ent->monsterinfo.aiflags & AI_NOSTEP))
        stepsize = STEPSIZE;
    else
        stepsize = 1;

    stepsize += 0.75f;

    contents_t mask = (ent->r.svflags & SVF_MONSTER) ? MASK_MONSTERSOLID : (MASK_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER);

    vec3_t start_up;
    VectorMA(oldorg, -1 * stepsize, ent->gravityVector, start_up);

    trace_t wtf_trace;
    trap_Trace(&wtf_trace, oldorg, ent->r.mins, ent->r.maxs, start_up, ent->s.number, mask);
    VectorCopy(wtf_trace.endpos, start_up);

    vec3_t end_up;
    VectorAdd(start_up, move, end_up);

    trace_t up_trace;
    trap_Trace(&up_trace, start_up, ent->r.mins, ent->r.maxs, end_up, ent->s.number, mask);

    if (up_trace.startsolid) {
        VectorMA(start_up, -1 * stepsize, ent->gravityVector, start_up);
        trap_Trace(&up_trace, start_up, ent->r.mins, ent->r.maxs, end_up, ent->s.number, mask);
    }

    vec3_t start_fwd, end_fwd;
    VectorCopy(oldorg, start_fwd);
    VectorAdd(start_fwd, move, end_fwd);

    trace_t fwd_trace;
    trap_Trace(&fwd_trace, start_fwd, ent->r.mins, ent->r.maxs, end_fwd, ent->s.number, mask);

    if (fwd_trace.startsolid) {
        VectorMA(start_up, -1 * stepsize, ent->gravityVector, start_up);
        trap_Trace(&fwd_trace, start_fwd, ent->r.mins, ent->r.maxs, end_fwd, ent->s.number, mask);
    }

    // pick the one that went farther
    const trace_t *chosen_forward = (up_trace.fraction > fwd_trace.fraction) ? &up_trace : &fwd_trace;

    if (chosen_forward->startsolid || chosen_forward->allsolid)
        return false;

    int steps = 1;
    bool stepped = false;

    if (up_trace.fraction > fwd_trace.fraction)
        steps = 2;

    // step us down
    vec3_t end;
    VectorMA(chosen_forward->endpos, steps * stepsize, ent->gravityVector, end);
    trace_t trace;
    trap_Trace(&trace, chosen_forward->endpos, ent->r.mins, ent->r.maxs, end, ent->s.number, mask);

    if (fabsf(ent->s.origin[2] - trace.endpos[2]) >= MIN_STEP_HEIGHT)
        stepped = true;

    // Paril: improved the water handling here.
    // monsters are okay with stepping into water
    // up to their waist.
    if (ent->waterlevel <= WATER_WAIST) {
        water_level_t end_waterlevel;
        contents_t    end_watertype;
        M_CategorizePosition(ent, trace.endpos, &end_waterlevel, &end_watertype);

        // don't go into deep liquids or
        // slime/lava voluntarily
        if (end_watertype & (CONTENTS_SLIME | CONTENTS_LAVA) || end_waterlevel > WATER_WAIST)
            return false;
    }

    if (trace.fraction == 1) {
        // if monster had the ground pulled out, go ahead and fall
        if (ent->flags & FL_PARTIALGROUND) {
            VectorAdd(ent->s.origin, move, ent->s.origin);
            if (relink) {
                trap_LinkEntity(ent);
                G_TouchTriggers(ent);
            }
            ent->groundentity = NULL;
            return true;
        }
        // [Paril-KEX] allow dead monsters to "fall" off of edges in their death animation
        if (!(ent->spawnflags & SPAWNFLAG_MONSTER_SUPER_STEP) && ent->health > 0)
            return false; // walked off an edge
    }

    // [Paril-KEX] if we didn't move at all (or barely moved), don't count it
    if (Distance(trace.endpos, oldorg) < VectorLength(move) * 0.05f) {
        ent->monsterinfo.bad_move_time = level.time + SEC(1);

        if (ent->monsterinfo.bump_time < level.time && chosen_forward->fraction < 1.0f) {
            // adjust ideal_yaw to move against the object we hit and try again
            vec3_t fwd;
            AngleVectors((const vec3_t) { 0, ent->ideal_yaw, 0 }, fwd, NULL, NULL);
            vec3_t dir;
            PM_ClipVelocity(fwd, chosen_forward->plane.normal, dir, 1.0f);
            float new_yaw = vectoyaw(dir);

            if (VectorLengthSquared(dir) > 0.1f && ent->ideal_yaw != new_yaw) {
                ent->ideal_yaw = new_yaw;
                ent->monsterinfo.random_change_time = level.time + SEC(0.1f);
                ent->monsterinfo.bump_time = level.time + SEC(0.2f);
                return true;
            }
        }

        return false;
    }

    // check point traces down for dangling corners
    VectorCopy(trace.endpos, ent->s.origin);

    // PGM
    //  PMM - don't bother with bad areas if we're dead
    if (ent->health > 0) {
        // use AI_BLOCKED to tell the calling layer that we're now mad at a tesla
        edict_t *new_bad = CheckForBadArea(ent);
        if (!current_bad && new_bad) {
            if (new_bad->r.ownernum != ENTITYNUM_NONE) {
                edict_t *owner = &g_edicts[new_bad->r.ownernum];
                if (!strcmp(owner->classname, "tesla_mine")) {
                    if (!ent->enemy || !ent->enemy->r.inuse) {
                        TargetTesla(ent, owner);
                        ent->monsterinfo.aiflags |= AI_BLOCKED;
                    } else if (!strcmp(ent->enemy->classname, "tesla_mine")) {
                    } else if (!ent->enemy->client || !visible(ent, ent->enemy)) {
                        TargetTesla(ent, owner);
                        ent->monsterinfo.aiflags |= AI_BLOCKED;
                    }
                }
            }

            VectorCopy(oldorg, ent->s.origin);
            return false;
        }
    }
    // PGM

    if (!M_CheckBottom(ent)) {
        if (ent->flags & FL_PARTIALGROUND) {
            // entity had floor mostly pulled out from underneath it
            // and is trying to correct
            if (relink) {
                trap_LinkEntity(ent);
                G_TouchTriggers(ent);
            }
            return true;
        }

        // walked off an edge that wasn't a stairway
        VectorCopy(oldorg, ent->s.origin);
        return false;
    }

    if (ent->spawnflags & SPAWNFLAG_MONSTER_SUPER_STEP && ent->health > 0) {
        if (!ent->groundentity || ent->groundentity->r.solid == SOLID_BSP) {
            if (g_edicts[trace.entnum].r.solid != SOLID_BSP) {
                // walked off an edge
                VectorCopy(oldorg, ent->s.origin);
                M_CheckGround(ent, G_GetClipMask(ent));
                return false;
            }
        }
    }

    // [Paril-KEX]
    M_CheckGround(ent, G_GetClipMask(ent));

    if (!ent->groundentity) {
        // walked off an edge
        VectorCopy(oldorg, ent->s.origin);
        M_CheckGround(ent, G_GetClipMask(ent));
        return false;
    }

    ent->flags &= ~FL_PARTIALGROUND;
    ent->groundentity = &g_edicts[trace.entnum];
    ent->groundentity_linkcount = ent->groundentity->r.linkcount;

    // the move is ok
    if (relink) {
        trap_LinkEntity(ent);

        // [Paril-KEX] this is something N64 does to avoid doors opening
        // at the start of a level, which triggers some monsters to spawn.
        if (!level.is_n64 || level.time > FRAME_TIME)
            G_TouchTriggers(ent);
    }

    if (stepped && TICK_RATE > 10)
        G_AddEvent(ent, EV_STAIR_STEP, 0);

    if (trace.fraction < 1.0f)
        G_Impact(ent, &trace);

    return true;
}

// check if a movement would succeed
bool ai_check_move(edict_t *self, float dist)
{
    if (ai_movement_disabled.integer)
        return false;

    float yaw = DEG2RAD(self->s.angles[YAW]);
    vec3_t move = {
        cosf(yaw) * dist,
        sinf(yaw) * dist,
    };

    vec3_t old_origin;
    VectorCopy(self->s.origin, old_origin);

    if (!SV_movestep(self, move, false))
        return false;

    VectorCopy(old_origin, self->s.origin);
    trap_LinkEntity(self);
    return true;
}

//============================================================================

/*
===============
M_ChangeYaw

===============
*/
void M_ChangeYaw(edict_t *ent)
{
    float ideal;
    float current;
    float move;
    float speed;

    current = anglemod(ent->s.angles[YAW]);
    ideal = ent->ideal_yaw;

    if (current == ideal)
        return;

    move = ideal - current;
    // [Paril-KEX] high tick rate
    speed = ent->yaw_speed / (TICK_RATE / 10);

    if (ideal > current) {
        if (move >= 180)
            move = move - 360;
    } else {
        if (move <= -180)
            move = move + 360;
    }
    if (move > 0) {
        if (move > speed)
            move = speed;
    } else {
        if (move < -speed)
            move = -speed;
    }

    ent->s.angles[YAW] = anglemod(current + move);
}

/*
======================
SV_StepDirection

Turns to the movement direction, and walks the current distance if
facing it.

======================
*/
static bool SV_StepDirection(edict_t *ent, float yaw, float dist, bool allow_no_turns)
{
    vec3_t move, oldorigin;

    if (!ent->r.inuse)
        return true; // PGM g_touchtrigger free problem

    float old_ideal_yaw = ent->ideal_yaw;
    float old_current_yaw = ent->s.angles[YAW];

    ent->ideal_yaw = yaw;
    M_ChangeYaw(ent);

    yaw = DEG2RAD(yaw);
    move[0] = cosf(yaw) * dist;
    move[1] = sinf(yaw) * dist;
    move[2] = 0;

    VectorCopy(ent->s.origin, oldorigin);
    if (SV_movestep(ent, move, false)) {
        ent->monsterinfo.aiflags &= ~AI_BLOCKED;
        if (!ent->r.inuse)
            return true; // PGM g_touchtrigger free problem

        if (strncmp(ent->classname, "monster_widow", 13)) {
            if (!FacingIdeal(ent)) {
                // not turned far enough, so don't take the step
                // but still turn
                VectorCopy(oldorigin, ent->s.origin);
                M_CheckGround(ent, G_GetClipMask(ent));
                return allow_no_turns; // [Paril-KEX]
            }
        }
        trap_LinkEntity(ent);
        G_TouchTriggers(ent);
        G_TouchProjectiles(ent, oldorigin);
        return true;
    }
    trap_LinkEntity(ent);
    G_TouchTriggers(ent);
    ent->ideal_yaw = old_ideal_yaw;
    ent->s.angles[YAW] = old_current_yaw;
    return false;
}

/*
======================
SV_FixCheckBottom

======================
*/
static void SV_FixCheckBottom(edict_t *ent)
{
    ent->flags |= FL_PARTIALGROUND;
}

/*
================
SV_NewChaseDir

================
*/
#define DI_NODIR    -1

static bool SV_NewChaseDir(edict_t *actor, vec3_t pos, float dist)
{
    float deltax, deltay;
    float d1, d2;
    float tdir, olddir, turnaround;

    olddir = anglemod(truncf(actor->ideal_yaw / 45) * 45);
    turnaround = anglemod(olddir - 180);

    deltax = pos[0] - actor->s.origin[0];
    deltay = pos[1] - actor->s.origin[1];
    if (deltax > 10)
        d1 = 0;
    else if (deltax < -10)
        d1 = 180;
    else
        d1 = DI_NODIR;
    if (deltay < -10)
        d2 = 270;
    else if (deltay > 10)
        d2 = 90;
    else
        d2 = DI_NODIR;

    // try direct route
    if (d1 != DI_NODIR && d2 != DI_NODIR) {
        if (d1 == 0)
            tdir = d2 == 90 ? 45 : 315;
        else
            tdir = d2 == 90 ? 135 : 215;

        if (tdir != turnaround && SV_StepDirection(actor, tdir, dist, false))
            return true;
    }

    // try other directions
    if (brandom() || fabsf(deltay) > fabsf(deltax))
        SWAP(float, d1, d2);

    if (d1 != DI_NODIR && d1 != turnaround && SV_StepDirection(actor, d1, dist, false))
        return true;

    if (d2 != DI_NODIR && d2 != turnaround && SV_StepDirection(actor, d2, dist, false))
        return true;

    // ROGUE
    if (actor->monsterinfo.blocked) {
        if ((actor->r.inuse) && (actor->health > 0) && !(actor->monsterinfo.aiflags & AI_TARGET_ANGER)) {
            // if block "succeeds", the actor will not move or turn.
            if (actor->monsterinfo.blocked(actor, dist)) {
                actor->monsterinfo.move_block_counter = -2;
                return true;
            }

            // we couldn't step; instead of running endlessly in our current
            // spot, try switching to node navigation temporarily to get to
            // where we need to go.
            if (!(actor->monsterinfo.aiflags & (AI_LOST_SIGHT | AI_COMBAT_POINT | AI_TARGET_ANGER | AI_PATHING | AI_TEMP_MELEE_COMBAT | AI_NO_PATH_FINDING))) {
                if (++actor->monsterinfo.move_block_counter > 2) {
                    actor->monsterinfo.aiflags |= AI_TEMP_MELEE_COMBAT;
                    actor->monsterinfo.move_block_change_time = level.time + SEC(3);
                    actor->monsterinfo.move_block_counter = 0;
                }
            }
        }
    }
    // ROGUE

    /* there is no direct path to the player, so pick another direction */

    if (olddir != DI_NODIR && SV_StepDirection(actor, olddir, dist, false))
        return true;

    if (brandom()) { /*randomly determine direction of search*/
        for (tdir = 0; tdir <= 315; tdir += 45)
            if (tdir != turnaround && SV_StepDirection(actor, tdir, dist, false))
                return true;
    } else {
        for (tdir = 315; tdir >= 0; tdir -= 45)
            if (tdir != turnaround && SV_StepDirection(actor, tdir, dist, false))
                return true;
    }

    if (turnaround != DI_NODIR && SV_StepDirection(actor, turnaround, dist, false))
        return true;

    actor->ideal_yaw = frandom1(360); // can't move; pick a random yaw...

    // if a bridge was pulled out from underneath a monster, it may not have
    // a valid standing position at all

    if (!M_CheckBottom(actor))
        SV_FixCheckBottom(actor);

    return false;
}

/*
======================
SV_CloseEnough

======================
*/
bool SV_CloseEnough(edict_t *ent, edict_t *goal, float dist)
{
    for (int i = 0; i < 3; i++) {
        if (goal->r.absmin[i] > ent->r.absmax[i] + dist)
            return false;
        if (goal->r.absmax[i] < ent->r.absmin[i] - dist)
            return false;
    }
    return true;
}

static bool M_NavPathToGoal(edict_t *self, float dist, const vec3_t goal)
{
    // mark us as *trying* now (nav_pos is valid)
    self->monsterinfo.aiflags |= AI_PATHING;

    vec3_t ground_origin, mon_mins, mon_maxs;

    VectorCopy(self->s.origin, ground_origin);
    ground_origin[2] += self->r.mins[2] - player_mins[2];

    VectorAdd(ground_origin, player_mins, mon_mins);
    VectorAdd(ground_origin, player_maxs, mon_maxs);

    vec_t *path_to = self->monsterinfo.nav_path.firstMovePoint;

    if (self->monsterinfo.nav_path_cache_time <= level.time ||
        (self->monsterinfo.nav_path.returnCode != PathReturnCode_TraversalPending &&
         boxes_intersect(mon_mins, mon_maxs, path_to, path_to)))
    {
        PathRequest request = { 0 };
        if (self->enemy)
            VectorCopy(self->enemy->s.origin, request.goal);
        else
            VectorCopy(self->goalentity->s.origin, request.goal);
        request.moveDist = dist;
        if (g_debug_monster_paths.integer == 1)
            request.debugging.drawTime = 1.5f;
        VectorCopy(self->s.origin, request.start);
        request.pathFlags = PathFlags_Walk;

        request.nodeSearch.minHeight = -(self->r.mins[2] * 2);
        request.nodeSearch.maxHeight =  (self->r.maxs[2] * 2);

        // FIXME remove hardcoding
        if (!strcmp(self->classname, "monster_guardian"))
            request.nodeSearch.radius = 2048;
        else
            request.nodeSearch.radius = 512;

        if (self->monsterinfo.can_jump || (self->flags & FL_FLY)) {
            if (self->monsterinfo.jump_height) {
                request.pathFlags |= PathFlags_BarrierJump;
                request.traversals.jumpHeight = self->monsterinfo.jump_height;
            }
            if (self->monsterinfo.drop_height) {
                request.pathFlags |= PathFlags_WalkOffLedge;
                request.traversals.dropHeight = self->monsterinfo.drop_height;
            }
        }

        if (self->flags & FL_FLY) {
            request.nodeSearch.maxHeight = request.nodeSearch.minHeight = 8192;
            request.pathFlags |= PathFlags_LongJump;
        }

        if (!trap_GetPathToGoal(&request, &self->monsterinfo.nav_path, NULL, 0)) {
            // fatal error, don't bother ever trying nodes
            if (self->monsterinfo.nav_path.returnCode == PathReturnCode_NoNavAvailable)
                self->monsterinfo.aiflags |= AI_NO_PATH_FINDING;
            return false;
        }

        self->monsterinfo.nav_path_cache_time = level.time + SEC(2);
    }

    float yaw;
    float old_yaw = self->s.angles[YAW];
    float old_ideal_yaw = self->ideal_yaw;

    path_to = (self->monsterinfo.nav_path.returnCode == PathReturnCode_TraversalPending) ?
        self->monsterinfo.nav_path.secondMovePoint : self->monsterinfo.nav_path.firstMovePoint;

    if (self->monsterinfo.random_change_time >= level.time &&
        !(self->monsterinfo.aiflags & AI_ALTERNATE_FLY))
        yaw = self->ideal_yaw;
    else {
        vec3_t dir;
        VectorSubtract(path_to, self->s.origin, dir);
        yaw = vectoyaw(dir);
    }

    if (!SV_StepDirection(self, yaw, dist, true)) {
        if (!self->r.inuse)
            return false;

        if (self->monsterinfo.blocked && !(self->monsterinfo.aiflags & AI_TARGET_ANGER)) {
            if ((self->r.inuse) && (self->health > 0)) {
                // if we're blocked, the blocked function will be deferred to for yaw
                self->s.angles[YAW] = old_yaw;
                self->ideal_yaw = old_ideal_yaw;
                if (self->monsterinfo.blocked(self, dist))
                    return true;
            }
        }

        // try the first point
        if (self->monsterinfo.random_change_time >= level.time)
            yaw = self->ideal_yaw;
        else {
            vec3_t dir;
            VectorSubtract(self->monsterinfo.nav_path.firstMovePoint, self->s.origin, dir);
            yaw = vectoyaw(dir);
        }

        if (!SV_StepDirection(self, yaw, dist, true)) {
            // we got blocked, but all is not lost yet; do a similar bump around-ish behavior
            // to try to regain our composure
            if (self->monsterinfo.aiflags & AI_BLOCKED) {
                self->monsterinfo.aiflags &= ~AI_BLOCKED;
                return true;
            }

            if (self->monsterinfo.random_change_time < level.time && self->r.inuse) {
                self->monsterinfo.random_change_time = level.time + SEC(1.5f);
                if (SV_NewChaseDir(self, path_to, dist))
                    return true;
            }

            self->monsterinfo.path_blocked_counter += FRAME_TIME * 3;
        }

        if (self->monsterinfo.path_blocked_counter > SEC(1.5f))
            return false;
    }

    return true;
}

/*
=============
M_MoveToPath

Advanced movement code that use the bots pathfinder if allowed and conditions are right.
Feel free to add any other conditions needed.
=============
*/
static bool M_MoveToPath(edict_t *self, float dist)
{
    if (!level.have_path_data)
        return false;
    if (self->flags & FL_STATIONARY)
        return false;
    if (self->monsterinfo.aiflags & AI_NO_PATH_FINDING)
        return false;
    if (self->monsterinfo.path_wait_time > level.time)
        return false;
    if (!self->enemy)
        return false;
    if (M_ClientInvisible(self->enemy))
        return false;
    if (self->monsterinfo.attack_state >= AS_MISSILE)
        return true;

    combat_style_t style = self->monsterinfo.combat_style;

    if (self->monsterinfo.aiflags & AI_TEMP_MELEE_COMBAT)
        style = COMBAT_MELEE;

    if (visible_ex(self, self->enemy, false)) {
        if ((self->flags & (FL_SWIM | FL_FLY)) || style == COMBAT_RANGED) {
            // do the normal "shoot, walk, shoot" behavior...
            return false;
        } else if (style == COMBAT_MELEE) {
            // path pretty close to the enemy, then let normal Quake movement take over.
            if (range_to(self, self->enemy) > 240 ||
                fabsf(self->s.origin[2] - self->enemy->s.origin[2]) > max(self->r.maxs[2], -self->r.mins[2])) {
                if (M_NavPathToGoal(self, dist, self->enemy->s.origin))
                    return true;
                self->monsterinfo.aiflags &= ~AI_TEMP_MELEE_COMBAT;
            } else {
                self->monsterinfo.aiflags &= ~AI_TEMP_MELEE_COMBAT;
                return false;
            }
        } else if (style == COMBAT_MIXED) {
            // most mixed combat AI have fairly short range attacks, so try to path within mid range.
            if (range_to(self, self->enemy) > RANGE_NEAR ||
                fabsf(self->s.origin[2] - self->enemy->s.origin[2]) > max(self->r.maxs[2], -self->r.mins[2]) * 2.0f) {
                if (M_NavPathToGoal(self, dist, self->enemy->s.origin))
                    return true;
            } else {
                return false;
            }
        }
    } else {
        // we can't see our enemy, let's see if we can path to them
        if (M_NavPathToGoal(self, dist, self->enemy->s.origin))
            return true;
    }

    if (!self->r.inuse)
        return false;

    if (self->monsterinfo.nav_path.returnCode > PathReturnCode_StartPathErrors) {
        self->monsterinfo.path_wait_time = level.time + SEC(10);
        return false;
    }

    self->monsterinfo.path_blocked_counter += FRAME_TIME * 3;

    if (self->monsterinfo.path_blocked_counter > SEC(5)) {
        self->monsterinfo.path_blocked_counter = 0;
        self->monsterinfo.path_wait_time = level.time + SEC(5);
        return false;
    }

    return true;
}

/*
======================
M_MoveToGoal
======================
*/
void M_MoveToGoal(edict_t *ent, float dist)
{
    if (ai_movement_disabled.integer) {
        if (!FacingIdeal(ent))
            M_ChangeYaw(ent);
        // mal: don't move, but still face toward target
        return;
    }

    if (!ent->groundentity && !(ent->flags & (FL_FLY | FL_SWIM)))
        return;

    edict_t *goal = ent->goalentity;

    // ???
    if (!goal)
        return;

    // [Paril-KEX] try paths if we can't see the enemy
    if (!(ent->monsterinfo.aiflags & AI_COMBAT_POINT) && ent->monsterinfo.attack_state < AS_MISSILE) {
        if (M_MoveToPath(ent, dist)) {
            ent->monsterinfo.path_blocked_counter = max(0, ent->monsterinfo.path_blocked_counter - FRAME_TIME);
            return;
        }
    }

    ent->monsterinfo.aiflags &= ~AI_PATHING;

    // [Paril-KEX] dumb hack; in some n64 maps, the corners are way too high and
    // I'm too lazy to fix them individually in maps, so here's a game fix..
    if (!(goal->flags & FL_PARTIALGROUND) && !(ent->flags & (FL_FLY | FL_SWIM)) &&
        goal->classname && (!strcmp(goal->classname, "path_corner") || !strcmp(goal->classname, "point_combat"))) {
        vec3_t p;
        VectorCopy(goal->s.origin, p);
        p[2] = ent->s.origin[2];

        if (boxes_intersect(ent->r.absmin, ent->r.absmax, p, p)) {
            // mark this so we don't do it again later
            goal->flags |= FL_PARTIALGROUND;

            if (!boxes_intersect(ent->r.absmin, ent->r.absmax, goal->s.origin, goal->s.origin)) {
                // move it if we would have touched it if the corner was lower
                goal->s.origin[2] = p[2];
                trap_LinkEntity(goal);
            }
        }
    }

    // [Paril-KEX] if we have a straight shot to our target, just move
    // straight instead of trying to stick to invisible guide lines
    if ((ent->monsterinfo.bad_move_time <= level.time || (ent->monsterinfo.aiflags & AI_CHARGING)) && goal) {
        if (!FacingIdeal(ent)) {
            M_ChangeYaw(ent);
            return;
        }

        trace_t tr;
        trap_Trace(&tr, ent->s.origin, NULL, NULL, goal->s.origin, ent->s.number, MASK_MONSTERSOLID);

        if (tr.fraction == 1.0f || tr.entnum == goal->s.number) {
            vec3_t v;
            VectorSubtract(goal->s.origin, ent->s.origin, v);
            if (SV_StepDirection(ent, vectoyaw(v), dist, false))
                return;
        }

        // we didn't make a step, so don't try this for a while
        // *unless* we're going to a path corner
        if (goal->classname && strcmp(goal->classname, "path_corner") && strcmp(goal->classname, "point_combat")) {
            ent->monsterinfo.bad_move_time = level.time + SEC(5);
            ent->monsterinfo.aiflags &= ~AI_CHARGING;
        }
    }

    // bump around...
    if ((ent->monsterinfo.random_change_time <= level.time // random change time is up
         && irandom1(4) == 1 // random bump around
         && !(ent->monsterinfo.aiflags & AI_CHARGING) // PMM - charging monsters (AI_CHARGING) don't deflect unless they have to
         && !((ent->monsterinfo.aiflags & AI_ALTERNATE_FLY) && ent->enemy && !(ent->monsterinfo.aiflags & AI_LOST_SIGHT))) // alternate fly monsters don't do this either unless they have to
        || !SV_StepDirection(ent, ent->ideal_yaw, dist, ent->monsterinfo.bad_move_time > level.time)) {
        if (ent->monsterinfo.aiflags & AI_BLOCKED) {
            ent->monsterinfo.aiflags &= ~AI_BLOCKED;
            return;
        }
        ent->monsterinfo.random_change_time = level.time + random_time_sec(0.5f, 1.0f);
        SV_NewChaseDir(ent, goal->s.origin, dist);
        ent->monsterinfo.move_block_counter = 0;
    } else
        ent->monsterinfo.bad_move_time -= SEC(0.25f);
}

/*
===============
M_walkmove
===============
*/
bool M_walkmove(edict_t *ent, float yaw, float dist)
{
    if (ai_movement_disabled.integer)
        return false;

    if (!ent->groundentity && !(ent->flags & (FL_FLY | FL_SWIM)))
        return false;

    yaw = DEG2RAD(yaw);
    vec3_t move = {
        cosf(yaw) * dist,
        sinf(yaw) * dist,
    };

    // PMM
    bool retval = SV_movestep(ent, move, true);
    ent->monsterinfo.aiflags &= ~AI_BLOCKED;
    return retval;
}
