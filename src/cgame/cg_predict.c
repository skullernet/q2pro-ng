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
        cg.prediction_error = vec3_origin;
        return;
    }

    if (!cg_predict.integer || (cg.frame->ps.pm_flags & PMF_NO_PREDICTION))
        return;

    // calculate the last usercmd_t we sent that the server has processed
    cmd = cg.frame->cmdnum;

    // compare what the server returned with what we had predicted it to be
    delta = Vec3_Sub(cg.frame->ps.origin, cg.predicted_origins[cmd & CMD_MASK]);

    // save the prediction error for interpolation
    len = fabsf(delta.x) + fabsf(delta.y) + fabsf(delta.z);
    if (len < 0x1p-5f || len > 80.0f) {
        // > 80 world units is a teleport or something
        cg.prediction_error = vec3_origin;
        return;
    }

    SHOWMISS("prediction miss on %i: %.f (%.f %.f %.f)\n",
             cg.frame->number, len, delta.x, delta.y, delta.z);

    cg.predicted_origins[cmd & CMD_MASK] = cg.frame->ps.origin;

    // save for error interpolation
    cg.prediction_error = delta;
}

/*
====================
CG_ClipMoveToEntities
====================
*/
static void CG_ClipMoveToEntities(trace_t *tr, const trace_args_t *args)
{
    trace_t     trace;
    qhandle_t   hmodel;

    for (int i = 0; i < cg.num_solid_entities; i++) {
        const centity_t *ent = cg.solid_entities[i];

        if (ent->current.number < cgs.maxclients && !(args->mask & CONTENTS_PLAYER))
            continue;
        if (ent->current.number == args->entnum)
            continue;

        if (ent->current.solid == PACKED_BSP) {
            // special value for bmodel
            hmodel = ent->current.modelindex;
        } else {
            hmodel = trap_TempBoxModel(ent->box);
        }

        if (tr->allsolid)
            return;

        trap_TransformedBoxTrace(&trace, args, hmodel,
                                 ent->current.origin, ent->current.angles);

        CM_ClipEntity(tr, &trace, ent->current.number);
    }
}

/*
================
CG_Trace
================
*/
void CG_TraceArgs(trace_t *tr, const trace_args_t *args)
{
    // check against world
    trap_BoxTrace(tr, args, MODELINDEX_WORLD);
    tr->entnum = ENTITYNUM_WORLD;
    if (tr->fraction == 0)
        return;     // blocked by the world

    // check all other solid models
    CG_ClipMoveToEntities(tr, args);
}

static void CG_ClipArgs(trace_t *tr, const trace_args_t *args)
{
    // only clip to world for now
    trap_BoxTrace(tr, args, MODELINDEX_WORLD);
    tr->entnum = ENTITYNUM_WORLD;
}

contents_t CG_PointContents(vec3_t point)
{
    contents_t contents = trap_PointContents(point, MODELINDEX_WORLD);

    for (int i = 0; i < cg.num_solid_entities; i++) {
        const centity_t *ent = cg.solid_entities[i];

        if (ent->current.solid != PACKED_BSP) // special value for bmodel
            continue;

        contents |= trap_TransformedPointContents(point,
            ent->current.modelindex, ent->current.origin, ent->current.angles);
    }

    return contents;
}

void CG_PredictAngles(void)
{
    usercmd_t cmd;
    if (trap_GetUsercmd(trap_GetUsercmdNumber(), &cmd))
        PM_ClampAngles(&cg.predicted_ps, &cmd);
}

static void CG_RunUsercmd(pmove_t *pm, unsigned frame)
{
    if (trap_GetUsercmd(frame, &pm->cmd))
        BG_Pmove(pm);

    if (cg.predicted_step_frame < frame && fabsf(pm->step_height) >= 2.0f) {
        // check for stepping up before a previous step is completed
        unsigned delta = cgs.realtime - cg.predicted_step_time;
        float prev_step = 0;
        if (delta < STEP_TIME)
            prev_step = cg.predicted_step * (STEP_TIME - delta);

        cg.predicted_step = Q_clipf(prev_step + pm->step_height, -MAX_STEP, MAX_STEP) / STEP_TIME;
        cg.predicted_step_time = cgs.realtime;
        cg.predicted_step_frame = frame;  // don't double step
        SHOWSTEP("%u: step %.f\n", frame, pm->step_height);
    }

    // save for debug checking
    cg.predicted_origins[frame & CMD_MASK] = pm->s->origin;
}

/*
=================
CG_PredictMovement
=================
*/
void CG_PredictMovement(void)
{
    unsigned    ack, current;
    pmove_t     pm;
    int         viewheight;

    if (sv_paused.integer)
        return;

    if (!CG_PredictionEnabled()) {
        // just set angles
        cg.predicted_ps = cg.frame->ps;
        CG_PredictAngles();
        return;
    }

    current = trap_GetUsercmdNumber();
    ack = cg.frame->cmdnum;

    // if we are too far out of date, just freeze
    if (current - ack > CMD_BACKUP) {
        SHOWMISS("%i: exceeded CMD_BACKUP\n", cg.frame->number);
        return;
    }

    viewheight = cg.predicted_ps.viewheight;
    cg.predicted_ps = cg.frame->ps;

    if (current == ack) {
        SHOWMISS("%i: not moved\n", cg.frame->number);
        return;
    }

    // copy current state to pmove
    memset(&pm, 0, sizeof(pm));
    pm.trace = CG_TraceArgs;
    pm.clip = CG_ClipArgs;
    pm.pointcontents = CG_PointContents;
    pm.s = &cg.predicted_ps;

    // run frames
    while (++ack <= current)
        CG_RunUsercmd(&pm, ack);

    // check for ducking
    if (cg.predicted_ps.viewheight != viewheight) {
        cg.duck_time = cg.time + DUCK_TIME;
        cg.duck_factor = (float)(cg.predicted_ps.viewheight - viewheight) / DUCK_TIME;
    }
}
