/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// m_spawn.c

#include "g_local.h"
#include "m_spawn.h"

static int sound_death;
static int sound_hit;
static int sound_land;
static int sound_sight;

static void spawn_unbounce(edict_t *self)
{
    self->movetype = MOVETYPE_STEP;
}

// Stand
static const mframe_t spawn_frames_stand[] = {
    { ai_stand, 0, spawn_unbounce }
};
const mmove_t MMOVE_T(spawn_move_stand) = { FRAME_stand1, FRAME_stand1, spawn_frames_stand, NULL };

void MONSTERINFO_STAND(spawn_stand)(edict_t *self)
{
    M_SetAnimation(self, &spawn_move_stand);
}

// Run
static const mframe_t spawn_frames_run[] = {
    { ai_run },
    { ai_run },
    { ai_run },
    { ai_run },
    { ai_run },
    { ai_run },
    { ai_run },
    { ai_run },
    { ai_run },
    { ai_run },
    { ai_run, 2 },
    { ai_run, 2 },
    { ai_run, 2 },
    { ai_run, 2 },
    { ai_run, 2 },
    { ai_run, 2 },
    { ai_run, 2 },
    { ai_run, 2 },
    { ai_run, 2 },
    { ai_run, 2 },
    { ai_run, 2 },
    { ai_run, 2 },
    { ai_run, 2 },
    { ai_run, 2 },
    { ai_run, 2 }
};
const mmove_t MMOVE_T(spawn_move_run) = { FRAME_run1, FRAME_run25, spawn_frames_run, NULL };

void MONSTERINFO_RUN(spawn_run)(edict_t *self)
{
    M_SetAnimation(self, &spawn_move_run);
}

// WALK
static const mframe_t spawn_frames_walk[] = {
    { ai_walk },
    { ai_walk },
    { ai_walk },
    { ai_walk },
    { ai_walk },
    { ai_walk },
    { ai_walk },
    { ai_walk },
    { ai_walk },
    { ai_walk },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 2 }
};
const mmove_t MMOVE_T(spawn_move_walk) = { FRAME_run1, FRAME_run25, spawn_frames_walk, NULL };

void MONSTERINFO_WALK(spawn_walk)(edict_t *self)
{
    M_SetAnimation(self, &spawn_move_walk);
}

// Sight
void MONSTERINFO_SIGHT(spawn_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM);
}

// Jump
static void prespawnJump(edict_t *self);
static void spawnJump(edict_t *self);
static void spawn_rejump(edict_t *self);

static const mframe_t spawn_frames_jump[] = {
    { ai_charge },
    { ai_charge, 0, prespawnJump },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, spawnJump },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, spawn_rejump }
};
const mmove_t MMOVE_T(spawn_move_jump) = { FRAME_jump1, FRAME_fly4, spawn_frames_jump, spawn_run };

void TOUCH(spawn_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    vec3_t point;
    vec3_t normal;
    float  length;
    int    damage;

    if (other->takedamage) {
        normal = Vec3_NormalizeLength(self->velocity, &length);
        if (length > 400) {
            point = Vec3_MA(self->s.origin, self->r.box.maxs.x, normal);
            damage = irandom2(10, 21);
            T_Damage(other, self, self, self->velocity, point, DirToByte(normal), damage, damage, DAMAGE_NONE, MOD_UNKNOWN);
            G_StartSound(self, CHAN_WEAPON, sound_hit, 1, ATTN_NORM);
        }
    } else {
        G_StartSound(self, CHAN_WEAPON, sound_land, 1, ATTN_NORM);
    }

    if (!M_CheckBottom(self)) {
        if (self->groundentity) {
            self->touch = NULL;
            M_SetAnimation(self, &spawn_move_run);
            self->movetype = MOVETYPE_STEP;
            self->count = 0;
        }
        return;
    }
    self->touch = NULL;
    M_SetAnimation(self, &spawn_move_jump);
}

static void prespawnJump(edict_t *self) {}

static void spawnJump(edict_t *self)
{
    vec3_t forward;

    AngleVectors(self->s.angles, &forward, NULL, NULL);
    self->s.origin.z += 1;
    self->velocity.x = 600 * forward.x;
    self->velocity.y = 600 * forward.y;
    self->velocity.z = 200 + frandom() * 150;

    self->movetype = MOVETYPE_BOUNCE;
    self->groundentity = NULL;
    self->touch = spawn_touch;
}

static void spawn_rejump(edict_t *self)
{
    self->count++;
    if (self->count < 4) {
        self->monsterinfo.nextframe = FRAME_jump1;
        return;
    }
}

// Attack
void MONSTERINFO_ATTACK(spawn_attack)(edict_t *self)
{
    M_SetAnimation(self, &spawn_move_jump);
}

// Melee
void MONSTERINFO_MELEE(spawn_melee)(edict_t *self)
{
    M_SetAnimation(self, &spawn_move_jump);
}

// Death
void DIE(spawn_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    self->s.frame = FRAME_death1;
    T_RadiusDamage(self, attacker, 500, self, self->classname, 100, DAMAGE_NONE, MOD_EXPLOSIVE);
    G_BecomeEvent(self, EV_ROCKET_EXPLOSION, 0);
}

// Pain

void PR_monster_spawn(void)
{
    sound_death = G_SoundIndex("spawn/death1.wav");
    sound_hit = G_SoundIndex("spawn/hit1.wav");
    sound_land = G_SoundIndex("spawn/land1.wav");
    sound_sight = G_SoundIndex("spawn/sight1.wav");
}

void SP_monster_spawn(edict_t *self)
{
    self->s.modelindex = G_ModelIndex("models/monsters/spawn/tris.md2");
    self->r.box = Box3_FromSize(16, -24, 40);
    self->r.solid = SOLID_BBOX;
    self->movetype = MOVETYPE_STEP;

    self->health = 80;
    self->gib_health = 0;
    self->mass = 80;

    self->die = spawn_die;
    //self->pain = spawn_pain;
    self->monsterinfo.stand = spawn_stand;
    self->monsterinfo.walk = spawn_walk;
    self->monsterinfo.run = spawn_run;
    self->monsterinfo.attack = spawn_attack;
    self->monsterinfo.melee = spawn_melee;
    self->monsterinfo.sight = spawn_sight;

    M_SetAnimation(self, &spawn_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    walkmonster_start(self);
}
