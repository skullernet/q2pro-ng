// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"

/*QUAKED misc_crashviper (1 .5 0) (-176 -120 -24) (176 120 72)
This is a large viper about to crash
*/
void SP_misc_crashviper(edict_t *ent)
{
    if (!ent->target) {
        G_Printf("%s: no target\n", etos(ent));
        G_FreeEdict(ent);
        return;
    }

    if (!ent->speed)
        ent->speed = 300;

    ent->movetype = MOVETYPE_PUSH;
    ent->r.solid = SOLID_NOT;
    ent->s.modelindex = G_ModelIndex("models/ships/bigviper/tris.md2");
    VectorSet(ent->r.mins, -16, -16, 0);
    VectorSet(ent->r.maxs, 16, 16, 32);

    ent->think = func_train_find;
    ent->nextthink = level.time + HZ(10);
    ent->use = misc_viper_use;
    ent->r.svflags |= SVF_NOCLIENT;
    ent->moveinfo.accel = ent->moveinfo.decel = ent->moveinfo.speed = ent->speed;

    trap_LinkEntity(ent);
}

// RAFAEL
/*QUAKED misc_viper_missile (1 0 0) (-8 -8 -8) (8 8 8)
"dmg"   how much boom should the bomb make? the default value is 250
*/

void USE(misc_viper_missile_use)(edict_t *self, edict_t *other, edict_t *activator)
{
    vec3_t start, dir;
    vec3_t vec;

    self->enemy = G_Find(NULL, FOFS(targetname), self->target);
    if (!self->enemy) {
        G_Printf("%s: target %s not found\n", etos(self), self->target);
        return;
    }

    VectorCopy(self->enemy->s.origin, vec);
    //vec[2] += 16;

    VectorCopy(self->s.origin, start);
    VectorSubtract(vec, start, dir);
    VectorNormalize(dir);

    monster_fire_rocket(self, start, dir, self->dmg, 500, MZ2_CHICK_ROCKET_1);

    self->nextthink = level.time + HZ(10);
    self->think = G_FreeEdict;
}

void SP_misc_viper_missile(edict_t *self)
{
    self->movetype = MOVETYPE_NONE;
    self->r.solid = SOLID_NOT;
    VectorSet(self->r.mins, -8, -8, -8);
    VectorSet(self->r.maxs, 8, 8, 8);

    if (!self->dmg)
        self->dmg = 250;

    self->s.modelindex = G_ModelIndex("models/objects/bomb/tris.md2");

    self->use = misc_viper_missile_use;
    self->r.svflags |= SVF_NOCLIENT;

    trap_LinkEntity(self);
}

// RAFAEL 17-APR-98
/*QUAKED misc_transport (1 0 0) (-8 -8 -8) (8 8 8)
Maxx's transport at end of game
*/
void SP_misc_transport(edict_t *ent)
{
    if (!ent->target) {
        G_Printf("%s: no target\n", etos(ent));
        G_FreeEdict(ent);
        return;
    }

    if (!ent->speed)
        ent->speed = 300;

    ent->movetype = MOVETYPE_PUSH;
    ent->r.solid = SOLID_NOT;
    ent->s.modelindex = G_ModelIndex("models/objects/ship/tris.md2");

    VectorSet(ent->r.mins, -16, -16, 0);
    VectorSet(ent->r.maxs, 16, 16, 32);

    ent->think = func_train_find;
    ent->nextthink = level.time + HZ(10);
    ent->use = misc_strogg_ship_use;
    ent->r.svflags |= SVF_NOCLIENT;
    ent->moveinfo.accel = ent->moveinfo.decel = ent->moveinfo.speed = ent->speed;

    if (!(ent->spawnflags & SPAWNFLAG_TRAIN_START_ON))
        ent->spawnflags |= SPAWNFLAG_TRAIN_START_ON;

    trap_LinkEntity(ent);
}
// END 17-APR-98

/*QUAKED misc_amb4 (1 0 0) (-16 -16 -16) (16 16 16)
Mal's amb4 loop entity
*/
void THINK(amb4_think)(edict_t *ent)
{
    ent->nextthink = level.time + SEC(2.7f);
    G_StartSound(ent, CHAN_VOICE, ent->noise_index, 1, ATTN_NONE);
}

void SP_misc_amb4(edict_t *ent)
{
    ent->think = amb4_think;
    ent->nextthink = level.time + SEC(1);
    ent->noise_index = G_SoundIndex("world/amb4.wav");
    trap_LinkEntity(ent);
}

/*QUAKED misc_nuke (1 0 0) (-16 -16 -16) (16 16 16)
 */
void target_killplayers_use(edict_t *self, edict_t *other, edict_t *activator);

void SP_misc_nuke(edict_t *ent)
{
    ent->use = target_killplayers_use;
}
