/*
Copyright (C) 2025 Andrey Nazarov

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

static void CG_SetActiveState(void)
{
    // initialize oldframe so lerping doesn't hurt anything
    cg.oldframe = cg.frame;
    cg.weapon.prev_frame = cg.frame->ps.gunframe;

    // set initial cg.predicted_ps
    cg.predicted_ps = cg.frame->ps;
    if (cg.frame->ps.pm_type < PM_DEAD) {
        // enhanced servers don't send viewangles
        CG_PredictAngles();
    }
    VectorCopy(cg.predicted_ps.viewangles, cg.oldviewangles);

    SCR_LagClear();
}

static void CG_SetClientTime(void)
{
    int prevtime, servertime;

    if (sv_paused.integer)
        return;

    if (!cg.frame)
        return;

    if (com_timedemo.integer) {
        cg.time = cg.frame->servertime;
        cg.lerpfrac = 1.0f;
        return;
    }

    prevtime = cg.oldframe->servertime;
    servertime = cg.frame->servertime;
    if (prevtime >= servertime) {
        SHOWCLAMP(2, "reset time %i\n", servertime);
        cg.time = servertime;
        cg.lerpfrac = 1.0f;
    } else if (cg.time > servertime) {
        SHOWCLAMP(2, "high clamp %i\n", cg.time - servertime);
        cg.time = servertime;
        cg.lerpfrac = 1.0f;
    } else if (cg.time < prevtime) {
        SHOWCLAMP(2, "low clamp %i\n", prevtime - cg.time);
        cg.time = prevtime;
        cg.lerpfrac = 0;
    } else {
        cg.lerpfrac = (float)(cg.time - prevtime) / (servertime - prevtime);
    }

    SHOWCLAMP(3, "time %d %d, lerpfrac %.3f\n",
              cg.time, servertime, cg.lerpfrac);
}

void CG_ProcessFrames(void)
{
    unsigned current = trap_GetServerFrameNumber();

    if (cg.serverframe > current)
        cg.serverframe = current;

    // read and process all pending server frames
    while (cg.serverframe < current) {
        cg_server_frame_t *frame = &cg.frames[++cg.serverframe & 1];

        if (!trap_GetServerFrame(cg.serverframe, frame)) {
            // frame was dropped, invalid or too old
            SCR_LagSample(NULL);
            continue;
        }

        cg.oldframe = cg.frame;
        cg.frame = frame;

        // once valid, oldframe will never turn invalid, but can be copy of
        // current if dropped
        if (!cg.oldframe)
            CG_SetActiveState();
        else if (cg.oldframe->number != cg.serverframe - 1)
            cg.oldframe = cg.frame;

        SCR_LagSample(frame);
        CG_DeltaFrame();
    }

    CG_SetClientTime();
}
