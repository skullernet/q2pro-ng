// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"

/*QUAKED rotating_light (0 .5 .8) (-8 -8 -8) (8 8 8) START_OFF ALARM
"health"    if set, the light may be killed.
*/

// RAFAEL
// note to self
// the lights will take damage from explosions
// this could leave a player in total darkness very bad

#define SPAWNFLAG_ROTATING_LIGHT_START_OFF  1
#define SPAWNFLAG_ROTATING_LIGHT_ALARM      2

void THINK(rotating_light_alarm)(edict_t *self)
{
    if (self->spawnflags & SPAWNFLAG_ROTATING_LIGHT_START_OFF) {
        self->think = NULL;
        self->nextthink = 0;
    } else {
        G_StartSound(self, CHAN_VOICE, self->noise_index, 1, ATTN_STATIC);
        self->nextthink = level.time + SEC(1);
    }
}

void DIE(rotating_light_killed)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t point, mod_t mod)
{
    G_BecomeEvent(self, EV_WELDING_SPARKS, MakeLittleLong(0, irandom2(0xe0, 0xe8), 30, 0));
}

void USE(rotating_light_use)(edict_t *self, edict_t *other, edict_t *activator)
{
    if (self->spawnflags & SPAWNFLAG_ROTATING_LIGHT_START_OFF) {
        self->spawnflags &= ~SPAWNFLAG_ROTATING_LIGHT_START_OFF;
        self->s.effects |= EF_SPINNINGLIGHTS;

        if (self->spawnflags & SPAWNFLAG_ROTATING_LIGHT_ALARM) {
            self->think = rotating_light_alarm;
            self->nextthink = level.time + FRAME_TIME;
        }
    } else {
        self->spawnflags |= SPAWNFLAG_ROTATING_LIGHT_START_OFF;
        self->s.effects &= ~EF_SPINNINGLIGHTS;
    }
}

void SP_rotating_light(edict_t *self)
{
    self->movetype = MOVETYPE_STOP;
    self->r.solid = SOLID_BBOX;

    self->s.modelindex = G_ModelIndex("models/objects/light/tris.md2");

    self->s.frame = 0;

    self->use = rotating_light_use;

    if (self->spawnflags & SPAWNFLAG_ROTATING_LIGHT_START_OFF)
        self->s.effects &= ~EF_SPINNINGLIGHTS;
    else
        self->s.effects |= EF_SPINNINGLIGHTS;

    if (!self->speed)
        self->speed = 32;
    // this is a real cheap way
    // to set the radius of the light
    // self->s.frame = self->speed;

    if (!self->health) {
        self->health = 10;
        self->max_health = self->health;
        self->die = rotating_light_killed;
        self->takedamage = true;
    } else {
        self->max_health = self->health;
        self->die = rotating_light_killed;
        self->takedamage = true;
    }

    if (self->spawnflags & SPAWNFLAG_ROTATING_LIGHT_ALARM)
        self->noise_index = G_SoundIndex("misc/alarm.wav");

    trap_LinkEntity(self);
}

/*QUAKED func_object_repair (1 .5 0) (-8 -8 -8) (8 8 8)
object to be repaired.
The default delay is 1 second
"delay" the delay in seconds for spark to occur
*/

void THINK(object_repair_fx)(edict_t *ent)
{
    ent->nextthink = level.time + SEC(ent->delay);

    if (ent->health <= 100)
        ent->health++;
    else
        G_AddEvent(ent, EV_WELDING_SPARKS, MakeLittleLong(0, irandom2(0xe0, 0xe8), 10, 0));
}

void THINK(object_repair_dead)(edict_t *ent)
{
    G_UseTargets(ent, ent);
    ent->nextthink = level.time + HZ(10);
    ent->think = object_repair_fx;
}

void THINK(object_repair_sparks)(edict_t *ent)
{
    if (ent->health <= 0) {
        ent->nextthink = level.time + HZ(10);
        ent->think = object_repair_dead;
        return;
    }

    ent->nextthink = level.time + SEC(ent->delay);

    G_AddEvent(ent, EV_WELDING_SPARKS, MakeLittleLong(0, irandom2(0xe0, 0xe8), 10, 0));
}

void SP_object_repair(edict_t *ent)
{
    ent->movetype = MOVETYPE_NONE;
    ent->r.solid = SOLID_NOT;
    ent->classname = "object_repair";
    VectorSet(ent->r.mins, -8, -8, 8);
    VectorSet(ent->r.maxs, 8, 8, 8);
    ent->think = object_repair_sparks;
    ent->nextthink = level.time + SEC(1);
    ent->health = 100;
    if (!ent->delay)
        ent->delay = 1.0f;
    trap_LinkEntity(ent);
}
