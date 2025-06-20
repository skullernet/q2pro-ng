#include "cg_local.h"

static void CG_SetActiveState(void)
{
    // initialize oldframe so lerping doesn't hurt anything
    cg.oldframe = cg.frame;

    if (!cgs.demoplayback) {
        // set initial cg.predicted_origin and cg.predicted_angles
        VectorCopy(cg.frame->ps.pmove.origin, cg.predicted_origin);
        VectorCopy(cg.frame->ps.pmove.velocity, cg.predicted_velocity);
        if (cg.frame->ps.pmove.pm_type < PM_DEAD) {
            // enhanced servers don't send viewangles
            CG_PredictAngles();
        } else {
            // just use what server provided
            VectorCopy(cg.frame->ps.viewangles, cg.predicted_angles);
        }
    }

    SCR_LagClear();
}

static void CG_SetClientTime(void)
{
    int prevtime;

    if (sv_paused.integer)
        return;

    if (com_timedemo.integer) {
        cg.time = cg.servertime;
        cg.lerpfrac = 1.0f;
        return;
    }

    prevtime = cg.oldframe->servertime;
    if (prevtime >= cg.servertime) {
        SHOWCLAMP(2, "bad time %i\n", prevtime);
        cg.time = prevtime;
        cg.lerpfrac = 0;
    } else if (cg.time > cg.servertime) {
        SHOWCLAMP(2, "high clamp %i\n", cg.time - cg.servertime);
        cg.time = cg.servertime;
        cg.lerpfrac = 1.0f;
    } else if (cg.time < prevtime) {
        SHOWCLAMP(2, "low clamp %i\n", prevtime - cg.time);
        cg.time = prevtime;
        cg.lerpfrac = 0;
    } else {
        cg.lerpfrac = (float)(cg.time - prevtime) / (cg.servertime - prevtime);
    }

    SHOWCLAMP(3, "time %d %d, lerpfrac %.3f\n",
              cg.time, cg.servertime, cg.lerpfrac);
}

static cg_server_frame_t *CG_ReadNextFrame(void)
{
    cg_server_frame_t *frame = &cg.frames[0];
    if (frame == cg.frame)
        frame = &cg.frames[1];

    while (cg.processed_framenum < cg.current_framenum) {
        cg.processed_framenum++;

        if (trap_GetServerFrame(cg.processed_framenum, frame)) {
            SCR_LagSample(frame);
            return frame;
        }

        SCR_LagSample(NULL);
    }

    return NULL;
}

void CG_ProcessFrames(void)
{
    trap_GetServerFrameNumber(&cg.current_framenum, &cg.servertime);

    if (!cg.frame) {
        cg.frame = CG_ReadNextFrame();
        if (!cg.frame)
            return;
        CG_SetActiveState();
    }

    cg_server_frame_t *frame = CG_ReadNextFrame();
    if (frame) {
        cg.oldframe = cg.frame;
        cg.frame = frame;
    }

    CG_SetClientTime();

    if (frame)
        CG_DeltaFrame();
}
