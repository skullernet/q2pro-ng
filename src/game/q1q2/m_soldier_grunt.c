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
#include "m_soldier_grunt.h"

#define SOUND   sound[!!self->style]

enum { Plain, Proto, Double, Strogg };

static struct {
    int death;
    int search;
    int pain1;
    int pain2;
    int sight;
    int beam;
} sound[2];

// Stand
static const mframe_t grunt_frames_stand[] = {
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand }
};
const mmove_t MMOVE_T(grunt_move_stand) = { FRAME_stand01, FRAME_stand08, grunt_frames_stand, NULL };

void MONSTERINFO_STAND(grunt_stand)(edict_t *self)
{
    M_SetAnimation(self, &grunt_move_stand);
}

// Run
static const mframe_t grunt_frames_run[] = {
    { ai_run, 11 },
    { ai_run, 15, monster_footstep },
    { ai_run, 10 },
    { ai_run, 10 },
    { ai_run, 8 },
    { ai_run, 15, monster_footstep },
    { ai_run, 10 },
    { ai_run, 8 }
};
const mmove_t MMOVE_T(grunt_move_run) = { FRAME_run01, FRAME_run08, grunt_frames_run, NULL };

void MONSTERINFO_RUN(grunt_run)(edict_t *self)
{
    M_SetAnimation(self, &grunt_move_run);
}

// Run
static const mframe_t grunt_frames_walk[] = {
    { ai_walk, 1 },
    { ai_walk, 1 },
    { ai_walk, 1 },
    { ai_walk, 1 },
    { ai_walk, 2 },
    { ai_walk, 3 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 1, monster_footstep },
    { ai_walk, 0 },
    { ai_walk, 1 },
    { ai_walk, 1 },
    { ai_walk, 1 },
    { ai_walk, 3 },
    { ai_walk, 3 },
    { ai_walk, 3 },
    { ai_walk, 3 },
    { ai_walk, 2 },
    { ai_walk, 1 },
    { ai_walk, 1 },
    { ai_walk, 1, monster_footstep }
};
const mmove_t MMOVE_T(grunt_move_walk) = { FRAME_prowl01, FRAME_prowl24, grunt_frames_walk, NULL };

void MONSTERINFO_WALK(grunt_walk)(edict_t *self)
{
    M_SetAnimation(self, &grunt_move_walk);
}

static void soldier_fire_checker(edict_t *self)
{
    self->fly_sound_debounce_time = level.time + SEC(1.5f);
}

static void soldier_fire_strogg(edict_t *self)
{
    vec3_t  start;
    vec3_t  dir;
    vec3_t  forward;
    vec3_t  right;

    if (!self->enemy || !self->enemy->r.inuse)
        return;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_GRUNT_BEAM], forward, right);

    M_PredictAim(self, self->enemy, start, 0, false, 0.1f, &dir, NULL);

    monster_fire_heatbeam(self, start, dir, start, 1, 0, MZ2_GRUNT_BEAM);

    if (self->fly_sound_debounce_time > level.time)
        self->monsterinfo.nextframe = FRAME_shoot05;
    else
        self->monsterinfo.weapon_sound = 0;
}

static void soldier_beamfire_sound(edict_t *self)
{
    self->monsterinfo.weapon_sound = SOUND.beam;
}

static const mframe_t stroggsoldier_frames_attack[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, soldier_beamfire_sound },
    { ai_charge, 0, soldier_fire_checker },
    { ai_charge, 0, soldier_fire_strogg },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
const mmove_t MMOVE_T(stroggsoldier_move_attack) = { FRAME_shoot01, FRAME_shoot09, stroggsoldier_frames_attack, grunt_run };

static void grunt_double_fire_left(edict_t *self)
{
    vec3_t start, forward, right, aim, end;

    if (!self->enemy || !self->enemy->r.inuse)
        return;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_GRUNT_HYPERGUN_L], forward, right);

    end = self->enemy->s.origin;
    end.z += self->enemy->viewheight;
    aim = Vec3_Direction(end, start);

    monster_fire_blueblaster(self, start, aim, 1, 600, MZ2_GRUNT_HYPERGUN_L, EF_BLUEHYPERBLASTER);
}

static void grunt_double_fire_right(edict_t *self)
{
    vec3_t start, forward, right, aim, end;

    if (!self->enemy || !self->enemy->r.inuse)
        return;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_GRUNT_HYPERGUN_R], forward, right);

    end = self->enemy->s.origin;
    end.z += self->enemy->viewheight;
    aim = Vec3_Direction(end, start);

    monster_fire_blueblaster(self, start, aim, 1, 600, MZ2_GRUNT_HYPERGUN_R, EF_BLUEHYPERBLASTER);
}

static const mframe_t grunt_double_frames_attack[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, grunt_double_fire_right },
    { ai_charge, 0, grunt_double_fire_left },
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
const mmove_t MMOVE_T(grunt_double_move_attack) = { FRAME_shoot01, FRAME_shoot09, grunt_double_frames_attack, grunt_run };

static void grunt_fire(edict_t *self)
{
    vec3_t forward, right, up;
    vec3_t start, end, dir, vec, aim;
    float r, u;

    if (!self->enemy || !self->enemy->r.inuse)
        return;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_GRUNT_SHOTGUN], forward, right);

    vec = self->enemy->s.origin;
    vec.z += self->enemy->viewheight;

    aim = Vec3_Direction(vec, start);

    dir = vectoangles(aim);
    AngleVectors(dir, &forward, &right, &up);

    r = crandom() * 1000;
    u = crandom() * 500;

    end = Vec3_MA(start, 8192, forward);
    end = Vec3_MA(end, r, right);
    end = Vec3_MA(end, u, up);

    aim = Vec3_Direction(end, start);
    monster_fire_shotgun(self, start, aim, 2, 1, 1500, 750, 9, MZ2_GRUNT_SHOTGUN);
}

static const mframe_t grunt_frames_attack[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, grunt_fire },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
const mmove_t MMOVE_T(grunt_move_attack) = { FRAME_shoot01, FRAME_shoot09, grunt_frames_attack, grunt_run };

void MONSTERINFO_ATTACK(grunt_attack)(edict_t *self)
{
    switch (self->style) {
    case Strogg:
        M_SetAnimation(self, &stroggsoldier_move_attack);
        break;
    case Double:
        M_SetAnimation(self, &grunt_double_move_attack);
        break;
    default:
        M_SetAnimation(self, &grunt_move_attack);
        break;
    }
}

void MONSTERINFO_SETSKIN(grunt_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

// Pain (1)
static const mframe_t grunt_frames_pain1[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(grunt_move_pain1) = { FRAME_pain01, FRAME_pain06, grunt_frames_pain1, grunt_run };

// Pain (2)
static const mframe_t grunt_frames_pain2[] = {
    { ai_move },
    { ai_move, 13 },
    { ai_move, 9 },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 2 },
    { ai_move }
};
const mmove_t MMOVE_T(grunt_move_pain2) = { FRAME_painb01, FRAME_painb14, grunt_frames_pain2, grunt_run };

// Pain (3)
static const mframe_t grunt_frames_pain3[] = {
    { ai_move, 0 },
    { ai_move, 1 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 1 },
    { ai_move, 1 },
    { ai_move, 0 },
    { ai_move, 1 },
    { ai_move, 4 },
    { ai_move, 3 },
    { ai_move, 6 },
    { ai_move, 8 },
    { ai_move, 2 }
};
const mmove_t MMOVE_T(grunt_move_pain3) = { FRAME_painc01, FRAME_painc13, grunt_frames_pain3, grunt_run };

// Pain
void PAIN(grunt_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    float r;

    if (self->pain_debounce_time > level.time)
        return;

    self->monsterinfo.weapon_sound = 0;

    r = frandom();
    if (r < 0.2f) {
        self->pain_debounce_time = level.time + SEC(6);
        G_StartSound(self, CHAN_VOICE, SOUND.pain1, 1, ATTN_NORM);
    } else {
        self->pain_debounce_time = level.time + SEC(1);
        G_StartSound(self, CHAN_VOICE, SOUND.pain2, 1, ATTN_NORM);
    }

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    if (r < 0.2f)
        M_SetAnimation(self, &grunt_move_pain1);
    else if (r < 0.6f)
        M_SetAnimation(self, &grunt_move_pain2);
    else
        M_SetAnimation(self, &grunt_move_pain3);
}

static void grunt_dead(edict_t *self)
{
    self->r.box.maxs.z = -8 * G_EntityScale(self);
    monster_dead(self);
}

// Death (1)
static const mframe_t grunt_frames_death1[] = {
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
const mmove_t MMOVE_T(grunt_move_death1) = { FRAME_death01, FRAME_death10, grunt_frames_death1, grunt_dead };

// Death (2)
static const mframe_t grunt_frames_death2[] = {
    { ai_move },
    { ai_move, -5 },
    { ai_move, -4 },
    { ai_move, -13 },
    { ai_move, -3 },
    { ai_move, -4 },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(grunt_move_death2) = { FRAME_deathc01, FRAME_deathc11, grunt_frames_death2, grunt_dead };

static const gib_def_t grunt_gibs_plain[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/objects/gibs/head2/tris.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t grunt_gibs_double[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/monsters/grunt_double/gibs/g_arm.md2", 2 },
    { "models/monsters/grunt_double/gibs/g_leg.md2", 2 },
    { "models/monsters/grunt_double/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t grunt_gibs_strogg[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/monsters/grunt_strogg/gibs/g_arm.md2", 2 },
    { "models/monsters/grunt_strogg/gibs/g_leg.md2", 2 },
    { "models/monsters/grunt_strogg/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

// Death
void DIE(grunt_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    self->monsterinfo.weapon_sound = 0;

    if (self->health <= self->gib_health) {
        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        switch (self->style) {
        case Strogg:
            ThrowGibs(self, damage, grunt_gibs_strogg);
            break;
        case Double:
            ThrowGibs(self, damage, grunt_gibs_double);
            break;
        default:
            ThrowGibs(self, damage, grunt_gibs_plain);
            break;
        }
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;
    G_StartSound(self, CHAN_VOICE, SOUND.death, 1, ATTN_NORM);

    self->deadflag = true;
    self->takedamage = true;

    if (self->style >= Double && brandom())
        Drop_Item(self, GetItemByIndex(IT_AMMO_CELLS))->count = 10;

    if (brandom())
        M_SetAnimation(self, &grunt_move_death1);
    else
        M_SetAnimation(self, &grunt_move_death2);
}

// Sight
void MONSTERINFO_SIGHT(grunt_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_VOICE, SOUND.sight, 1, ATTN_NORM);
}

// Search
void MONSTERINFO_SEARCH(grunt_search)(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.search, 1, ATTN_NORM);
}

void PR_monster_soldier_grunt(void)
{
    sound[0].death = G_SoundIndex("army/death1.wav");
    sound[0].search = G_SoundIndex("army/idle.wav");
    sound[0].pain1 = G_SoundIndex("army/pain1.wav");
    sound[0].pain2 = G_SoundIndex("army/pain2.wav");
    sound[0].sight = G_SoundIndex("army/sight1.wav");
}

void PR_monster_soldier_strogg(void)
{
    sound[1].death = G_SoundIndex("army/death1_s.wav");
    sound[1].search = G_SoundIndex("army/idle_s.wav");
    sound[1].pain1 = G_SoundIndex("army/pain1_s.wav");
    sound[1].pain2 = G_SoundIndex("army/pain2_s.wav");
    sound[1].sight = G_SoundIndex("army/sight1_s.wav");
    sound[1].beam = G_SoundIndex("army/bfgbeam.wav");
}

static void SP_monster_soldier_grunt_x(edict_t *self)
{
    self->movetype = MOVETYPE_STEP;
    self->r.box = Box3_FromSize(16, -24, 40);
    self->r.solid = SOLID_BBOX;

    self->health *= st.health_multiplier;
    self->gib_health = -35;
    self->mass = 30;

    self->pain = grunt_pain;
    self->die = grunt_die;
    self->monsterinfo.stand = grunt_stand;
    self->monsterinfo.walk = grunt_walk;
    self->monsterinfo.run = grunt_run;
    self->monsterinfo.attack = grunt_attack;
    self->monsterinfo.sight = grunt_sight;
    self->monsterinfo.search = grunt_search;
    self->monsterinfo.setskin = grunt_setskin;

    trap_LinkEntity(self);

    M_SetAnimation(self, &grunt_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    walkmonster_start(self);
}

void SP_monster_soldier_grunt(edict_t *self)
{
    self->style = Plain;
    self->health = 60;
    self->s.modelindex = G_ModelIndex("models/monsters/grunt/tris.md2");
    SP_monster_soldier_grunt_x(self);
}

void SP_monster_soldier_grunt_prototype(edict_t *self)
{
    self->style = Proto;
    self->health = 30;
    self->s.modelindex = G_ModelIndex("models/monsters/grunt_prototype/tris.md2");
    SP_monster_soldier_grunt_x(self);
}

void SP_monster_soldier_grunt_double(edict_t *self)
{
    self->style = Double;
    self->health = 60;
    self->s.modelindex = G_ModelIndex("models/monsters/grunt_double/tris.md2");
    SP_monster_soldier_grunt_x(self);
    G_PrecacheGibs(grunt_gibs_double);
}

void SP_monster_soldier_strogg(edict_t *self)
{
    self->style = Strogg;
    self->health = 30;
    self->s.modelindex = G_ModelIndex("models/monsters/grunt_strogg/tris.md2");
    SP_monster_soldier_grunt_x(self);
    G_PrecacheGibs(grunt_gibs_strogg);
}
