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

#include "cg_local.h"

/*
===================
CG_CheckPredictionError
===================
*/
void CG_CheckPredictionError(void)
{
    vec3_t      delta;
    unsigned    cmd;
    float       len;

    if (cgs.demoplayback)
        return;

    if (sv_paused.integer) {
        VectorClear(cg.prediction_error);
        return;
    }

    if (!cg_predict.integer || (cg.frame->ps.pmove.pm_flags & PMF_NO_PREDICTION))
        return;

    // calculate the last usercmd_t we sent that the server has processed
    trap_GetUsercmdNumber(&cmd, NULL);

    // compare what the server returned with what we had predicted it to be
    VectorSubtract(cg.frame->ps.pmove.origin, cg.predicted_origins[cmd & CMD_MASK], delta);

    // save the prediction error for interpolation
    len = fabsf(delta[0]) + fabsf(delta[1]) + fabsf(delta[2]);
    if (len < 0x1p-5f || len > 80.0f) {
        // > 80 world units is a teleport or something
        VectorClear(cg.prediction_error);
        return;
    }

    SHOWMISS("prediction miss on %i: %.f (%.f %.f %.f)\n",
             cg.frame->number, len, delta[0], delta[1], delta[2]);

    VectorCopy(cg.frame->ps.pmove.origin, cg.predicted_origins[cmd & CMD_MASK]);

    // save for error interpolation
    VectorCopy(delta, cg.prediction_error);
}

/*
====================
CG_ClipMoveToEntities
====================
*/
static void CG_ClipMoveToEntities(trace_t *tr, const vec3_t start, const vec3_t end,
                                  const vec3_t mins, const vec3_t maxs, contents_t contentmask)
{
    trace_t     trace;
    qhandle_t   hmodel;

    for (int i = 0; i < cg.numSolidEntities; i++) {
        const centity_t *ent = cg.solidEntities[i];

        if (ent->current.number <= cg.maxclients && !(contentmask & CONTENTS_PLAYER))
            continue;

        if (ent->current.solid == PACKED_BSP) {
            // special value for bmodel
            hmodel = ent->current.modelindex;
        } else {
            hmodel = trap_TempBoxModel(ent->mins, ent->maxs);
        }

        if (tr->allsolid)
            return;

        trap_TransformedBoxTrace(&trace, start, end,
                                 mins, maxs, hmodel, contentmask,
                                 ent->current.origin, ent->current.angles);

        trap_ClipEntity(tr, &trace, ent->current.number);
    }
}

/*
================
CG_Trace
================
*/
void CG_Trace(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs,
              const vec3_t end, unsigned passent, contents_t contentmask)
{
    if (!mins)
        mins = vec3_origin;
    if (!maxs)
        maxs = vec3_origin;

    // check against world
    trap_BoxTrace(tr, start, end, mins, maxs, MODELINDEX_WORLD, contentmask);
    tr->entnum = ENTITYNUM_WORLD;
    if (tr->fraction == 0)
        return;     // blocked by the world

    // check all other solid models
    CG_ClipMoveToEntities(tr, start, end, mins, maxs, contentmask);
}

static void CG_Clip(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs,
                    const vec3_t end, unsigned clipent, contents_t contentmask)
{
    // only clip to world for now
    trap_BoxTrace(tr, start, end, mins, maxs, MODELINDEX_WORLD, contentmask);
    tr->entnum = ENTITYNUM_WORLD;
}

static contents_t CG_PointContents(const vec3_t point)
{
    contents_t contents = trap_PointContents(point, MODELINDEX_WORLD);

    for (int i = 0; i < cg.numSolidEntities; i++) {
        const centity_t *ent = cg.solidEntities[i];

        if (ent->current.solid != PACKED_BSP) // special value for bmodel
            continue;

        contents |= trap_TransformedPointContents(point,
            ent->current.modelindex, ent->current.origin, ent->current.angles);
    }

    return contents;
}

/*
=================
CG_PredictMovement

Sets cg.predicted_origin and cg.predicted_angles
=================
*/
void CG_PredictAngles(void)
{
    unsigned current;
    usercmd_t cmd;

    trap_GetUsercmdNumber(NULL, &current);
    trap_GetUsercmd(current, &cmd);

    for (int i = 0; i < 3; i++)
        cg.predicted_angles[i] = SHORT2ANGLE((short)(cmd.angles[i] + cg.frame->ps.pmove.delta_angles[i]));
}

static void CG_RunUsercmd(pmove_t *pm, unsigned frame)
{
    trap_GetUsercmd(frame, &pm->cmd);
    BG_Pmove(pm);

    if (cg.predicted_step_frame < frame && pm->groundentity != ENTITYNUM_NONE && fabsf(pm->step_height) > 1.0f) {
        // check for stepping up before a previous step is completed
        unsigned delta = cgs.realtime - cg.predicted_step_time;
        float prev_step = 0;
        if (delta < 100)
            prev_step = cg.predicted_step * (100 - delta) * 0.01f;

        cg.predicted_step = Q_clipf(prev_step + pm->step_height, -32, 32);
        cg.predicted_step_time = cgs.realtime;
        cg.predicted_step_frame = frame;  // don't double step
        SHOWSTEP("%u: step %.3f\n", frame, pm->step_height);
    }

    // save for debug checking
    VectorCopy(pm->s.origin, cg.predicted_origins[frame & CMD_MASK]);
}

void CG_PredictMovement(void)
{
    unsigned    ack, current;
    pmove_t     pm;

    if (cgs.demoplayback)
        return;

    if (sv_paused.integer)
        return;

    if (!cg_predict.integer || (cg.frame->ps.pmove.pm_flags & PMF_NO_PREDICTION)) {
        // just set angles
        CG_PredictAngles();
        return;
    }

    trap_GetUsercmdNumber(&ack, &current);

    // if we are too far out of date, just freeze
    if (current - ack > CMD_BACKUP) {
        SHOWMISS("%i: exceeded CMD_BACKUP\n", cg.frame->number);
        return;
    }

    if (current == ack) {
        SHOWMISS("%i: not moved\n", cg.frame->number);
        return;
    }

    // copy current state to pmove
    memset(&pm, 0, sizeof(pm));
    pm.trace = CG_Trace;
    pm.clip = CG_Clip;
    pm.pointcontents = CG_PointContents;
    pm.s = cg.frame->ps.pmove;
    VectorCopy(cg.frame->ps.viewoffset, pm.viewoffset);
    pm.snapinitial = true;

    // run frames
    while (++ack <= current) {
        CG_RunUsercmd(&pm, ack);
        pm.snapinitial = false;
    }

    // copy results out for rendering
    VectorCopy(pm.s.origin, cg.predicted_origin);
    VectorCopy(pm.s.velocity, cg.predicted_velocity);
    VectorCopy(pm.viewangles, cg.predicted_angles);
}
