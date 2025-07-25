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

    VectorCopy(targ->s.origin, ownerv);

    ownerv[2] += targ->viewheight;

    VectorCopy(targ->client->v_angle, angles);
    if (angles[PITCH] > 56)
        angles[PITCH] = 56;
    AngleVectors(angles, forward, right, NULL);
    VectorMA(ownerv, -30, forward, o);

    if (o[2] < targ->s.origin[2] + 20)
        o[2] = targ->s.origin[2] + 20;

    // jump animation lifts
    if (!targ->groundentity)
        o[2] += 16;

    trap_Trace(&trace, ownerv, NULL, NULL, o, targ->s.number, MASK_SOLID);

    VectorCopy(trace.endpos, goal);

    VectorMA(goal, 2, forward, goal);

    // pad for floors and ceilings
    VectorCopy(goal, o);
    o[2] += 6;
    trap_Trace(&trace, goal, NULL, NULL, o, targ->s.number, MASK_SOLID);
    if (trace.fraction < 1) {
        VectorCopy(trace.endpos, goal);
        goal[2] -= 6;
    }

    VectorCopy(goal, o);
    o[2] -= 6;
    trap_Trace(&trace, goal, NULL, NULL, o, targ->s.number, MASK_SOLID);
    if (trace.fraction < 1) {
        VectorCopy(trace.endpos, goal);
        goal[2] += 6;
    }

    if (targ->deadflag)
        ent->client->ps.pm_type = PM_DEAD;
    else
        ent->client->ps.pm_type = PM_FREEZE;

    VectorCopy(goal, ent->s.origin);
    for (int i = 0; i < 3; i++)
        ent->client->ps.delta_angles[i] = ANGLE2SHORT(targ->client->v_angle[i] - ent->client->resp.cmd_angles[i]);

    if (targ->deadflag) {
        ent->client->ps.viewangles[ROLL] = 40;
        ent->client->ps.viewangles[PITCH] = -15;
        ent->client->ps.viewangles[YAW] = targ->client->killer_yaw;
    } else {
        VectorCopy(targ->client->v_angle, ent->client->ps.viewangles);
        VectorCopy(targ->client->v_angle, ent->client->v_angle);
        AngleVectors(ent->client->v_angle, ent->client->v_forward, NULL, NULL);
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
