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
// m_knight.c

#include "g_local.h"
#include "m_knight.h"

#define SOUND   sound[!!self->style]

enum { Plain, Proto, Strogg };

static struct {
    int death;
    int pain;
    int sight;
    int search;
    int melee1;
    int melee2;
} sound[2];

static void knight_attack(edict_t *self);

static void SwingSword(edict_t *self)
{
    vec3_t aim = { MELEE_DISTANCE, 0, 24 };
    int damage = (frandom() + frandom() + frandom()) * 3;
    fire_hit(self, aim, damage, 20);
}

// Stand
static const mframe_t knight_frames_stand[] = {
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
const mmove_t MMOVE_T(knight_move_stand) = { FRAME_stand01, FRAME_stand09, knight_frames_stand, NULL };

void MONSTERINFO_STAND(knight_stand)(edict_t *self)
{
    M_SetAnimation(self, &knight_move_stand);
}

// Run
static const mframe_t knight_frames_run[] = {
    { ai_run, 16 },
    { ai_run, 20, monster_footstep },
    { ai_run, 13 },
    { ai_run,  7 },
    { ai_run, 16 },
    { ai_run, 20, monster_footstep },
    { ai_run, 14 },
    { ai_run,  6 }
};
const mmove_t MMOVE_T(knight_move_run) = { FRAME_runb01, FRAME_runb08, knight_frames_run, knight_attack };

void MONSTERINFO_RUN(knight_run)(edict_t *self)
{
    M_SetAnimation(self, &knight_move_run);
}

static void knight_attack_swing(edict_t *self)
{
    if (brandom())
        G_StartSound(self, CHAN_WEAPON, SOUND.melee1, 1, ATTN_NORM);
    else
        G_StartSound(self, CHAN_WEAPON, SOUND.melee2, 1, ATTN_NORM);
}

// Walk
static const mframe_t knight_frames_walk[] = {
    { ai_walk, 3 },
    { ai_walk, 2 },
    { ai_walk, 3 },
    { ai_walk, 4, monster_footstep },
    { ai_walk, 3 },
    { ai_walk, 3 },
    { ai_walk, 3 },
    { ai_walk, 4 },
    { ai_walk, 3 },
    { ai_walk, 3 },
    { ai_walk, 2, monster_footstep },
    { ai_walk, 3 },
    { ai_walk, 4 },
    { ai_walk, 3 }
};
const mmove_t MMOVE_T(knight_move_walk) = { FRAME_walk01, FRAME_walk14, knight_frames_walk, NULL };

void MONSTERINFO_WALK(knight_walk)(edict_t *self)
{
    M_SetAnimation(self, &knight_move_walk);
}

// Attack
static const mframe_t knight_frames_attack[] = {
    { ai_charge, 16, knight_attack_swing },
    { ai_charge, 20 },
    { ai_charge, 13 },
    { ai_charge,  7 },
    { ai_charge, 16, SwingSword },
    { ai_charge, 20, SwingSword },
    { ai_charge, 14, SwingSword },
    { ai_charge,  6, SwingSword },
    { ai_charge, 14, SwingSword },
    { ai_charge, 10 },
    { ai_charge,  7 }
};
const mmove_t MMOVE_T(knight_move_attack) = { FRAME_runattack01, FRAME_runattack11, knight_frames_attack, knight_run };

static void knight_attack(edict_t *self)
{
    if (self->enemy && range_to(self, self->enemy) < (MELEE_DISTANCE * 4))
        M_SetAnimation(self, &knight_move_attack);
    else
        M_SetAnimation(self, &knight_move_run);
}

static void knight_melee_swing(edict_t *self)
{
    G_StartSound(self, CHAN_WEAPON, SOUND.melee1, 1, ATTN_NORM);
}

// Melee
static const mframe_t knight_frames_melee[] = {
    { ai_charge, 0, knight_melee_swing },
    { ai_charge, 7 },
    { ai_charge, 4 },
    { ai_charge, 0 },
    { ai_charge, 3 },
    { ai_charge, 4, SwingSword },
    { ai_charge, 1, SwingSword },
    { ai_charge, 3, SwingSword },
    { ai_charge, 1 },
    { ai_charge, 5 }
};
const mmove_t MMOVE_T(knight_move_melee) = { FRAME_attackb01, FRAME_attackb10, knight_frames_melee, knight_run };

void MONSTERINFO_MELEE(knight_melee)(edict_t *self)
{
    M_SetAnimation(self, &knight_move_melee);
}

// Pain (1)
static const mframe_t knight_frames_pain1[] = {
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(knight_move_pain1) = { FRAME_pain01, FRAME_pain03, knight_frames_pain1, knight_run };

// Pain (2)
static const mframe_t knight_frames_pain2[] = {
    { ai_move, 0 },
    { ai_move, 3 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 2 },
    { ai_move, 4 },
    { ai_move, 2 },
    { ai_move, 5 },
    { ai_move, 5 },
    { ai_move, 0 },
    { ai_move, 0 }
};
const mmove_t MMOVE_T(knight_move_pain2) = { FRAME_painb01, FRAME_painb11, knight_frames_pain2, knight_run };

void PAIN(knight_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (level.time < self->pain_debounce_time)
        return;
    G_StartSound(self, CHAN_VOICE, SOUND.pain, 1, ATTN_NORM);

    self->pain_debounce_time = level.time + SEC(1);

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    if (frandom() < 0.85f)
        M_SetAnimation(self, &knight_move_pain1);
    else
        M_SetAnimation(self, &knight_move_pain2);
}

static void knight_dead(edict_t *self)
{
    self->r.box.maxs.z = -8 * G_EntityScale(self);
    monster_dead(self);
}

// Death (1)
static const mframe_t knight_frames_die1[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(knight_move_die1) = { FRAME_death01, FRAME_death10, knight_frames_die1, knight_dead };

// Death (2)
static const mframe_t knight_frames_die2[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(knight_move_die2) = { FRAME_deathb01, FRAME_deathb11, knight_frames_die2, knight_dead };

static const gib_def_t knight_gibs_plain[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/objects/gibs/head2/tris.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t knight_gibs_strogg[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/monsters/knightstrogg/gibs/g_arm.md2", 2 },
    { "models/monsters/knightstrogg/gibs/g_leg.md2", 2 },
    { "models/monsters/knightstrogg/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

void DIE(knight_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    if (self->health <= self->gib_health) {
        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        if (self->style == Strogg)
            ThrowGibs(self, damage, knight_gibs_strogg);
        else
            ThrowGibs(self, damage, knight_gibs_plain);
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;

    G_StartSound(self, CHAN_VOICE, SOUND.death, 1, ATTN_NORM);

    self->deadflag = true;
    self->takedamage = true;

    if (brandom())
        M_SetAnimation(self, &knight_move_die1);
    else
        M_SetAnimation(self, &knight_move_die2);
}

// Sight
void MONSTERINFO_SIGHT(knight_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_VOICE, SOUND.sight, 1, ATTN_NORM);
}

// Search
void MONSTERINFO_SEARCH(knight_search)(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.search, 1, ATTN_NORM);
}

void MONSTERINFO_SETSKIN(knight_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum |= 1;
    else
        self->s.skinnum &= ~1;
}

void PR_monster_knight(void)
{
    sound[0].melee1 = G_SoundIndex("knight/sword1.wav");
    sound[0].melee2 = G_SoundIndex("knight/sword2.wav");
    sound[0].death = G_SoundIndex("knight/kdeath.wav");
    sound[0].pain = G_SoundIndex("knight/khurt.wav");
    sound[0].sight = G_SoundIndex("knight/ksight.wav");
    sound[0].search = G_SoundIndex("knight/idle.wav");
}

void PR_monster_knight_strogg(void)
{
    sound[1].melee1 = G_SoundIndex("knight/sword1.wav");
    sound[1].melee2 = G_SoundIndex("knight/sword2.wav");
    sound[1].death = G_SoundIndex("knight/kdeath_s.wav");
    sound[1].pain = G_SoundIndex("knight/khurt_s.wav");
    sound[1].sight = G_SoundIndex("knight/ksight_s.wav");
    sound[1].search = G_SoundIndex("knight/idle_s.wav");
}

static void SP_monster_knight_x(edict_t *self)
{
    self->movetype = MOVETYPE_STEP;
    self->r.box = Box3_FromSize(16, -24, 40);
    self->r.solid = SOLID_BBOX;

    self->health *= st.health_multiplier;
    self->gib_health = -40;
    self->mass = 75;

    self->s.skinnum = brandom() * 2;

    self->pain = knight_pain;
    self->die = knight_die;
    self->monsterinfo.stand = knight_stand;
    self->monsterinfo.walk = knight_walk;
    self->monsterinfo.run = knight_run;
    self->monsterinfo.melee = knight_melee;
    self->monsterinfo.sight = knight_sight;
    self->monsterinfo.search = knight_search;
    self->monsterinfo.setskin = knight_setskin;

    trap_LinkEntity(self);

    M_SetAnimation(self, &knight_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    walkmonster_start(self);
}

void SP_monster_knight(edict_t *self)
{
    self->style = Plain;
    self->health = 75;
    self->s.modelindex = G_ModelIndex("models/monsters/knight/tris.md2");
    SP_monster_knight_x(self);
}

void SP_monster_knight_prototype(edict_t *self)
{
    self->style = Proto;
    self->health = 75;
    self->s.modelindex = G_ModelIndex("models/monsters/knight_prototype/tris.md2");
    SP_monster_knight_x(self);
}

void SP_monster_knight_strogg(edict_t *self)
{
    self->style = Strogg;
    self->health = 100;
    self->s.modelindex = G_ModelIndex("models/monsters/knightstrogg/tris.md2");
    SP_monster_knight_x(self);
    G_PrecacheGibs(knight_gibs_strogg);
}
