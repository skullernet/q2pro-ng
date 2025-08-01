// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "g_local.h"

// PGM - some of these are mine, some id's. I added the define's.
#define SPAWNFLAG_TRIGGER_MONSTER       1
#define SPAWNFLAG_TRIGGER_NOT_PLAYER    2
#define SPAWNFLAG_TRIGGER_TRIGGERED     4
#define SPAWNFLAG_TRIGGER_TOGGLE        8
#define SPAWNFLAG_TRIGGER_LATCHED       16
#define SPAWNFLAG_TRIGGER_CLIP          32
// PGM

static void InitTrigger(edict_t *self)
{
    if (ED_WasKeySpecified("angle") || ED_WasKeySpecified("angles") || !VectorEmpty(self->s.angles))
        G_SetMovedir(self->s.angles, self->movedir);

    self->r.solid = SOLID_TRIGGER;
    self->movetype = MOVETYPE_NONE;
    // [Paril-KEX] adjusted to allow mins/maxs to be defined
    // by hand instead
    if (self->model)
        trap_SetBrushModel(self, self->model);
    self->r.svflags = SVF_NOCLIENT;
}

// the wait time has passed, so set back up for another activation
void THINK(multi_wait)(edict_t *ent)
{
    ent->nextthink = 0;
}

// the trigger was just activated
// ent->activator should be set to the activator so it can be held through a delay
// so wait for the delay time before firing
static void multi_trigger(edict_t *ent)
{
    if (ent->nextthink)
        return; // already been triggered

    G_UseTargets(ent, ent->activator);

    if (ent->wait > 0) {
        ent->think = multi_wait;
        ent->nextthink = level.time + SEC(ent->wait);
    } else {
        // we can't just remove (self) here, because this is a touch function
        // called while looping through area links...
        ent->touch = NULL;
        ent->nextthink = level.time + FRAME_TIME;
        ent->think = G_FreeEdict;
    }
}

void USE(Use_Multi)(edict_t *ent, edict_t *other, edict_t *activator)
{
    // PGM
    if (ent->spawnflags & SPAWNFLAG_TRIGGER_TOGGLE) {
        if (ent->r.solid == SOLID_TRIGGER)
            ent->r.solid = SOLID_NOT;
        else
            ent->r.solid = SOLID_TRIGGER;
        trap_LinkEntity(ent);
    } else {
        ent->activator = activator;
        multi_trigger(ent);
    }
    // PGM
}

void TOUCH(Touch_Multi)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    if (other->client) {
        if (self->spawnflags & SPAWNFLAG_TRIGGER_NOT_PLAYER)
            return;
    } else if (other->r.svflags & SVF_MONSTER) {
        if (!(self->spawnflags & SPAWNFLAG_TRIGGER_MONSTER))
            return;
    } else
        return;

    if (!G_BrushModelClip(self, other))
        return;

    if (!VectorEmpty(self->movedir)) {
        vec3_t forward;

        AngleVectors(other->s.angles, forward, NULL, NULL);
        if (DotProduct(forward, self->movedir) < 0)
            return;
    }

    self->activator = other;
    multi_trigger(self);
}

/*QUAKED trigger_multiple (.5 .5 .5) ? MONSTER NOT_PLAYER TRIGGERED TOGGLE LATCHED
Variable sized repeatable trigger.  Must be targeted at one or more entities.
If "delay" is set, the trigger waits some time after activating before firing.
"wait" : Seconds between triggerings. (.2 default)

TOGGLE - using this trigger will activate/deactivate it. trigger will begin inactive.

sounds
1)  secret
2)  beep beep
3)  large switch
4)
set "message" to text string
*/
void USE(trigger_enable)(edict_t *self, edict_t *other, edict_t *activator)
{
    self->r.solid = SOLID_TRIGGER;
    self->use = Use_Multi;
    trap_LinkEntity(self);
}

void THINK(latched_trigger_think)(edict_t *self)
{
    self->nextthink = level.time + FRAME_TIME;

    int list[MAX_EDICTS_OLD];
    int count = trap_BoxEdicts(self->r.absmin, self->r.absmax, list, q_countof(list), AREA_SOLID);
    bool any_inside = false;

    for (int i = 0; i < count; i++) {
        edict_t *other = g_edicts + list[i];

        if (other->client) {
            if (self->spawnflags & SPAWNFLAG_TRIGGER_NOT_PLAYER)
                continue;
        } else if (other->r.svflags & SVF_MONSTER) {
            if (!(self->spawnflags & SPAWNFLAG_TRIGGER_MONSTER))
                continue;
        } else
            continue;

        if (!VectorEmpty(self->movedir)) {
            vec3_t forward;

            AngleVectors(other->s.angles, forward, NULL, NULL);
            if (DotProduct(forward, self->movedir) < 0)
                continue;
        }

        self->activator = other;
        any_inside = true;
        break;
    }

    if (!!self->count != any_inside) {
        G_UseTargets(self, self->activator);
        self->count = any_inside;
    }
}

void SP_trigger_multiple(edict_t *ent)
{
    // [Paril-KEX] PSX
    if (st.noise && *st.noise)
        ent->noise_index = G_SoundIndex(st.noise);
    else if (ent->sounds == 1)
        ent->noise_index = G_SoundIndex("misc/secret.wav");
    else if (ent->sounds == 2)
        ent->noise_index = G_SoundIndex("misc/talk.wav");
    else if (ent->sounds == 3)
        ent->noise_index = G_SoundIndex("misc/trigger1.wav");

    if (!ent->wait)
        ent->wait = 0.2f;

    InitTrigger(ent);

    if (ent->spawnflags & SPAWNFLAG_TRIGGER_LATCHED) {
        if (ent->spawnflags & (SPAWNFLAG_TRIGGER_TRIGGERED | SPAWNFLAG_TRIGGER_TOGGLE))
            G_Printf("%s: latched and triggered/toggle are not supported\n", etos(ent));

        ent->think = latched_trigger_think;
        ent->nextthink = level.time + FRAME_TIME;
        ent->use = Use_Multi;
        return;
    }

    if (ent->model || !VectorEmpty(ent->r.mins) || !VectorEmpty(ent->r.maxs))
        ent->touch = Touch_Multi;

    // PGM
    if (ent->spawnflags & (SPAWNFLAG_TRIGGER_TRIGGERED | SPAWNFLAG_TRIGGER_TOGGLE)) {
    // PGM
        ent->r.solid = SOLID_NOT;
        ent->use = trigger_enable;
    } else {
        ent->r.solid = SOLID_TRIGGER;
        ent->use = Use_Multi;
    }

    trap_LinkEntity(ent);

    if (ent->spawnflags & SPAWNFLAG_TRIGGER_CLIP)
        ent->r.svflags |= SVF_HULL;
}

/*QUAKED trigger_once (.5 .5 .5) ? x x TRIGGERED
Triggers once, then removes itself.
You must set the key "target" to the name of another object in the level that has a matching "targetname".

If TRIGGERED, this trigger must be triggered before it is live.

sounds
 1) secret
 2) beep beep
 3) large switch
 4)

"message"   string to be displayed when triggered
*/

void SP_trigger_once(edict_t *ent)
{
    // make old maps work because I messed up on flag assignments here
    // triggered was on bit 1 when it should have been on bit 4
    if (ent->spawnflags & SPAWNFLAG_TRIGGER_MONSTER) {
        ent->spawnflags &= ~SPAWNFLAG_TRIGGER_MONSTER;
        ent->spawnflags |= SPAWNFLAG_TRIGGER_TRIGGERED;
        G_Printf("%s: fixed TRIGGERED flag\n", etos(ent));
    }

    ent->wait = -1;
    SP_trigger_multiple(ent);
}

/*QUAKED trigger_relay (.5 .5 .5) (-8 -8 -8) (8 8 8)
This fixed size trigger cannot be touched, it can only be fired by other events.
*/
#define SPAWNFLAGS_TRIGGER_RELAY_NO_SOUND   1

void USE(trigger_relay_use)(edict_t *self, edict_t *other, edict_t *activator)
{
    if (self->crosslevel_flags && self->crosslevel_flags != (game.cross_level_flags & SFL_CROSS_TRIGGER_MASK & self->crosslevel_flags))
        return;

    G_UseTargets(self, activator);
}

void SP_trigger_relay(edict_t *self)
{
    self->use = trigger_relay_use;

    if (self->spawnflags & SPAWNFLAGS_TRIGGER_RELAY_NO_SOUND)
        self->noise_index = -1;
}

/*
==============================================================================

trigger_key

==============================================================================
*/

#define SPAWNFLAGS_TRIGGER_KEY_BECOME_RELAY 1

/*QUAKED trigger_key (.5 .5 .5) (-8 -8 -8) (8 8 8)
A relay trigger that only fires it's targets if player has the proper key.
Use "item" to specify the required key, for example "key_data_cd"
*/
void USE(trigger_key_use)(edict_t *self, edict_t *other, edict_t *activator)
{
    item_id_t index;

    if (!self->item)
        return;
    if (!activator->client)
        return;

    index = self->item->id;
    if (!activator->client->pers.inventory[index]) {
        if (level.time < self->touch_debounce_time)
            return;
        self->touch_debounce_time = level.time + SEC(5);
        G_ClientPrintf(activator, PRINT_CENTER, "You need %s", self->item->pickup_name_definite);
        G_StartSound(activator, CHAN_AUTO, G_SoundIndex("misc/keytry.wav"), 1, ATTN_NORM);
        return;
    }

    G_StartSound(activator, CHAN_AUTO, G_SoundIndex("misc/keyuse.wav"), 1, ATTN_NORM);
    if (coop.integer) {
        edict_t *ent;

        if (index == IT_KEY_POWER_CUBE || index == IT_KEY_EXPLOSIVE_CHARGES) {
            int cube;

            for (cube = 0; cube < 8; cube++)
                if (activator->client->pers.power_cubes & (1 << cube))
                    break;

            for (int player = 0; player < game.maxclients; player++) {
                ent = &g_edicts[player];
                if (!ent->r.inuse)
                    continue;
                if (!ent->client)
                    continue;
                if (ent->client->pers.power_cubes & (1 << cube)) {
                    ent->client->pers.inventory[index]--;
                    ent->client->pers.power_cubes &= ~(1 << cube);

                    // [Paril-KEX] don't allow respawning players to keep
                    // used keys
                    if (!P_UseCoopInstancedItems()) {
                        ent->client->resp.coop_respawn.inventory[index] = 0;
                        ent->client->resp.coop_respawn.power_cubes &= ~(1 << cube);
                    }
                }
            }
        } else {
            for (int player = 0; player < game.maxclients; player++) {
                ent = &g_edicts[player];
                if (!ent->r.inuse)
                    continue;
                if (!ent->client)
                    continue;
                ent->client->pers.inventory[index] = 0;

                // [Paril-KEX] don't allow respawning players to keep
                // used keys
                if (!P_UseCoopInstancedItems())
                    ent->client->resp.coop_respawn.inventory[index] = 0;
            }
        }
    } else {
        activator->client->pers.inventory[index]--;
    }

    G_UseTargets(self, activator);

    if (self->spawnflags & SPAWNFLAGS_TRIGGER_KEY_BECOME_RELAY)
        self->use = trigger_relay_use;
    else
        self->use = NULL;
}

void SP_trigger_key(edict_t *self)
{
    if (!st.item) {
        G_Printf("%s: no key item\n", etos(self));
        return;
    }
    self->item = FindItemByClassname(st.item);

    if (!self->item) {
        G_Printf("%s: item %s not found\n", etos(self), st.item);
        return;
    }

    if (!self->target) {
        G_Printf("%s: no target\n", etos(self));
        return;
    }

    G_SoundIndex("misc/keytry.wav");
    G_SoundIndex("misc/keyuse.wav");

    self->use = trigger_key_use;
}

/*
==============================================================================

trigger_counter

==============================================================================
*/

/*QUAKED trigger_counter (.5 .5 .5) ? nomessage
Acts as an intermediary for an action that takes multiple inputs.

If nomessage is not set, t will print "1 more.. " etc when triggered and "sequence complete" when finished.

After the counter has been triggered "count" times (default 2), it will fire all of it's targets and remove itself.
*/

#define SPAWNFLAG_COUNTER_NOMESSAGE 1

void USE(trigger_counter_use)(edict_t *self, edict_t *other, edict_t *activator)
{
    if (self->count == 0)
        return;

    self->count--;

    if (self->count) {
        if (!(self->spawnflags & SPAWNFLAG_COUNTER_NOMESSAGE)) {
            G_ClientPrintf(activator, PRINT_CENTER, "%d more to go...", self->count);
            G_StartSound(activator, CHAN_AUTO, G_SoundIndex("misc/talk1.wav"), 1, ATTN_NORM);
        }
        return;
    }

    if (!(self->spawnflags & SPAWNFLAG_COUNTER_NOMESSAGE)) {
        G_ClientPrintf(activator, PRINT_CENTER, "Sequence completed!");
        G_StartSound(activator, CHAN_AUTO, G_SoundIndex("misc/talk1.wav"), 1, ATTN_NORM);
    }
    self->activator = activator;
    multi_trigger(self);
}

void SP_trigger_counter(edict_t *self)
{
    self->wait = -1;
    if (!self->count)
        self->count = 2;

    self->use = trigger_counter_use;
}

/*
==============================================================================

trigger_always

==============================================================================
*/

/*QUAKED trigger_always (.5 .5 .5) (-8 -8 -8) (8 8 8)
This trigger will always fire.  It is activated by the world.
*/
void SP_trigger_always(edict_t *ent)
{
    // we must have some delay to make sure our use targets are present
    if (!ent->delay)
        ent->delay = 0.2f;
    G_UseTargets(ent, ent);
}

/*
==============================================================================

trigger_push

==============================================================================
*/

// PGM
#define SPAWNFLAG_PUSH_ONCE         1
#define SPAWNFLAG_PUSH_PLUS         2
#define SPAWNFLAG_PUSH_SILENT       4
#define SPAWNFLAG_PUSH_START_OFF    8
#define SPAWNFLAG_PUSH_CLIP         16
#define SPAWNFLAG_PUSH_ADDITIVE     32
// PGM

void TOUCH(trigger_push_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    if (!G_BrushModelClip(self, other))
        return;

    if (strcmp(other->classname, "grenade") == 0 || other->health > 0) {
        if (self->spawnflags & SPAWNFLAG_PUSH_ADDITIVE) {
            float max_speed = self->speed * 10;
            if (DotProduct(other->velocity, self->movedir) < max_speed) {
                float speed_adjust = max_speed * FRAME_TIME_SEC * 2;
                VectorMA(other->velocity, speed_adjust, self->movedir, other->velocity);
                other->no_gravity_time = level.time + SEC(0.1f);
            }
        } else
            VectorScale(self->movedir, self->speed * 10, other->velocity);

        if (other->client) {
            // don't take falling damage immediately from this
            VectorCopy(other->velocity, other->client->oldvelocity);
            other->client->oldgroundentity = other->groundentity;
            if (!(self->spawnflags & SPAWNFLAG_PUSH_SILENT) && (other->fly_sound_debounce_time < level.time)) {
                other->fly_sound_debounce_time = level.time + SEC(1.5f);
                G_StartSound(other, CHAN_AUTO, G_SoundIndex("misc/windfly.wav"), 1, ATTN_NORM);
            }
        }
    }

    SV_CheckVelocity(other);

    if (self->spawnflags & SPAWNFLAG_PUSH_ONCE)
        G_FreeEdict(self);
}

//======
// PGM
void USE(trigger_push_use)(edict_t *self, edict_t *other, edict_t *activator)
{
    if (self->r.solid == SOLID_NOT)
        self->r.solid = SOLID_TRIGGER;
    else
        self->r.solid = SOLID_NOT;
    trap_LinkEntity(self);
}
// PGM
//======

// RAFAEL
void trigger_push_active(edict_t *self);

static void trigger_effect(edict_t *self)
{
    vec3_t origin;
    int    i;

    VectorAvg(self->r.absmin, self->r.absmax, origin);

    for (i = 0; i < 10; i++) {
        origin[2] += (self->speed * 0.01f) * (i + frandom());
        G_TempEntity(origin, EV_TUNNEL_SPARKS, MakeLittleLong(0, irandom2(0x74, 0x7C), 1, 0));
    }
}

void THINK(trigger_push_inactive)(edict_t *self)
{
    if (self->timestamp > level.time) {
        self->nextthink = level.time + HZ(10);
    } else {
        self->touch = trigger_push_touch;
        self->think = trigger_push_active;
        self->nextthink = level.time + HZ(10);
        self->timestamp = self->nextthink + SEC(self->wait);
    }
}

void THINK(trigger_push_active)(edict_t *self)
{
    if (self->timestamp > level.time) {
        self->nextthink = level.time + HZ(10);
        trigger_effect(self);
    } else {
        self->touch = NULL;
        self->think = trigger_push_inactive;
        self->nextthink = level.time + HZ(10);
        self->timestamp = self->nextthink + SEC(self->wait);
    }
}
// RAFAEL

/*QUAKED trigger_push (.5 .5 .5) ? PUSH_ONCE PUSH_PLUS PUSH_SILENT START_OFF CLIP
Pushes the player
"speed" defaults to 1000
"wait"  defaults to 10, must use PUSH_PLUS

If targeted, it will toggle on and off when used.

START_OFF - toggled trigger_push begins in off setting
SILENT - doesn't make wind noise
*/
void SP_trigger_push(edict_t *self)
{
    InitTrigger(self);
    if (!(self->spawnflags & SPAWNFLAG_PUSH_SILENT))
        G_SoundIndex("misc/windfly.wav");
    self->touch = trigger_push_touch;

    // RAFAEL
    if (self->spawnflags & SPAWNFLAG_PUSH_PLUS) {
        if (!self->wait)
            self->wait = 10;

        self->think = trigger_push_active;
        self->nextthink = level.time + HZ(10);
        self->timestamp = self->nextthink + SEC(self->wait);
    }
    // RAFAEL

    if (!self->speed)
        self->speed = 1000;

    // PGM
    if (self->targetname) { // toggleable
        self->use = trigger_push_use;
        if (self->spawnflags & SPAWNFLAG_PUSH_START_OFF)
            self->r.solid = SOLID_NOT;
    } else if (self->spawnflags & SPAWNFLAG_PUSH_START_OFF) {
        G_Printf("trigger_push is START_OFF but not targeted.\n");
        self->r.svflags = SVF_NONE;
        self->touch = NULL;
        self->r.solid = SOLID_BSP;
        self->movetype = MOVETYPE_PUSH;
    }
    // PGM

    trap_LinkEntity(self);

    if (self->spawnflags & SPAWNFLAG_PUSH_CLIP)
        self->r.svflags |= SVF_HULL;
}

/*
==============================================================================

trigger_hurt

==============================================================================
*/

/*QUAKED trigger_hurt (.5 .5 .5) ? START_OFF TOGGLE SILENT NO_PROTECTION SLOW NO_PLAYERS NO_MONSTERS PASSIVE
Any entity that touches this will be hurt.

It does dmg points of damage each server frame

SILENT          supresses playing the sound
SLOW            changes the damage rate to once per second
NO_PROTECTION   *nothing* stops the damage

"dmg"           default 5 (whole numbers only)

*/

#define SPAWNFLAG_HURT_START_OFF        1
#define SPAWNFLAG_HURT_TOGGLE           2
#define SPAWNFLAG_HURT_SILENT           4
#define SPAWNFLAG_HURT_NO_PROTECTION    8
#define SPAWNFLAG_HURT_SLOW             16
#define SPAWNFLAG_HURT_NO_PLAYERS       32
#define SPAWNFLAG_HURT_NO_MONSTERS      64
#define SPAWNFLAG_HURT_CLIPPED          128
#define SPAWNFLAG_HURT_PASSIVE          BIT(16)

void USE(hurt_use)(edict_t *self, edict_t *other, edict_t *activator)
{
    if (self->r.solid == SOLID_NOT)
        self->r.solid = SOLID_TRIGGER;
    else
        self->r.solid = SOLID_NOT;
    trap_LinkEntity(self);

    if (!(self->spawnflags & SPAWNFLAG_HURT_TOGGLE))
        self->use = NULL;

    if (self->spawnflags & SPAWNFLAG_HURT_PASSIVE) {
        if (self->r.solid == SOLID_TRIGGER) {
            if (self->spawnflags & SPAWNFLAG_HURT_SLOW)
                self->nextthink = level.time + SEC(1);
            else
                self->nextthink = level.time + HZ(10);
        } else
            self->nextthink = 0;
    }
}

static bool can_hurt(edict_t *self, edict_t *other)
{
    if (!other->r.inuse)
        return false;
    if (!other->takedamage)
        return false;
    if (!(other->r.svflags & SVF_MONSTER) && !(other->flags & FL_DAMAGEABLE) && (!other->client) && (strcmp(other->classname, "misc_explobox") != 0))
        return false;
    if ((self->spawnflags & SPAWNFLAG_HURT_NO_MONSTERS) && (other->r.svflags & SVF_MONSTER))
        return false;
    if ((self->spawnflags & SPAWNFLAG_HURT_NO_PLAYERS) && (other->client))
        return false;
    if (!G_BrushModelClip(self, other))
        return false;

    return true;
}

void THINK(hurt_think)(edict_t *self)
{
    int list[MAX_EDICTS_OLD];
    damageflags_t dflags;
    int count;

    if (self->spawnflags & SPAWNFLAG_HURT_NO_PROTECTION)
        dflags = DAMAGE_NO_PROTECTION;
    else
        dflags = DAMAGE_NONE;

    count = trap_BoxEdicts(self->r.absmin, self->r.absmax, list, q_countof(list), AREA_SOLID);

    for (int i = 0; i < count; i++) {
        edict_t *other = g_edicts + list[i];
        if (!can_hurt(self, other))
            continue;

        if (!(self->spawnflags & SPAWNFLAG_HURT_SILENT)) {
            if (self->fly_sound_debounce_time < level.time) {
                G_StartSound(other, CHAN_AUTO, self->noise_index, 1, ATTN_NORM);
                self->fly_sound_debounce_time = level.time + SEC(1);
            }
        }

        T_Damage(other, self, self, vec3_origin, other->s.origin, 0, self->dmg, self->dmg, dflags, (mod_t) { MOD_TRIGGER_HURT });
    }

    if (self->spawnflags & SPAWNFLAG_HURT_SLOW)
        self->nextthink = level.time + SEC(1);
    else
        self->nextthink = level.time + HZ(10);
}

void TOUCH(hurt_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    damageflags_t dflags;

    if (self->timestamp > level.time)
        return;

    if (!can_hurt(self, other))
        return;

    if (self->spawnflags & SPAWNFLAG_HURT_SLOW)
        self->timestamp = level.time + SEC(1);
    else
        self->timestamp = level.time + HZ(10);

    if (!(self->spawnflags & SPAWNFLAG_HURT_SILENT)) {
        if (self->fly_sound_debounce_time < level.time) {
            G_StartSound(other, CHAN_AUTO, self->noise_index, 1, ATTN_NORM);
            self->fly_sound_debounce_time = level.time + SEC(1);
        }
    }

    if (self->spawnflags & SPAWNFLAG_HURT_NO_PROTECTION)
        dflags = DAMAGE_NO_PROTECTION;
    else
        dflags = DAMAGE_NONE;

    T_Damage(other, self, self, vec3_origin, other->s.origin, 0, self->dmg, self->dmg, dflags, (mod_t) { MOD_TRIGGER_HURT });
}

void SP_trigger_hurt(edict_t *self)
{
    InitTrigger(self);

    self->noise_index = G_SoundIndex("world/electro.wav");

    if (self->spawnflags & SPAWNFLAG_HURT_PASSIVE) {
        self->think = hurt_think;

        if (!(self->spawnflags & SPAWNFLAG_HURT_START_OFF)) {
            if (self->spawnflags & SPAWNFLAG_HURT_SLOW)
                self->nextthink = level.time + SEC(1);
            else
                self->nextthink = level.time + HZ(10);
        }
    } else
        self->touch = hurt_touch;

    if (!self->dmg)
        self->dmg = 5;

    if (self->spawnflags & SPAWNFLAG_HURT_START_OFF)
        self->r.solid = SOLID_NOT;
    else
        self->r.solid = SOLID_TRIGGER;

    if (self->spawnflags & SPAWNFLAG_HURT_TOGGLE)
        self->use = hurt_use;

    trap_LinkEntity(self);

    if (self->spawnflags & SPAWNFLAG_HURT_CLIPPED)
        self->r.svflags |= SVF_HULL;
}

/*
==============================================================================

trigger_gravity

==============================================================================
*/

/*QUAKED trigger_gravity (.5 .5 .5) ? TOGGLE START_OFF
Changes the touching entites gravity to the value of "gravity".
1.0 is standard gravity for the level.

TOGGLE - trigger_gravity can be turned on and off
START_OFF - trigger_gravity starts turned off (implies TOGGLE)
*/

#define SPAWNFLAG_GRAVITY_TOGGLE    1
#define SPAWNFLAG_GRAVITY_START_OFF 2
#define SPAWNFLAG_GRAVITY_CLIPPED   4

// PGM
void USE(trigger_gravity_use)(edict_t *self, edict_t *other, edict_t *activator)
{
    if (self->r.solid == SOLID_NOT)
        self->r.solid = SOLID_TRIGGER;
    else
        self->r.solid = SOLID_NOT;
    trap_LinkEntity(self);
}
// PGM

void TOUCH(trigger_gravity_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    if (!G_BrushModelClip(self, other))
        return;

    other->gravity = self->gravity;
}

void SP_trigger_gravity(edict_t *self)
{
    if (!st.gravity || !*st.gravity) {
        G_Printf("%s: no gravity set\n", etos(self));
        G_FreeEdict(self);
        return;
    }

    InitTrigger(self);

    // PGM
    self->gravity = Q_atof(st.gravity);

    if (self->spawnflags & SPAWNFLAG_GRAVITY_TOGGLE)
        self->use = trigger_gravity_use;

    if (self->spawnflags & SPAWNFLAG_GRAVITY_START_OFF) {
        self->use = trigger_gravity_use;
        self->r.solid = SOLID_NOT;
    }

    self->touch = trigger_gravity_touch;
    // PGM

    trap_LinkEntity(self);

    if (self->spawnflags & SPAWNFLAG_GRAVITY_CLIPPED)
        self->r.svflags |= SVF_HULL;
}

/*
==============================================================================

trigger_monsterjump

==============================================================================
*/

/*QUAKED trigger_monsterjump (.5 .5 .5) ?
Walking monsters that touch this will jump in the direction of the trigger's angle
"speed" default to 200, the speed thrown forward
"height" default to 200, the speed thrown upwards

TOGGLE - trigger_monsterjump can be turned on and off
START_OFF - trigger_monsterjump starts turned off (implies TOGGLE)
*/

#define SPAWNFLAG_MONSTERJUMP_TOGGLE    1
#define SPAWNFLAG_MONSTERJUMP_START_OFF 2
#define SPAWNFLAG_MONSTERJUMP_CLIPPED   4

void USE(trigger_monsterjump_use)(edict_t *self, edict_t *other, edict_t *activator)
{
    if (self->r.solid == SOLID_NOT)
        self->r.solid = SOLID_TRIGGER;
    else
        self->r.solid = SOLID_NOT;
    trap_LinkEntity(self);
}

void TOUCH(trigger_monsterjump_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    if (other->flags & (FL_FLY | FL_SWIM))
        return;
    if (other->r.svflags & SVF_DEADMONSTER)
        return;
    if (!(other->r.svflags & SVF_MONSTER))
        return;
    if (!G_BrushModelClip(self, other))
        return;

    // set XY even if not on ground, so the jump will clear lips
    other->velocity[0] = self->movedir[0] * self->speed;
    other->velocity[1] = self->movedir[1] * self->speed;

    if (!other->groundentity)
        return;

    other->groundentity = NULL;
    other->velocity[2] = self->movedir[2];
}

void SP_trigger_monsterjump(edict_t *self)
{
    if (!self->speed)
        self->speed = 200;
    if (!st.height)
        st.height = 200;
    if (self->s.angles[YAW] == 0)
        self->s.angles[YAW] = 360;
    InitTrigger(self);
    self->touch = trigger_monsterjump_touch;
    self->movedir[2] = st.height;

    if (self->spawnflags & SPAWNFLAG_MONSTERJUMP_TOGGLE)
        self->use = trigger_monsterjump_use;

    if (self->spawnflags & SPAWNFLAG_MONSTERJUMP_START_OFF) {
        self->use = trigger_monsterjump_use;
        self->r.solid = SOLID_NOT;
    }

    trap_LinkEntity(self);

    if (self->spawnflags & SPAWNFLAG_MONSTERJUMP_CLIPPED)
        self->r.svflags |= SVF_HULL;
}

/*
==============================================================================

trigger_flashlight

==============================================================================
*/

/*QUAKED trigger_flashlight (.5 .5 .5) ?
Players moving against this trigger will have their flashlight turned on or off.
"style" default to 0, set to 1 to always turn flashlight on, 2 to always turn off,
        otherwise "angles" are used to control on/off state
*/

#define SPAWNFLAG_FLASHLIGHT_CLIPPED    1

void TOUCH(trigger_flashlight_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    if (!other->client)
        return;
    if (!G_BrushModelClip(self, other))
        return;

    if (self->style == 1)
        P_ToggleFlashlight(other, true);
    else if (self->style == 2)
        P_ToggleFlashlight(other, false);
    else if (VectorLengthSquared(other->velocity) > 32)
        P_ToggleFlashlight(other, DotProduct(other->velocity, self->movedir) > 0);
}

void SP_trigger_flashlight(edict_t *self)
{
    if (self->s.angles[YAW] == 0)
        self->s.angles[YAW] = 360;
    InitTrigger(self);
    self->touch = trigger_flashlight_touch;
    self->movedir[2] = st.height;

    if (self->spawnflags & SPAWNFLAG_FLASHLIGHT_CLIPPED)
        self->r.svflags |= SVF_HULL;
    trap_LinkEntity(self);
}


/*
==============================================================================

trigger_fog

==============================================================================
*/

/*QUAKED trigger_fog (.5 .5 .5) ? AFFECT_FOG AFFECT_HEIGHTFOG INSTANTANEOUS FORCE BLEND
Players moving against this trigger will have their fog settings changed.
Fog/heightfog will be adjusted if the spawnflags are set. Instantaneous
ignores any delays. Force causes it to ignore movement dir and always use
the "on" values. Blend causes it to change towards how far you are into the trigger
with respect to angles.
"target" can target an info_notnull to pull the keys below from.
"delay" default to 0.5; time in seconds a change in fog will occur over
"wait" default to 0.0; time in seconds before a re-trigger can be executed

"fog_density"; density value of fog, 0-1
"fog_color"; color value of fog, 3d vector with values between 0-1 (r g b)
"fog_density_off"; transition density value of fog, 0-1
"fog_color_off"; transition color value of fog, 3d vector with values between 0-1 (r g b)
"fog_sky_factor"; sky factor value of fog, 0-1
"fog_sky_factor_off"; transition sky factor value of fog, 0-1

"heightfog_falloff"; falloff value of heightfog, 0-1
"heightfog_density"; density value of heightfog, 0-1
"heightfog_start_color"; the start color for the fog (r g b, 0-1)
"heightfog_start_dist"; the start distance for the fog (units)
"heightfog_end_color"; the start color for the fog (r g b, 0-1)
"heightfog_end_dist"; the end distance for the fog (units)

"heightfog_falloff_off"; transition falloff value of heightfog, 0-1
"heightfog_density_off"; transition density value of heightfog, 0-1
"heightfog_start_color_off"; transition the start color for the fog (r g b, 0-1)
"heightfog_start_dist_off"; transition the start distance for the fog (units)
"heightfog_end_color_off"; transition the start color for the fog (r g b, 0-1)
"heightfog_end_dist_off"; transition the end distance for the fog (units)
*/

#define SPAWNFLAG_FOG_AFFECT_FOG        1
#define SPAWNFLAG_FOG_AFFECT_HEIGHTFOG  2
#define SPAWNFLAG_FOG_INSTANTANEOUS     4
#define SPAWNFLAG_FOG_FORCE             8
#define SPAWNFLAG_FOG_BLEND             16

void TOUCH(trigger_fog_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    if (!other->client)
        return;

    if (self->timestamp > level.time)
        return;

    self->timestamp = level.time + SEC(self->wait);

    edict_t *fog_value_storage = self;

    if (self->movetarget)
        fog_value_storage = self->movetarget;

    if (self->spawnflags & SPAWNFLAG_FOG_INSTANTANEOUS) {
        other->client->fog_transition_end   = 0;
        other->client->fog_transition_start = 0;
    } else if (other->client->fog_transition_end <= level.time) {
        other->client->fog_transition_end   = level.time + SEC(fog_value_storage->delay);
        other->client->fog_transition_start = level.time;
        other->client->start_fog            = other->client->ps.fog;
        other->client->start_heightfog      = other->client->ps.heightfog;
    }

    if (self->spawnflags & SPAWNFLAG_FOG_BLEND) {
        vec3_t center, half_size, start, end, player_dist;

        VectorAvg(self->r.absmin, self->r.absmax, center);
        VectorAvg(self->r.size, other->r.size, half_size);
        VectorVectorScale(half_size, self->movedir, end);
        VectorNegate(end, start);
        VectorSubtract(other->s.origin, center, player_dist);
        player_dist[0] *= fabsf(self->movedir[0]);
        player_dist[1] *= fabsf(self->movedir[1]);
        player_dist[2] *= fabsf(self->movedir[2]);

        float frac = Q_clipf(Distance(player_dist, start) / Distance(end, start), 0, 1);

        if (self->spawnflags & SPAWNFLAG_FOG_AFFECT_FOG)
            lerp_values(&fog_value_storage->fog_off, &fog_value_storage->fog, frac,
                        &other->client->wanted_fog, sizeof(other->client->wanted_fog) / sizeof(float));

        if (self->spawnflags & SPAWNFLAG_FOG_AFFECT_HEIGHTFOG)
            lerp_values(&fog_value_storage->heightfog_off, &fog_value_storage->heightfog, frac,
                        &other->client->wanted_heightfog, sizeof(other->client->wanted_heightfog) / sizeof(float));

        return;
    }

    bool use_on = true;

    if (!(self->spawnflags & SPAWNFLAG_FOG_FORCE)) {
        vec3_t forward;

        // not moving enough to trip; this is so we don't trip
        // the wrong direction when on an elevator, etc.
        if (VectorNormalize2(other->velocity, forward) <= 0.0001f)
            return;

        use_on = DotProduct(forward, self->movedir) > 0;
    }

    if (self->spawnflags & SPAWNFLAG_FOG_AFFECT_FOG)
        other->client->wanted_fog = use_on ? fog_value_storage->fog : fog_value_storage->fog_off;

    if (self->spawnflags & SPAWNFLAG_FOG_AFFECT_HEIGHTFOG)
        other->client->wanted_heightfog = use_on ? fog_value_storage->heightfog : fog_value_storage->heightfog_off;
}

void SP_trigger_fog(edict_t *self)
{
    if (self->s.angles[YAW] == 0)
        self->s.angles[YAW] = 360;

    InitTrigger(self);

    if (!(self->spawnflags & (SPAWNFLAG_FOG_AFFECT_FOG | SPAWNFLAG_FOG_AFFECT_HEIGHTFOG)))
        G_Printf("WARNING: %s with no fog spawnflags set\n", etos(self));

    if (self->target) {
        self->movetarget = G_PickTarget(self->target);

        if (self->movetarget && !self->movetarget->delay)
            self->movetarget->delay = 0.5f;
    }

    if (!self->delay)
        self->delay = 0.5f;

    self->touch = trigger_fog_touch;
}

/*QUAKED trigger_coop_relay (.5 .5 .5) ? AUTO_FIRE
Like a trigger_relay, but all players must be touching its
mins/maxs in order to fire, otherwise a message will be printed.

AUTO_FIRE: check every `wait` seconds for containment instead of
requiring to be fired by something else. Frees itself after firing.

"message"; message to print to the one activating the relay if
           not all players are inside the bounds
"message2"; message to print to players not inside the trigger
            if they aren't in the bounds
*/

#define SPAWNFLAG_COOP_RELAY_AUTO_FIRE  1

static bool trigger_coop_relay_ok(edict_t *player)
{
    return player->r.inuse && player->client && player->health > 0 &&
        player->movetype != MOVETYPE_NOCLIP && player->s.modelindex == MODELINDEX_PLAYER;
}

static bool trigger_coop_relay_can_use(edict_t *self, edict_t *activator)
{
    // not coop, so act like a standard trigger_relay minus the message
    if (!coop.integer)
        return true;

    // coop; scan for all alive players, print appropriate message
    // to those in/out of range
    bool can_use = true;

    for (int i = 0; i < game.maxclients; i++) {
        edict_t *player = &g_edicts[i];

        // dead or spectator, don't count them
        if (!trigger_coop_relay_ok(player))
            continue;

        if (G_EntitiesContact(player, self))
            continue;

        if (self->timestamp < level.time)
            G_ClientPrintf(player, PRINT_CENTER, "%s", self->map);
        can_use = false;
    }

    return can_use;
}

void USE(trigger_coop_relay_use)(edict_t *self, edict_t *other, edict_t *activator)
{
    if (!trigger_coop_relay_can_use(self, activator)) {
        if (self->timestamp < level.time)
            G_ClientPrintf(activator, PRINT_CENTER, "%s", self->message);

        self->timestamp = level.time + SEC(5);
        return;
    }

    const char *msg = self->message;
    self->message = NULL;
    G_UseTargets(self, activator);
    self->message = msg;
}

void THINK(trigger_coop_relay_think)(edict_t *self)
{
    int num_active = 0, num_present = 0;

    for (int i = 0; i < game.maxclients; i++) {
        edict_t *player = &g_edicts[i];
        if (!trigger_coop_relay_ok(player))
            continue;
        num_active++;
        if (G_EntitiesContact(player, self))
            num_present++;
    }

    if (num_present == num_active) {
        const char *msg = self->message;
        self->message = NULL;
        G_UseTargets(self, &g_edicts[0]);
        self->message = msg;

        G_FreeEdict(self);
        return;
    }

    if (num_present && self->timestamp < level.time) {
        for (int i = 0; i < game.maxclients; i++) {
            edict_t *player = &g_edicts[i];
            if (!trigger_coop_relay_ok(player))
                continue;
            if (G_EntitiesContact(player, self))
                G_ClientPrintf(player, PRINT_CENTER, "%s", self->message);
            else
                G_ClientPrintf(player, PRINT_CENTER, "%s", self->map);
        }

        self->timestamp = level.time + SEC(5);
    }

    self->nextthink = level.time + SEC(self->wait);
}

void SP_trigger_coop_relay(edict_t *self)
{
    if (self->targetname && self->spawnflags & SPAWNFLAG_COOP_RELAY_AUTO_FIRE)
        G_Printf("%s: targetname and auto-fire are mutually exclusive\n", etos(self));

    InitTrigger(self);

    if (!self->message)
        self->message = "All players must be present to continue...";

    if (!self->map)
        self->map = "Players are waiting for you...";

    if (!self->wait)
        self->wait = 1;

    if (self->spawnflags & SPAWNFLAG_COOP_RELAY_AUTO_FIRE) {
        self->think = trigger_coop_relay_think;
        self->nextthink = level.time + SEC(self->wait);
    } else
        self->use = trigger_coop_relay_use;
    trap_LinkEntity(self);
}

/*QUAKED trigger_safe_fall (.5 .5 .5) ?
Players that touch this trigger are granted one (1)
free safe fall damage exemption.

They must already be in the air to get this ability.
*/

void TOUCH(trigger_safe_fall_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    if (other->client && !other->groundentity)
        other->client->landmark_free_fall = true;
}

void SP_trigger_safe_fall(edict_t *self)
{
    InitTrigger(self);
    self->touch = trigger_safe_fall_touch;
    trap_LinkEntity(self);
}
