// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_newtrig.c
// pmack
// october 1997

#include "g_local.h"

/*QUAKED info_teleport_destination (.5 .5 .5) (-16 -16 -24) (16 16 32)
Destination marker for a teleporter.
*/
void SP_info_teleport_destination(edict_t *self)
{
}

//#define SPAWNFLAG_TELEPORT_PLAYER_ONLY    1
//#define SPAWNFLAG_TELEPORT_SILENT         2
//#define SPAWNFLAG_TELEPORT_CTF_ONLY       4
#define SPAWNFLAG_TELEPORT_START_ON         8

/*QUAKED trigger_teleport (.5 .5 .5) ? player_only silent ctf_only start_on
Any object touching this will be transported to the corresponding
info_teleport_destination entity. You must set the "target" field,
and create an object with a "targetname" field that matches.

If the trigger_teleport has a targetname, it will only teleport
entities when it has been fired.

player_only: only players are teleported
silent: <not used right now>
ctf_only: <not used right now>
start_on: when trigger has targetname, start active, deactivate when used.
*/
void TOUCH(trigger_teleport_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    edict_t *dest;

    if (/*(self->spawnflags & SPAWNFLAG_TELEPORT_PLAYER_ONLY) &&*/ !(other->client))
        return;

    if (self->delay)
        return;

    dest = G_PickTarget(self->target);
    if (!dest) {
        G_Printf("Teleport Destination not found!\n");
        return;
    }

    G_TempEntity(other->s.origin, EV_TELEPORT_EFFECT, 0);

    VectorCopy(dest->s.origin, other->s.origin);
    VectorCopy(dest->s.origin, other->s.old_origin);
    other->s.origin[2] += 10;

    // clear the velocity and hold them in place briefly
    VectorClear(other->velocity);

    if (other->client) {
        other->client->ps.pm_time = 160; // hold time
        other->client->ps.pm_flags |= PMF_TIME_TELEPORT;
        other->client->ps.rdflags ^= RDF_TELEPORT_BIT;

        // draw the teleport splash at source and on the player
        G_AddEvent(other, EV_PLAYER_TELEPORT, 0);

        // set angles
        for (int i = 0; i < 3; i++)
            other->client->ps.delta_angles[i] = ANGLE2SHORT(dest->s.angles[i] - other->client->resp.cmd_angles[i]);

        VectorCopy(dest->s.angles, other->client->ps.viewangles);
        VectorCopy(dest->s.angles, other->client->v_angle);
    }

    VectorCopy(dest->s.angles, other->s.angles);

    trap_LinkEntity(other);

    // kill anything at the destination
    KillBox(other, !!other->client);

    // [Paril-KEX] move sphere, if we own it
    if (other->client && other->client->owned_sphere) {
        edict_t *sphere = other->client->owned_sphere;
        VectorCopy(other->s.origin, sphere->s.origin);
        sphere->s.origin[2] = other->r.absmax[2];
        sphere->s.angles[YAW] = other->s.angles[YAW];
        trap_LinkEntity(sphere);
    }
}

void USE(trigger_teleport_use)(edict_t *self, edict_t *other, edict_t *activator)
{
    if (self->delay)
        self->delay = 0;
    else
        self->delay = 1;
}

void SP_trigger_teleport(edict_t *self)
{
    if (!self->wait)
        self->wait = 0.2f;

    self->delay = 0;

    if (self->targetname) {
        self->use = trigger_teleport_use;
        if (!(self->spawnflags & SPAWNFLAG_TELEPORT_START_ON))
            self->delay = 1;
    }

    self->touch = trigger_teleport_touch;

    self->r.svflags = SVF_NOCLIENT;
    self->r.solid = SOLID_TRIGGER;
    self->movetype = MOVETYPE_NONE;

    if (!VectorEmpty(self->s.angles))
        G_SetMovedir(self->s.angles, self->movedir);

    trap_SetBrushModel(self, self->model);
    trap_LinkEntity(self);
}

// ***************************
// TRIGGER_DISGUISE
// ***************************

/*QUAKED trigger_disguise (.5 .5 .5) ? TOGGLE START_ON REMOVE
Anything passing through this trigger when it is active will
be marked as disguised.

TOGGLE - field is turned off and on when used. (Paril N.B.: always the case)
START_ON - field is active when spawned.
REMOVE - field removes the disguise
*/

//#define SPAWNFLAG_DISGUISE_TOGGLE 1
#define SPAWNFLAG_DISGUISE_START_ON 2
#define SPAWNFLAG_DISGUISE_REMOVE   4

void TOUCH(trigger_disguise_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    if (other->client) {
        if (self->spawnflags & SPAWNFLAG_DISGUISE_REMOVE)
            other->flags &= ~FL_DISGUISED;
        else
            other->flags |= FL_DISGUISED;
    }
}

void USE(trigger_disguise_use)(edict_t *self, edict_t *other, edict_t *activator)
{
    if (self->r.solid == SOLID_NOT)
        self->r.solid = SOLID_TRIGGER;
    else
        self->r.solid = SOLID_NOT;

    trap_LinkEntity(self);
}

void SP_trigger_disguise(edict_t *self)
{
    if (!level.disguise_icon)
        level.disguise_icon = G_ImageIndex("i_disguise");

    if (self->spawnflags & SPAWNFLAG_DISGUISE_START_ON)
        self->r.solid = SOLID_TRIGGER;
    else
        self->r.solid = SOLID_NOT;

    self->touch = trigger_disguise_touch;
    self->use = trigger_disguise_use;
    self->movetype = MOVETYPE_NONE;
    self->r.svflags = SVF_NOCLIENT;

    trap_SetBrushModel(self, self->model);
    trap_LinkEntity(self);
}
