// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"

//====
// PGM
#define SPAWNFLAGS_PLAT2_TOGGLE         2
#define SPAWNFLAGS_PLAT2_TOP            4
#define SPAWNFLAGS_PLAT2_START_ACTIVE   8
#define SPAWNFLAGS_PLAT2_BOX_LIFT       32
// PGM
//====

void plat2_go_down(edict_t *ent);
void plat2_go_up(edict_t *ent);

void plat2_spawn_danger_area(edict_t *ent)
{
    vec3_t mins, maxs;

    VectorCopy(ent->r.mins, mins);
    VectorCopy(ent->r.maxs, maxs);
    maxs[2] = ent->r.mins[2] + 64;

    SpawnBadArea(mins, maxs, 0, ent);
}

void plat2_kill_danger_area(edict_t *ent)
{
    edict_t *t;

    t = NULL;
    while ((t = G_Find(t, FOFS(classname), "bad_area"))) {
        if (t->r.ownernum == ent->s.number)
            G_FreeEdict(t);
    }
}

void MOVEINFO_ENDFUNC(plat2_hit_top)(edict_t *ent)
{
    if (!(ent->flags & FL_TEAMSLAVE) && ent->moveinfo.sound_end)
        G_AddEvent(ent, EV_SOUND, ent->moveinfo.sound_end);
    ent->s.sound = 0;
    ent->moveinfo.state = STATE_TOP;

    if (ent->plat2flags & PLAT2_CALLED) {
        ent->plat2flags = PLAT2_WAITING;
        if (!(ent->spawnflags & SPAWNFLAGS_PLAT2_TOGGLE)) {
            ent->think = plat2_go_down;
            ent->nextthink = level.time + SEC(ent->wait * 2.5f);
        }
        if (deathmatch.integer)
            ent->last_move_time = level.time - SEC(ent->wait * 0.5f);
        else
            ent->last_move_time = level.time - SEC(ent->wait);
    } else if (!(ent->spawnflags & SPAWNFLAGS_PLAT2_TOP) && !(ent->spawnflags & SPAWNFLAGS_PLAT2_TOGGLE)) {
        ent->plat2flags = PLAT2_NONE;
        ent->think = plat2_go_down;
        ent->nextthink = level.time + SEC(ent->wait);
        ent->last_move_time = level.time;
    } else {
        ent->plat2flags = PLAT2_NONE;
        ent->last_move_time = level.time;
    }

    G_UseTargets(ent, ent);
}

void MOVEINFO_ENDFUNC(plat2_hit_bottom)(edict_t *ent)
{
    if (!(ent->flags & FL_TEAMSLAVE) && ent->moveinfo.sound_end)
        G_AddEvent(ent, EV_SOUND, ent->moveinfo.sound_end);
    ent->s.sound = 0;
    ent->moveinfo.state = STATE_BOTTOM;

    if (ent->plat2flags & PLAT2_CALLED) {
        ent->plat2flags = PLAT2_WAITING;
        if (!(ent->spawnflags & SPAWNFLAGS_PLAT2_TOGGLE)) {
            ent->think = plat2_go_up;
            ent->nextthink = level.time + SEC(ent->wait * 2.5f);
        }
        if (deathmatch.integer)
            ent->last_move_time = level.time - SEC(ent->wait * 0.5f);
        else
            ent->last_move_time = level.time - SEC(ent->wait);
    } else if ((ent->spawnflags & SPAWNFLAGS_PLAT2_TOP) && !(ent->spawnflags & SPAWNFLAGS_PLAT2_TOGGLE)) {
        ent->plat2flags = PLAT2_NONE;
        ent->think = plat2_go_up;
        ent->nextthink = level.time + SEC(ent->wait);
        ent->last_move_time = level.time;
    } else {
        ent->plat2flags = PLAT2_NONE;
        ent->last_move_time = level.time;
    }

    plat2_kill_danger_area(ent);
    G_UseTargets(ent, ent);
}

void THINK(plat2_go_down)(edict_t *ent)
{
    if (!(ent->flags & FL_TEAMSLAVE) && ent->moveinfo.sound_start)
        G_AddEvent(ent, EV_SOUND, ent->moveinfo.sound_start);

    ent->s.sound = ent->moveinfo.sound_middle;

    ent->moveinfo.state = STATE_DOWN;
    ent->plat2flags |= PLAT2_MOVING;

    Move_Calc(ent, ent->moveinfo.end_origin, plat2_hit_bottom);
}

void THINK(plat2_go_up)(edict_t *ent)
{
    if (!(ent->flags & FL_TEAMSLAVE) && ent->moveinfo.sound_start)
        G_AddEvent(ent, EV_SOUND, ent->moveinfo.sound_start);

    ent->s.sound = ent->moveinfo.sound_middle;

    ent->moveinfo.state = STATE_UP;
    ent->plat2flags |= PLAT2_MOVING;

    plat2_spawn_danger_area(ent);

    Move_Calc(ent, ent->moveinfo.start_origin, plat2_hit_top);
}

static void plat2_operate(edict_t *ent, edict_t *other)
{
    int      otherState;
    gtime_t  pauseTime;
    float    platCenter;
    edict_t *trigger;

    trigger = ent;
    ent = ent->enemy; // now point at the plat, not the trigger

    if (ent->plat2flags & PLAT2_MOVING)
        return;

    if ((ent->last_move_time + SEC(ent->wait)) > level.time)
        return;

    platCenter = (trigger->r.absmin[2] + trigger->r.absmax[2]) / 2;

    if (ent->moveinfo.state == STATE_TOP) {
        otherState = STATE_TOP;
        if (ent->spawnflags & SPAWNFLAGS_PLAT2_BOX_LIFT) {
            if (platCenter > other->s.origin[2])
                otherState = STATE_BOTTOM;
        } else {
            if (trigger->r.absmax[2] > other->s.origin[2])
                otherState = STATE_BOTTOM;
        }
    } else {
        otherState = STATE_BOTTOM;
        if (other->s.origin[2] > platCenter)
            otherState = STATE_TOP;
    }

    ent->plat2flags = PLAT2_MOVING;

    if (deathmatch.integer)
        pauseTime = SEC(0.3f);
    else
        pauseTime = SEC(0.5f);

    if (ent->moveinfo.state != otherState) {
        ent->plat2flags |= PLAT2_CALLED;
        pauseTime = SEC(0.1f);
    }

    ent->last_move_time = level.time;

    if (ent->moveinfo.state == STATE_BOTTOM) {
        ent->think = plat2_go_up;
        ent->nextthink = level.time + pauseTime;
    } else {
        ent->think = plat2_go_down;
        ent->nextthink = level.time + pauseTime;
    }
}

void TOUCH(Touch_Plat_Center2)(edict_t *ent, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    // this requires monsters to actively trigger plats, not just step on them.

    // FIXME - commented out for E3
    // if (!other->client)
    //  return;

    if (other->health <= 0)
        return;

    // PMM - don't let non-monsters activate plat2s
    if ((!(other->r.svflags & SVF_MONSTER)) && (!other->client))
        return;

    plat2_operate(ent, other);
}

void MOVEINFO_BLOCKED(plat2_blocked)(edict_t *self, edict_t *other)
{
    if (!(other->r.svflags & SVF_MONSTER) && (!other->client)) {
        // give it a chance to go away on it's own terms (like gibs)
        T_Damage(other, self, self, vec3_origin, other->s.origin, 0, 100000, 1, DAMAGE_NONE, (mod_t) { MOD_CRUSH });
        // if it's still there, nuke it
        if (other && other->r.inuse && other->r.solid)
            BecomeExplosion1(other);
        return;
    }

    // gib dead things
    if (other->health < 1)
        T_Damage(other, self, self, vec3_origin, other->s.origin, 0, 100, 1, DAMAGE_NONE, (mod_t) { MOD_CRUSH });

    T_Damage(other, self, self, vec3_origin, other->s.origin, 0, self->dmg, 1, DAMAGE_NONE, (mod_t) { MOD_CRUSH });

    // [Paril-KEX] killed, so don't change direction
    if (!other->r.inuse || !other->r.solid)
        return;

    if (self->moveinfo.state == STATE_UP)
        plat2_go_down(self);
    else if (self->moveinfo.state == STATE_DOWN)
        plat2_go_up(self);
}

void USE(Use_Plat2)(edict_t *ent, edict_t *other, edict_t *activator)
{
    int i;

    if (ent->moveinfo.state > STATE_BOTTOM)
        return;
    // [Paril-KEX] disabled this; causes confusing situations
    //if ((ent->last_move_time + SEC(2)) > level.time)
    //  return;

    for (i = game.maxclients + BODY_QUEUE_SIZE; i < level.num_edicts; i++) {
        edict_t *trigger = g_edicts + i;
        if (!trigger->r.inuse)
            continue;
        if (trigger->touch == Touch_Plat_Center2 && trigger->enemy == ent) {
            plat2_operate(trigger, activator);
            return;
        }
    }
}

void USE(plat2_activate)(edict_t *ent, edict_t *other, edict_t *activator)
{
    edict_t *trigger;

    ent->use = Use_Plat2;

    trigger = plat_spawn_inside_trigger(ent); // the "start moving" trigger

    trigger->r.maxs[0] += 10;
    trigger->r.maxs[1] += 10;
    trigger->r.mins[0] -= 10;
    trigger->r.mins[1] -= 10;

    trap_LinkEntity(trigger);

    trigger->touch = Touch_Plat_Center2; // Override trigger touch function

    plat2_go_down(ent);
}

/*QUAKED func_plat2 (0 .5 .8) ? PLAT_LOW_TRIGGER PLAT2_TOGGLE PLAT2_TOP PLAT2_START_ACTIVE UNUSED BOX_LIFT
speed   default 150

PLAT_LOW_TRIGGER - creates a short trigger field at the bottom
PLAT2_TOGGLE - plat will not return to default position.
PLAT2_TOP - plat's default position will the the top.
PLAT2_START_ACTIVE - plat will trigger it's targets each time it hits top
UNUSED
BOX_LIFT - this indicates that the lift is a box, rather than just a platform

Plats are always drawn in the extended position, so they will light correctly.

If the plat is the target of another trigger or button, it will start out disabled in the extended position until it is trigger, when it will lower and become a normal plat.

"speed" overrides default 200.
"accel" overrides default 500
"lip"   no default

If the "height" key is set, that will determine the amount the plat moves, instead of being implicitly determoveinfoned by the model's height.

*/
void SP_func_plat2(edict_t *ent)
{
    edict_t *trigger;

    VectorClear(ent->s.angles);
    ent->r.solid = SOLID_BSP;
    ent->movetype = MOVETYPE_PUSH;

    trap_SetBrushModel(ent, ent->model);

    ent->moveinfo.blocked = plat2_blocked;

    if (!ent->speed)
        ent->speed = 20;
    else
        ent->speed *= 0.1f;

    if (!ent->accel)
        ent->accel = 5;
    else
        ent->accel *= 0.1f;

    if (!ent->decel)
        ent->decel = 5;
    else
        ent->decel *= 0.1f;

    if (!ent->wait)
        ent->wait = 2.0f;

    if (deathmatch.integer) {
        ent->speed *= 2;
        ent->accel *= 2;
        ent->decel *= 2;
    }

    // PMM Added to kill things it's being blocked by
    if (!ent->dmg)
        ent->dmg = 2;

    // pos1 is the top position, pos2 is the bottom
    VectorCopy(ent->s.origin, ent->pos1);
    VectorCopy(ent->s.origin, ent->pos2);

    if (st.height)
        ent->pos2[2] -= (st.height - st.lip);
    else
        ent->pos2[2] -= (ent->r.maxs[2] - ent->r.mins[2]) - st.lip;

    ent->moveinfo.state = STATE_TOP;

    if (ent->targetname && !(ent->spawnflags & SPAWNFLAGS_PLAT2_START_ACTIVE)) {
        ent->use = plat2_activate;
    } else {
        ent->use = Use_Plat2;

        trigger = plat_spawn_inside_trigger(ent); // the "start moving" trigger

        // PGM - debugging??
        trigger->r.maxs[0] += 10;
        trigger->r.maxs[1] += 10;
        trigger->r.mins[0] -= 10;
        trigger->r.mins[1] -= 10;

        trap_LinkEntity(trigger);

        trigger->touch = Touch_Plat_Center2; // Override trigger touch function

        if (!(ent->spawnflags & SPAWNFLAGS_PLAT2_TOP)) {
            VectorCopy(ent->pos2, ent->s.origin);
            ent->moveinfo.state = STATE_BOTTOM;
        }
    }

    trap_LinkEntity(ent);

    G_SetMoveinfoParams(ent);

    G_SetMoveinfoSounds(ent, "plats/pt1_strt.wav", "plats/pt1_mid.wav", "plats/pt1_end.wav");
}
