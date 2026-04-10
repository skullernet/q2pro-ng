// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"

//============
// ROGUE
/*
=============
SV_Physics_NewToss

Toss, bounce, and fly movement. When on ground and no velocity, do nothing. With velocity,
slide.
=============
*/
void SV_Physics_NewToss(edict_t *ent)
{
    trace_t  trace;
    vec3_t   move;
    edict_t *slave;
    bool     wasinwater;
    bool     isinwater;
    float    speed, newspeed;
    vec3_t   old_origin;

    // regular thinking
    SV_RunThink(ent);

    // if not a team captain, so movement will be handled elsewhere
    if (ent->flags & FL_TEAMSLAVE)
        return;

    wasinwater = ent->waterlevel;

    // find out what we're sitting on.
    move = ent->s.origin;
    move.z -= 0.25f;
    trace = G_Trace(ent->s.origin, move, ent->r.box, ent->s.number, ent->clipmask);
    if (ent->groundentity && ent->groundentity->r.inuse)
        ent->groundentity = &g_edicts[trace.entnum];
    else
        ent->groundentity = NULL;

    // if we're sitting on something flat and have no velocity of our own, return.
    if (ent->groundentity && (trace.plane.normal.z == 1.0f) && Vec3_IsEmpty(ent->velocity))
        return;

    // store the old origin
    old_origin = ent->s.origin;

    SV_CheckVelocity(ent);

    // add gravity
    SV_AddGravity(ent);

    if (!Vec3_IsEmpty(ent->avelocity))
        SV_AddRotationalFriction(ent);

    // add friction
    speed = Vec3_Length(ent->velocity);
    if (ent->waterlevel) // friction for water movement
        newspeed = speed - (sv_waterfriction * 6 * ent->waterlevel);
    else if (!ent->groundentity) // friction for air movement
        newspeed = speed - sv_friction;
    else // use ground friction
        newspeed = speed - (sv_friction * 6);

    if (newspeed < 0)
        newspeed = 0;
    newspeed /= speed;
    ent->velocity = Vec3_Scale(ent->velocity, newspeed);

    SV_FlyMove(ent, FRAME_TIME_SEC, ent->clipmask);
    trap_LinkEntity(ent);

    G_TouchTriggers(ent);

    // check for water transition
    wasinwater = (ent->watertype & MASK_WATER);
    ent->watertype = trap_PointContents(ent->s.origin);
    isinwater = ent->watertype & MASK_WATER;

    if (isinwater)
        ent->waterlevel = WATER_FEET;
    else
        ent->waterlevel = WATER_NONE;

    if (!wasinwater && isinwater)
        G_PositionedSound(old_origin, CHAN_AUTO, G_SoundIndex("misc/h2ohit1.wav"), 1, ATTN_NORM);
    else if (wasinwater && !isinwater)
        G_PositionedSound(ent->s.origin, CHAN_AUTO, G_SoundIndex("misc/h2ohit1.wav"), 1, ATTN_NORM);

    // move teamslaves
    for (slave = ent->teamchain; slave; slave = slave->teamchain) {
        slave->s.origin = ent->s.origin;
        trap_LinkEntity(slave);
    }
}

// ROGUE
//============
