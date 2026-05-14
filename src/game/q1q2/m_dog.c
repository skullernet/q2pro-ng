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
// m_army.c

#include "g_local.h"
#include "m_dog.h"

#define SOUND   sound[!!self->style]

static struct {
    int melee;
    int death;
    int pain;
    int sight;
    int search;
} sound[2];

// Stand
static const mframe_t dog_frames_stand[] = {
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand }
};
const mmove_t MMOVE_T(dog_move_stand) = { FRAME_stand01, FRAME_stand09, dog_frames_stand, NULL };

void MONSTERINFO_STAND(dog_stand)(edict_t *self)
{
    M_SetAnimation(self, &dog_move_stand);
}

// walk
static const mframe_t dog_frames_walk[] = {
    { ai_walk, 8, monster_footstep },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8, monster_footstep },
    { ai_walk, 8 },
    { ai_walk, 8 },
    { ai_walk, 8 }
};
const mmove_t MMOVE_T(dog_move_walk) = { FRAME_walk01, FRAME_walk08, dog_frames_walk, NULL };

void MONSTERINFO_WALK(dog_walk)(edict_t *self)
{
    M_SetAnimation(self, &dog_move_walk);
}

// Run
static const mframe_t dog_frames_run[] = {
    { ai_run, 16 },
    { ai_run, 32 },
    { ai_run, 32, monster_footstep },
    { ai_run, 20 },
    { ai_run, 64 },
    { ai_run, 32 },
    { ai_run, 16 },
    { ai_run, 32, monster_footstep },
    { ai_run, 32 },
    { ai_run, 20 },
    { ai_run, 64 },
    { ai_run, 64, monster_footstep }
};
const mmove_t MMOVE_T(dog_move_run) = { FRAME_run01, FRAME_run12, dog_frames_run, NULL };

void MONSTERINFO_RUN(dog_run)(edict_t *self)
{
    M_SetAnimation(self, &dog_move_run);
}

void TOUCH(DogLeapTouch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    vec3_t point;
    vec3_t normal;
    float  length;
    int    damage;

    if (self->health <= 0) {
        self->touch = NULL;
        return;
    }

    if (other->takedamage) {
        normal = Vec3_NormalizeLength(self->velocity, &length);
        if (length > 30) {
            point = Vec3_MA(self->s.origin, self->r.box.maxs.x, normal);
            damage = irandom2(10, 21);
            T_Damage(other, self, self, self->velocity, point, DirToByte(normal), damage, damage, DAMAGE_NONE, MOD_UNKNOWN);
        }
    }

    self->touch = NULL;
}

static void DogLeaper(edict_t *self)
{
    if (!self->enemy || !self->enemy->r.inuse)
        return;

    float length = Vec3_Distance(self->s.origin, self->enemy->s.origin);
    float fwd_speed = length * 1.95f;
    vec3_t forward;

    G_StartSound(self, CHAN_VOICE, SOUND.melee, 1, ATTN_NORM);

    AngleVectors(self->s.angles, &forward, NULL, NULL);
    self->s.origin.z += 1;
    self->velocity = Vec3_Scale(forward, fwd_speed);
    self->velocity.z = 250;
    self->groundentity = NULL;
    self->touch = DogLeapTouch;
}

// Leap
static const mframe_t dog_frames_leap[] = {
    { ai_charge },
    { ai_charge, 0, DogLeaper },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(dog_move_leap) = { FRAME_leap01, FRAME_leap09, dog_frames_leap, dog_run };

void MONSTERINFO_ATTACK(dog_leap)(edict_t *self)
{
    if (range_to(self, self->enemy) > 300)
        return;
    M_SetAnimation(self, &dog_move_leap);
}

static void DogBite(edict_t *self)
{
    vec3_t aim = { MELEE_DISTANCE, 0, 8 };
    G_StartSound(self, CHAN_VOICE, SOUND.melee, 1, ATTN_NORM);
    fire_hit(self, aim, irandom1(3), 10);
}

// melee
static const mframe_t dog_frames_melee[] = {
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 10, DogBite },
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 10 }
};
const mmove_t MMOVE_T(dog_move_melee) = { FRAME_attack01, FRAME_attack08, dog_frames_melee, dog_run };

void MONSTERINFO_MELEE(dog_melee)(edict_t *self)
{
    if (range_to(self, self->enemy) > MELEE_DISTANCE)
        return;
    M_SetAnimation(self, &dog_move_melee);
}

void MONSTERINFO_SETSKIN(dog_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

static void dog_pain_sound(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.pain, 1, ATTN_NORM);
}

// Pain (1)
static const mframe_t dog_frames_pain1[] = {
    { ai_move },
    { ai_move, 0, dog_pain_sound },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(dog_move_pain1) = { FRAME_pain01, FRAME_pain06, dog_frames_pain1, dog_run };

// Pain (2)
static const mframe_t dog_frames_pain2[] = {
    { ai_move },
    { ai_move, 0, dog_pain_sound },
    { ai_move, -4 },
    { ai_move, -12 },
    { ai_move, -12 },
    { ai_move, -2 },
    { ai_move, 0 },
    { ai_move, -4 },
    { ai_move, 0 },
    { ai_move, -10 },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(dog_move_pain2) = { FRAME_painb01, FRAME_painb16, dog_frames_pain2, dog_run };

// Pain
void PAIN(dog_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    if (brandom())
        M_SetAnimation(self, &dog_move_pain1);
    else
        M_SetAnimation(self, &dog_move_pain2);
}

static void dog_dead(edict_t *self)
{
    self->r.box.maxs.z = -8 * G_EntityScale(self);
    monster_dead(self);
}

static void dog_shrink(edict_t *self)
{
    self->r.box.maxs.z = 0;
    self->r.svflags |= SVF_DEADMONSTER;
    trap_LinkEntity(self);
}

// Death (1)
static const mframe_t dog_frames_death1[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, dog_shrink },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(dog_move_death1) = { FRAME_death01, FRAME_death09, dog_frames_death1, dog_dead };

// Death (2)
static const mframe_t dog_frames_death2[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, dog_shrink },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(dog_move_death2) = { FRAME_deathb01, FRAME_deathb09, dog_frames_death2, dog_dead };

static const gib_def_t dog_gibs[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/objects/gibs/head2/tris.md2", 1, GIB_HEAD },
    { 0 }
};

// Death
void DIE(dog_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    if (self->health <= self->gib_health) {
        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        ThrowGibs(self, damage, dog_gibs);
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;

    G_StartSound(self, CHAN_VOICE, SOUND.death, 1, ATTN_NORM);

    self->deadflag = true;
    self->takedamage = true;

    if (brandom())
        M_SetAnimation(self, &dog_move_death1);
    else
        M_SetAnimation(self, &dog_move_death2);
}

// Sight
void MONSTERINFO_SIGHT(dog_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_VOICE, SOUND.sight, 1, ATTN_NORM);
}

// Search
void MONSTERINFO_SEARCH(dog_search)(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.search, 1, ATTN_NORM);
}

void PR_monster_dog(void)
{
    sound[0].melee = G_SoundIndex("dog/dattack1.wav");
    sound[0].death = G_SoundIndex("dog/ddeath.wav");
    sound[0].pain = G_SoundIndex("dog/dpain1.wav");
    sound[0].sight = G_SoundIndex("dog/dsight.wav");
    sound[0].search = G_SoundIndex("dog/idle.wav");
}

void PR_monster_dog_prototype(void)
{
    sound[1].melee = G_SoundIndex("dog/dattack1_s.wav");
    sound[1].death = G_SoundIndex("dog/ddeath_s.wav");
    sound[1].pain = G_SoundIndex("dog/dpain1_s.wav");
    sound[1].sight = G_SoundIndex("dog/dsight_s.wav");
    sound[1].search = G_SoundIndex("dog/idle_s.wav");
}

static void SP_monster_dog_x(edict_t *self)
{
    self->r.box = Box3_FromSize(16, -24, 24);
    self->r.solid = SOLID_BBOX;
    self->movetype = MOVETYPE_STEP;

    self->health = 50 * st.health_multiplier;
    self->gib_health = -35;
    self->mass = 40;

    self->pain = dog_pain;
    self->die = dog_die;
    self->monsterinfo.stand = dog_stand;
    self->monsterinfo.walk = dog_walk;
    self->monsterinfo.run = dog_run;
    self->monsterinfo.melee = dog_melee;
    self->monsterinfo.attack = dog_leap;
    self->monsterinfo.sight = dog_sight;
    self->monsterinfo.search = dog_search;
    self->monsterinfo.setskin = dog_setskin;

    trap_LinkEntity(self);

    M_SetAnimation(self, &dog_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    walkmonster_start(self);
}

void SP_monster_dog(edict_t *self)
{
    self->style = 0;
    self->s.modelindex = G_ModelIndex("models/monsters/dog/tris.md2");
    SP_monster_dog_x(self);
}

void SP_monster_dog_prototype(edict_t *self)
{
    self->style = 1;
    self->s.modelindex = G_ModelIndex("models/monsters/dog_prototype/tris.md2");
    SP_monster_dog_x(self);
}
