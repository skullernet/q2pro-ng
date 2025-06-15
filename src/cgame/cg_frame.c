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


static cg_server_frame_t *CG_ReadNextFrame(void)
{
    cg_server_frame_t *frame = &cg.frames[0];
    if (frame == cg.oldframe)
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
    unsigned time;

    trap_GetServerFrameNumber(&cg.current_framenum, &time);
    cg.time=time;

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
        CG_DeltaFrame();
    }
}
