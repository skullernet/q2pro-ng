// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "g_local.h"

/*
===============
G_UpdateChaseCam
===============
*/
void G_UpdateChaseCam(edict_t *ent)
{
    edict_t *targ = ent->client->chase_target;

    // is our chase target gone?
    if (!targ->r.inuse || targ->client->resp.spectator) {
        ChaseNext(ent);
        if (ent->client->chase_target == targ) {
            G_StopChaseCam(ent);
            return;
        }
    }

    // copy playerstate, inluding clientnum
    ent->client->ps = targ->client->ps;

    ent->client->ps.pm_type = PM_FREEZE;
    ent->client->ps.pm_flags |= PMF_NO_PREDICTION;

    ent->client->v_angle = targ->client->v_angle;
    ent->client->v_forward = targ->client->v_forward;

    ent->s.origin = targ->s.origin;
    ent->s.angles = targ->s.angles;
    ent->viewheight = targ->viewheight;

    G_SetSpectatorStats(ent);
}

/*
===============
G_StopChaseCam
===============
*/
void G_StopChaseCam(edict_t *ent)
{
    ent->client->chase_target = NULL;
    ent->client->ps.pm_type = PM_SPECTATOR;
    ent->client->ps.pm_flags &= ~PMF_NO_PREDICTION;
    ent->client->ps.clientnum = ent->s.number;
    ent->client->ps.gunindex = 0;
    ent->client->ps.gunskin = 0;
    ent->client->ps.viewheight = ent->viewheight = 22;
    ent->client->ps.velocity = ent->velocity = vec3_origin;
    P_SetClientAngles(ent->client, ent->client->ps.viewangles);
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
    FOR_EACH_PLAYER(other) {
        if (!other->client->resp.spectator) {
            ent->client->chase_target = other;
            ent->client->update_chase = true;
            G_UpdateChaseCam(ent);
            return;
        }
    }

    if (ent->client->chase_msg_time <= level.time) {
        G_ClientPrintf(ent, PRINT_CENTER, "No other players to chase.");
        ent->client->chase_msg_time = level.time + Time_FromSec(5);
    }
}
