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

#include "g_local.h"
#include "m_fish.h"

static int sound_search;
static int sound_death;
static int sound_melee;

// Stand
static const mframe_t fish_frames_stand[] = {
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
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand }
};
const mmove_t MMOVE_T(fish_move_stand) = { FRAME_stand01, FRAME_stand18, fish_frames_stand, NULL };

void MONSTERINFO_STAND(fish_stand)(edict_t *self)
{
    M_SetAnimation(self, &fish_move_stand);
}

// Run
static const mframe_t fish_frames_run[] = {
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 },
    { ai_run, 12 }
};
const mmove_t MMOVE_T(fish_move_run) = { FRAME_stand01, FRAME_stand18, fish_frames_run, NULL };

void MONSTERINFO_RUN(fish_run)(edict_t *self)
{
    M_SetAnimation(self, &fish_move_run);
}

static const mframe_t fish_walk_run[] = {
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 4 }
};
const mmove_t MMOVE_T(fish_move_walk) = { FRAME_stand01, FRAME_stand18, fish_walk_run, NULL };

void MONSTERINFO_WALK(fish_walk)(edict_t *self)
{
    M_SetAnimation(self, &fish_move_walk);
}

static void FishBite(edict_t *self)
{
    if (!self->enemy || !self->enemy->r.inuse)
        return;

    vec3_t aim = { MELEE_DISTANCE, 0, 0 };
    fire_hit(self, aim, 6, 0);

    G_StartSound(self, CHAN_WEAPON, sound_melee, 1, ATTN_NORM);
}

// Melee
static const mframe_t fish_frames_melee[] = {
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 0, FishBite },
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 0, FishBite },
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 0, FishBite },
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 10 }
};
const mmove_t MMOVE_T(fish_move_melee) = {0, 17, fish_frames_melee, fish_run};

void MONSTERINFO_MELEE(fish_melee)(edict_t *self)
{
    M_SetAnimation(self, &fish_move_melee);
}

void MONSTERINFO_SEARCH(fish_search)(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, sound_search, 1, ATTN_NORM);
}

static void fish_dead(edict_t *self)
{
    self->r.box.maxs.z = -8;
    monster_dead(self);
}

// Death
static const mframe_t fish_frames_death[] = {
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
const mmove_t MMOVE_T(fish_move_death) = { FRAME_death01, FRAME_death21, fish_frames_death, fish_dead };

static const gib_def_t fish_gibs[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/objects/gibs/head2/tris.md2", 1, GIB_HEAD },
    { 0 }
};

void DIE(fish_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    if (M_CheckGib(self, mod)) {
        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        ThrowGibs(self, damage, fish_gibs);
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;

    G_StartSound(self, CHAN_VOICE, sound_death, 1, ATTN_NORM);

    self->deadflag = true;
    self->takedamage = true;

    M_SetAnimation(self, &fish_move_death);
}

// Pain
static const mframe_t fish_frames_pain[] = {
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
const mmove_t MMOVE_T(fish_move_pain) = { FRAME_pain01, FRAME_pain09, fish_frames_pain, fish_run };

void PAIN(fish_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + SEC(3);

    M_SetAnimation(self, &fish_move_pain);
}

static void fish_set_fly_parameters(edict_t *self)
{
    self->monsterinfo.fly_thrusters = false;
    self->monsterinfo.fly_acceleration = 30;
    self->monsterinfo.fly_speed = 110;
    self->monsterinfo.fly_min_distance = 10;
    self->monsterinfo.fly_max_distance = 10;
}

void PR_monster_fish(void)
{
    sound_search = G_SoundIndex("fish/idle.wav");
    sound_death = G_SoundIndex("fish/death.wav");
    sound_melee = G_SoundIndex("fish/bite.wav");
}

void SP_monster_fish(edict_t *self)
{
    self->s.modelindex = G_ModelIndex("models/monsters/fish/tris.md2");
    self->r.box = Box3_FromSize(16, -24, 24);
    self->r.solid = SOLID_BBOX;
    self->movetype = MOVETYPE_STEP;

    self->health = 25 * st.health_multiplier;
    self->gib_health = -25;
    self->mass = 25;

    self->pain = fish_pain;
    self->die = fish_die;
    self->monsterinfo.stand = fish_stand;
    self->monsterinfo.walk = fish_walk;
    self->monsterinfo.run = fish_run;
    self->monsterinfo.melee = fish_melee;
    self->monsterinfo.search = fish_search;

    trap_LinkEntity(self);

    M_SetAnimation(self, &fish_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
    fish_set_fly_parameters(self);

    swimmonster_start(self);
}
