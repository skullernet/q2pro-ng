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
// m_shalrath_strogg.cpp

#include "g_local.h"
#include "m_shalrath_strogg.h"

static int sound_death;
static int sound_search;
static int sound_pain;
static int sound_attack;
static int sound_fire;
static int sound_sight;

void fire_shalrath_pod(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed);

// Stand
static const mframe_t shalrath_strogg_frames_stand[] = {
    { ai_stand },
    { ai_stand },
    { ai_stand },
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
const mmove_t MMOVE_T(shalrath_strogg_move_stand) = { FRAME_stand01, FRAME_stand12, shalrath_strogg_frames_stand, NULL };

void MONSTERINFO_STAND(shalrath_strogg_stand)(edict_t *self)
{
    M_SetAnimation(self, &shalrath_strogg_move_stand);
}

// Walk
static const mframe_t shalrath_strogg_frames_walk[] = {
    { ai_walk, 0 },
    { ai_walk, 3 },
    { ai_walk, 4 },
    { ai_walk, 5 },
    { ai_walk, 6 },
    { ai_walk, 8 },
    { ai_walk, 1 },
    { ai_walk, 3 },
    { ai_walk, 4 },
    { ai_walk, 5 },
    { ai_walk, 6 },
    { ai_walk, 8 }
};
const mmove_t MMOVE_T(shalrath_strogg_move_walk) = { FRAME_walk01, FRAME_walk12, shalrath_strogg_frames_walk, NULL };

void MONSTERINFO_WALK(shalrath_strogg_walk)(edict_t *self)
{
    M_SetAnimation(self, &shalrath_strogg_move_walk);
}

// Run
static const mframe_t shalrath_strogg_frames_run[] = {
    { ai_run, 0 },
    { ai_run, 3 },
    { ai_run, 4 },
    { ai_run, 5 },
    { ai_run, 6 },
    { ai_run, 8 },
    { ai_run, 1 },
    { ai_run, 3 },
    { ai_run, 4 },
    { ai_run, 5 },
    { ai_run, 6 },
    { ai_run, 8 }
};
const mmove_t MMOVE_T(shalrath_strogg_move_run) = { FRAME_walk01, FRAME_walk12, shalrath_strogg_frames_run, NULL };

void MONSTERINFO_RUN(shalrath_strogg_run)(edict_t *self)
{
    M_SetAnimation(self, &shalrath_strogg_move_run);
}

static void shalrath_strogg_roar(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, sound_attack, 1, ATTN_NORM);
}

static void FireShalrathStroggPod(edict_t *self)
{
    vec3_t  forward, right;
    vec3_t  start, dir;
    vec3_t  offset = { 16, 0, 16 };

    if (!self->enemy || !self->enemy->r.inuse)
        return;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, offset, forward, right);

    dir = Vec3_Direction(self->enemy->s.origin, start);
    fire_shalrath_pod(self, start, dir, 40, 400);

    G_StartSound(self, CHAN_WEAPON, sound_fire, 1, ATTN_NORM);
}

// Attack
static const mframe_t shalrath_strogg_frames_attack[] = {
    { ai_charge, 0, shalrath_strogg_roar },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, FireShalrathStroggPod },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
const mmove_t MMOVE_T(shalrath_strogg_move_attack) = { FRAME_attack01, FRAME_attack14, shalrath_strogg_frames_attack, shalrath_strogg_run };

void MONSTERINFO_ATTACK(shalrath_strogg_attack)(edict_t *self)
{
    if (self->timestamp <= level.time && brandom()) {
        M_SetAnimation(self, &shalrath_strogg_move_attack);
        self->timestamp = level.time + random_time_sec(2, 3);
    }
}

// Pain
static const mframe_t shalrath_strogg_frames_pain1[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(shalrath_strogg_move_pain1) = { FRAME_paina01, FRAME_paina05, shalrath_strogg_frames_pain1, shalrath_strogg_run };

static const mframe_t shalrath_strogg_frames_pain2[] = {
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
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(shalrath_strogg_move_pain2) = { FRAME_painb01, FRAME_painb16, shalrath_strogg_frames_pain2, shalrath_strogg_run };

void PAIN(shalrath_strogg_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (level.time < self->pain_debounce_time)
        return;

    G_StartSound(self, CHAN_VOICE, sound_pain, 1, ATTN_NORM);

    self->pain_debounce_time = level.time + SEC(3);

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    if (brandom())
        M_SetAnimation(self, &shalrath_strogg_move_pain2);
    else
        M_SetAnimation(self, &shalrath_strogg_move_pain1);
}

void MONSTERINFO_SETSKIN(shalrath_strogg_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

static void shalrath_strogg_dead(edict_t *self)
{
    self->r.box.maxs.z = -8;
    monster_dead(self);
}

// Death
static const mframe_t shalrath_strogg_frames_death[] = {
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
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(shalrath_strogg_move_death) = { FRAME_death01, FRAME_death28, shalrath_strogg_frames_death, shalrath_strogg_dead };

static const gib_def_t shalrath_strogg_gibs[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/monsters/shalrathstrogg/gibs/g_arm.md2", 2 },
    { "models/monsters/shalrathstrogg/gibs/g_leg.md2", 4 },
    { "models/monsters/shalrathstrogg/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

void DIE(shalrath_strogg_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    if (self->health <= self->gib_health) {
        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        ThrowGibs(self, damage, shalrath_strogg_gibs);
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;

    G_StartSound(self, CHAN_VOICE, sound_death, 1, ATTN_NORM);

    self->deadflag = true;
    self->takedamage = true;
    M_SetAnimation(self, &shalrath_strogg_move_death);
}

// Sight
void MONSTERINFO_SIGHT(shalrath_strogg_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM);
}

// Search
void MONSTERINFO_SEARCH(shalrath_strogg_search)(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, sound_search, 1, ATTN_NORM);
}

void PR_monster_shalrath_strogg(void)
{
    sound_death = G_SoundIndex("shalrath/death_s.wav");
    sound_search = G_SoundIndex("shalrath/idle_s.wav");
    sound_pain = G_SoundIndex("shalrath/pain_s.wav");
    sound_attack = G_SoundIndex("shalrath/attack_s.wav");
    sound_fire = G_SoundIndex("shalrath/attack2_s.wav");
    sound_sight = G_SoundIndex("shalrath/sight_s.wav");
}

void SP_monster_shalrath_strogg(edict_t *self)
{
    self->s.modelindex = G_ModelIndex("models/monsters/shalrathstrogg/tris.md2");

    G_PrecacheGibs(shalrath_strogg_gibs);

    self->r.box = Box3_FromSize(32, -24, 48);
    self->r.solid = SOLID_BBOX;
    self->movetype = MOVETYPE_STEP;

    self->health = 400 * st.health_multiplier;
    self->gib_health = -90;
    self->mass = 400;

    self->pain = shalrath_strogg_pain;
    self->die = shalrath_strogg_die;
    self->monsterinfo.stand = shalrath_strogg_stand;
    self->monsterinfo.walk = shalrath_strogg_walk;
    self->monsterinfo.run = shalrath_strogg_run;
    self->monsterinfo.attack = shalrath_strogg_attack;
    self->monsterinfo.sight = shalrath_strogg_sight;
    self->monsterinfo.search = shalrath_strogg_search;
    self->monsterinfo.setskin = shalrath_strogg_setskin;

    trap_LinkEntity(self);

    M_SetAnimation(self, &shalrath_strogg_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    walkmonster_start(self);
}
