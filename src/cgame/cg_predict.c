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
    cg_predicted_origin_t *pred;
    vec3_t      delta;
    unsigned    cmd;
    float       len;

    cg.prediction_error = vec3_origin;

    if (!CG_PredictionEnabled())
        return;

    // calculate the last usercmd_t we sent that the server has processed
    cmd = cg.frame->cmdnum;
    pred = &cg.predicted_origins[cmd & CMD_MASK];

    // ignore any error if teleported
    if (cg.teleported) {
        pred->origin = cg.frame->ps.origin;
        return;
    }

    // was it ever predicted?
    if (pred->cmd != cmd) {
        SHOWMISS("%i: unpredicted usercmd acknowledged\n", cg.frame->number);
        return;
    }

    // compare what the server returned with what we had predicted it to be
    delta = Vec3_Sub(cg.frame->ps.origin, pred->origin);

    // save the prediction error for interpolation
    len = Vec3_Length(delta);
    if (len < 0x1p-5f)
        return;

    SHOWMISS("prediction miss on %i: %.f (%.f %.f %.f)\n",
             cg.frame->number, len, delta.x, delta.y, delta.z);

    pred->origin = cg.frame->ps.origin;

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
        const cg_entity_t *ent = cg.solid_entities[i];

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

        trace = trap_TransformedBoxTrace(args,
            hmodel, ent->current.origin, ent->current.angles);

        CM_ClipEntity(tr, &trace, ent->current.number);
    }
}

/*
================
CG_Trace
================
*/
trace_t CG_TraceArgs(const trace_args_t *args)
{
    // check against world
    trace_t tr = trap_BoxTrace(args, MODELINDEX_WORLD);
    tr.entnum = ENTITYNUM_WORLD;
    if (tr.fraction == 0)
        return tr;  // blocked by the world

    // check all other solid models
    CG_ClipMoveToEntities(&tr, args);
    return tr;
}

static trace_t CG_ClipArgs(const trace_args_t *args)
{
    // only clip to world for now
    trace_t tr = trap_BoxTrace(args, MODELINDEX_WORLD);
    tr.entnum = ENTITYNUM_WORLD;
    return tr;
}

contents_t CG_PointContents(vec3_t point)
{
    contents_t contents = trap_PointContents(point, MODELINDEX_WORLD);

    for (int i = 0; i < cg.num_solid_entities; i++) {
        const cg_entity_t *ent = cg.solid_entities[i];

        if (ent->current.solid != PACKED_BSP) // special value for bmodel
            continue;

        contents |= trap_TransformedPointContents(point,
            ent->current.modelindex, ent->current.origin, ent->current.angles);
    }

    return contents;
}

void CG_PredictAngles(void)
{
    // grab the most recent cmd
    usercmd_t cmd;
    unsigned current = trap_GetUsercmdNumber();
    if (trap_GetUsercmd(current + 1, &cmd) || trap_GetUsercmd(current, &cmd))
        PM_ClampAngles(&cg.predicted_ps, &cmd);
}

static void CG_RunUsercmd(pmove_t *pm, unsigned frame, bool save)
{
    // too old, or no pending cmd
    if (!trap_GetUsercmd(frame, &pm->cmd))
        return;

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
    if (save)
        cg.predicted_origins[frame & CMD_MASK] = (cg_predicted_origin_t){ pm->s->origin, frame };
}

/*
=================
CG_PredictMovement
=================
*/
void CG_PredictMovement(void)
{
    unsigned    ack, current;
    int         viewheight;

    if (sv_paused.integer)
        return;

    if (!cg.frame)
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

    // copy current state to pmove
    pmove_t pm = {
        .trace = CG_TraceArgs,
        .clip = CG_ClipArgs,
        .pointcontents = CG_PointContents,
        .s = &cg.predicted_ps
    };

    // run frames
    while (++ack <= current)
        CG_RunUsercmd(&pm, ack, true);

    // run pending cmd
    CG_RunUsercmd(&pm, ack, false);

    // check for ducking
    if (cg.predicted_ps.viewheight != viewheight && !cg.teleported) {
        cg.duck_time = cg.time + DUCK_TIME;
        cg.duck_factor = (float)(cg.predicted_ps.viewheight - viewheight) / DUCK_TIME;
    }
}
