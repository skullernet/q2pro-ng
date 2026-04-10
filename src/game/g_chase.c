// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "g_local.h"

void UpdateChaseCam(edict_t *ent)
{
    vec3_t   o, ownerv, goal;
    edict_t *targ;
    vec3_t   forward, right;
    trace_t  trace;
    vec3_t   angles;

    // is our chase target gone?
    if (!ent->client->chase_target->r.inuse || ent->client->chase_target->client->resp.spectator) {
        edict_t *old = ent->client->chase_target;
        ChaseNext(ent);
        if (ent->client->chase_target == old) {
            ent->client->chase_target = NULL;
            ent->client->ps.pm_flags &= ~PMF_NO_PREDICTION;
            return;
        }
    }

    targ = ent->client->chase_target;

    ownerv = targ->s.origin;

    ownerv.z += targ->viewheight;

    angles = targ->client->v_angle;
    if (angles.pitch > 56)
        angles.pitch = 56;
    AngleVectors(angles, &forward, &right, NULL);
    o = Vec3_MA(ownerv, -30, forward);

    if (o.z < targ->s.origin.z + 20)
        o.z = targ->s.origin.z + 20;

    // jump animation lifts
    if (!targ->groundentity)
        o.z += 16;

    trace = G_TraceLine(ownerv, o, targ->s.number, MASK_SOLID);

    goal = Vec3_MA(trace.endpos, 2, forward);

    // pad for floors and ceilings
    o = goal;
    o.z += 6;
    trace = G_TraceLine(goal, o, targ->s.number, MASK_SOLID);
    if (trace.fraction < 1) {
        goal = trace.endpos;
        goal.z -= 6;
    }

    o = goal;
    o.z -= 6;
    trace = G_TraceLine(goal, o, targ->s.number, MASK_SOLID);
    if (trace.fraction < 1) {
        goal = trace.endpos;
        goal.z += 6;
    }

    if (targ->deadflag)
        ent->client->ps.pm_type = PM_DEAD;
    else
        ent->client->ps.pm_type = PM_FREEZE;

    ent->s.origin = goal;

    if (targ->deadflag) {
        ent->client->ps.viewangles.roll = 40;
        ent->client->ps.viewangles.pitch = -15;
        ent->client->ps.viewangles.yaw = targ->client->killer_yaw;
    } else {
        P_SetClientAngles(ent->client, targ->client->v_angle);
    }

    ent->viewheight = 0;
    ent->client->ps.pm_flags |= PMF_NO_PREDICTION;
    trap_LinkEntity(ent);
}

void ChaseNext(edict_t *ent)
{
    int      i;
    edict_t *e;

    if (!ent->client->chase_target)
        return;

    i = ent->client->chase_target - g_edicts;
    do {
        i++;
        if (i >= game.maxclients)
            i = 0;
        e = g_edicts + i;
        if (!e->r.inuse)
            continue;
        if (!e->client->resp.spectator)
            break;
    } while (e != ent->client->chase_target);

    ent->client->chase_target = e;
    ent->client->update_chase = true;
}

void ChasePrev(edict_t *ent)
{
    int      i;
    edict_t *e;

    if (!ent->client->chase_target)
        return;

    i = ent->client->chase_target - g_edicts;
    do {
        i--;
        if (i < 0)
            i = game.maxclients - 1;
        e = g_edicts + i;
        if (!e->r.inuse)
            continue;
        if (!e->client->resp.spectator)
            break;
    } while (e != ent->client->chase_target);

    ent->client->chase_target = e;
    ent->client->update_chase = true;
}

void GetChaseTarget(edict_t *ent)
{
    int      i;
    edict_t *other;

    for (i = 0; i < game.maxclients; i++) {
        other = g_edicts + i;
        if (other->r.inuse && !other->client->resp.spectator) {
            ent->client->chase_target = other;
            ent->client->update_chase = true;
            UpdateChaseCam(ent);
            return;
        }
    }

    if (ent->client->chase_msg_time <= level.time) {
        G_ClientPrintf(ent, PRINT_CENTER, "No other players to chase.");
        ent->client->chase_msg_time = level.time + SEC(5);
    }
}
