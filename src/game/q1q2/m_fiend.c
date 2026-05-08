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
// m_demon.c

#include "g_local.h"
#include "m_fiend.h"

#define SPAWNFLAG_FIEND_NOJUMPING 8

#define SOUND   sound[!!self->style]

enum { Plain, Proto, Strogg };

static struct {
    int swing;
    int hit;
    int jump;
    int land;
    int pain;
    int death;
    int sight;
    int search;
} sound[2];

// Stand
static const mframe_t fiend_frames_stand[] = {
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
const mmove_t MMOVE_T(fiend_move_stand) = { FRAME_stand01, FRAME_stand13, fiend_frames_stand, NULL };

void MONSTERINFO_STAND(fiend_stand)(edict_t *self)
{
    M_SetAnimation(self, &fiend_move_stand);
}

// Run
static const mframe_t fiend_frames_run[] = {
    { ai_run, 20 },
    { ai_run, 15, monster_footstep },
    { ai_run, 36 },
    { ai_run, 20 },
    { ai_run, 15, monster_footstep },
    { ai_run, 36 }
};
const mmove_t MMOVE_T(fiend_move_run) = { FRAME_run01, FRAME_run06, fiend_frames_run, NULL };

void MONSTERINFO_RUN(fiend_run)(edict_t *self)
{
    M_SetAnimation(self, &fiend_move_run);
}

// Walk
static const mframe_t fiend_frames_walk[] = {
    { ai_walk, 8, monster_footstep },
    { ai_walk, 6 },
    { ai_walk, 6 },
    { ai_walk, 7 },
    { ai_walk, 4, monster_footstep },
    { ai_walk, 6 },
    { ai_walk, 10 },
    { ai_walk, 10 }
};
const mmove_t MMOVE_T(fiend_move_walk) = { FRAME_walk01, FRAME_walk08, fiend_frames_walk, NULL };

void MONSTERINFO_WALK(fiend_walk)(edict_t *self)
{
    M_SetAnimation(self, &fiend_move_walk);
}

// Melee
static void fiend_hit_left(edict_t *self)
{
    vec3_t aim = { MELEE_DISTANCE, self->r.box.mins.x, 8 };
    if (fire_hit(self, aim, irandom2(10, 16), 100)) {
        G_StartSound(self, CHAN_WEAPON, SOUND.hit, 1, ATTN_NORM);
    } else {
        G_StartSound(self, CHAN_WEAPON, SOUND.swing, 1, ATTN_NORM);
        self->monsterinfo.melee_debounce_time = level.time + SEC(1.5f);
    }
}

static void fiend_hit_right(edict_t *self)
{
    vec3_t aim = { MELEE_DISTANCE, self->r.box.maxs.x, 8 };
    if (fire_hit(self, aim, irandom2(10, 16), 100)) {
        G_StartSound(self, CHAN_WEAPON, SOUND.hit, 1, ATTN_NORM);
    } else {
        G_StartSound(self, CHAN_WEAPON, SOUND.swing, 1, ATTN_NORM);
        self->monsterinfo.melee_debounce_time = level.time + SEC(1.5f);
    }
}

static void fiend_check_refire(edict_t *self)
{
    if (!has_valid_enemy(self))
        return;

    if ((self->monsterinfo.melee_debounce_time <= level.time) && ((brandom()) || (range_to(self, self->enemy) <= RANGE_MELEE)))
        self->monsterinfo.nextframe = FRAME_attacka01;
}

static const mframe_t fiend_frames_melee[] = {
    { ai_charge, 4 },
    { ai_charge, 0 },
    { ai_charge, 0 },
    { ai_charge, 1 },
    { ai_charge, 14, fiend_hit_left },
    { ai_charge, 1 },
    { ai_charge, 6 },
    { ai_charge, 8 },
    { ai_charge, 4 },
    { ai_charge, 2 },
    { ai_charge, 12, fiend_hit_right },
    { ai_charge, 5 },
    { ai_charge, 8 },
    { ai_charge, 4 },
    { ai_charge, 4, fiend_check_refire }
};
const mmove_t MMOVE_T(fiend_move_melee) = { FRAME_attacka01, FRAME_attacka15, fiend_frames_melee, fiend_run };

void MONSTERINFO_MELEE(fiend_melee)(edict_t *self)
{
    M_SetAnimation(self, &fiend_move_melee);
}

// Attack (jump)
void TOUCH(fiend_jump_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    vec3_t point;
    vec3_t normal;
    float  length;
    int    damage;

    if (self->health <= 0) {
        self->touch = NULL;
        return;
    }

    if (self->dmg == 1 && other->takedamage) {
        normal = Vec3_NormalizeLength(self->velocity, &length);
        if (length > 30) {
            point = Vec3_MA(self->s.origin, self->r.box.maxs.x, normal);
            damage = irandom2(10, 21);
            T_Damage(other, self, self, self->velocity, point, DirToByte(normal), damage, damage, DAMAGE_NONE, MOD_UNKNOWN);
            self->dmg = 0;
        }
    }

    if (!M_CheckBottom(self)) {
        if (self->groundentity) {
            self->monsterinfo.nextframe = FRAME_leap02;
            self->touch = NULL;
        }
        return;
    }

    self->touch = NULL;
}

static void fiend_jump_takeoff(edict_t *self)
{
    vec3_t forward;

    AngleVectors(self->s.angles, &forward, NULL, NULL);
    self->s.origin.z += 1;
    self->velocity.x = 600 * forward.x;
    self->velocity.y = 600 * forward.y;
    self->velocity.z = 250;
    self->groundentity = NULL;
    self->monsterinfo.aiflags |= AI_DUCKED;
    self->monsterinfo.attack_finished = level.time + SEC(3);
    self->dmg = 1;
    self->touch = fiend_jump_touch;
}

static void fiend_check_landing(edict_t *self)
{
    monster_jump_finished(self);

    if (self->groundentity) {
        G_StartSound(self, CHAN_WEAPON, SOUND.land, 1, ATTN_NORM);
        self->monsterinfo.attack_finished = level.time + random_time_sec(0.5f, 1.5f);
        self->monsterinfo.aiflags &= ~AI_DUCKED;

        if (self->enemy && range_to(self, self->enemy) <= RANGE_MELEE * 2)
            self->monsterinfo.melee(self);

        return;
    }

    if (level.time > self->monsterinfo.attack_finished)
        self->monsterinfo.nextframe = FRAME_leap02;
    else
        self->monsterinfo.nextframe = FRAME_leap06;
}

static void fiend_roar(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.jump, 1, ATTN_NORM);
}

static const mframe_t fiend_frames_jump[] = {
    { ai_charge },
    { ai_charge, 0, fiend_roar },
    { ai_charge },
    { ai_charge, 0, fiend_jump_takeoff },
    { ai_move },
    { ai_move, 0, fiend_check_landing },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(fiend_move_jump) = { FRAME_leap01, FRAME_leap12, fiend_frames_jump, fiend_run };

// Attack (slam)
static void fiend_attack_slam(edict_t *self)
{
    vec3_t f, r, start;
    AngleVectors(self->s.angles, &f, &r, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_GENERIC_SLAM], f, r);
    trace_t tr = G_TraceLine(self->s.origin, start, self->s.number, MASK_SOLID);

    G_AddEvent(self, EV_MUZZLEFLASH2, MZ2_GENERIC_SLAM);
    self->gravity = 1.0f;
    self->velocity = vec3_origin;
    self->flags |= FL_KILL_VELOCITY;

    T_SlamRadiusDamage(tr.endpos, self, self, 100, 300, self, NULL, 165, MOD_UNKNOWN);
}

void TOUCH(fiend_jump_touch_slam)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    if (self->health <= 0) {
        self->touch = NULL;
        return;
    }

    if (self->groundentity) {
        self->s.frame = FRAME_leap12;

        if (self->touch)
            fiend_attack_slam(self);

        self->touch = NULL;
    }
}

static void fiend_high_gravity(edict_t *self)
{
    if (self->velocity.z < 0)
        self->gravity = 2.25f * (800.0f / level.gravity);
    else
        self->gravity = 5.25f * (800.0f / level.gravity);
}

static void fiend_jump_takeoff_slam(edict_t *self)
{
    if (!self->enemy)
        return;

    // immediately turn to where we need to go
    float length = Vec3_Distance(self->s.origin, self->enemy->s.origin);
    float fwd_speed = length * 1.95f;
    vec3_t dir, forward;
    M_PredictAim(self, self->enemy, self->s.origin, fwd_speed, false, 0, &dir, NULL);
    self->s.angles.yaw = vectoyaw(dir);
    AngleVectors(self->s.angles, &forward, NULL, NULL);
    self->s.origin.z += 1;
    self->velocity = Vec3_Scale(forward, fwd_speed);
    self->velocity.z = 450;
    self->groundentity = NULL;
    self->monsterinfo.aiflags |= AI_DUCKED;
    self->monsterinfo.attack_finished = level.time + SEC(3);
    self->touch = fiend_jump_touch_slam;
    fiend_high_gravity(self);
}

static void fiend_check_landing_slam(edict_t *self)
{
    fiend_high_gravity(self);

    if (self->groundentity) {
        self->monsterinfo.attack_finished = level.time + random_time_sec(0.5f, 1.5f);
        self->monsterinfo.aiflags &= ~AI_DUCKED;
        self->s.frame = FRAME_leap12;
        if (self->touch) {
            fiend_attack_slam(self);
            self->touch = NULL;
        }
        self->flags &= ~FL_KILL_VELOCITY;
        return;
    }

    if (level.time > self->monsterinfo.attack_finished)
        self->monsterinfo.nextframe = FRAME_leap05;
    else
        self->monsterinfo.nextframe = FRAME_leap07;
}

static const mframe_t fiend_frames_jump_slam[] = {
    { ai_charge },
    { ai_charge, 0, fiend_roar },
    { ai_charge },
    { ai_charge, 0, fiend_jump_takeoff_slam },
    { ai_move, 0, fiend_high_gravity },
    { ai_move, 0, fiend_check_landing_slam },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(fiend_move_jump_slam) = { FRAME_leap01, FRAME_leap12, fiend_frames_jump_slam, fiend_run };

void MONSTERINFO_ATTACK(fiend_attack)(edict_t *self)
{
    if (self->style == Strogg)
        M_SetAnimation(self, &fiend_move_jump_slam);
    else
        M_SetAnimation(self, &fiend_move_jump);
}

// Check attack
static bool fiend_check_melee(edict_t *self)
{
    return range_to(self, self->enemy) <= RANGE_MELEE && self->monsterinfo.melee_debounce_time <= level.time;
}

static bool fiend_check_jump(edict_t *self)
{
    vec3_t  dir;
    float   distance;

    // don't jump if there's no way we can reach standing height
    if (self->r.absbox.mins.z + 150 < self->enemy->r.absbox.mins.z)
        return false;

    dir = Vec3_Sub(self->s.origin, self->enemy->s.origin);
    distance = Vec2_Length(Vec2_FromVec3(dir));

    // if we're not trying to avoid a melee, then don't jump
    if (distance < 100 && self->monsterinfo.melee_debounce_time <= level.time)
        return false;

    // only use it to close distance gaps
    if (distance > 265)
        return false;

    return self->monsterinfo.attack_finished < level.time;
}

bool MONSTERINFO_CHECKATTACK(fiend_checkattack)(edict_t *self)
{
    if (!self->enemy || self->enemy->health <= 0)
        return false;

    if (fiend_check_melee(self)) {
        self->monsterinfo.attack_state = AS_MELEE;
        return true;
    }

    if (!(self->spawnflags & SPAWNFLAG_FIEND_NOJUMPING) && fiend_check_jump(self)) {
        self->monsterinfo.attack_state = AS_MISSILE;
        return true;
    }

    return false;
}

// Pain
static void fiend_pain_sound(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.pain, 1, ATTN_NORM);
}

static const mframe_t fiend_frames_pain[] = {
    { ai_move },
    { ai_move, 0, fiend_pain_sound },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(fiend_move_pain) = { FRAME_pain01, FRAME_pain06, fiend_frames_pain, fiend_run };

void PAIN(fiend_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (!M_ShouldReactToPain(self, mod))
        return;
    if (self->pain_debounce_time > level.time)
        return;
    self->pain_debounce_time = level.time + SEC(1);

    if (frandom() * 200 > damage)
        return;
    M_SetAnimation(self, &fiend_move_pain);
}

void MONSTERINFO_SETSKIN(fiend_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

static void fiend_dead(edict_t *self)
{
    self->r.box.maxs.z = -8 * G_EntityScale(self);
    monster_dead(self);
}

static void fiend_death_sound(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.death, 1, ATTN_NORM);
}

static const mframe_t fiend_frames_die[] = {
    { ai_move },
    { ai_move, 0, fiend_death_sound },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(fiend_move_die) = { FRAME_death01, FRAME_death09, fiend_frames_die, fiend_dead };

static const gib_def_t fiend_gibs_plain[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/objects/gibs/head2/tris.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t fiend_gibs_strogg[] = {
    { "models/objects/gibs/bone/tris.md2" },
    { "models/objects/gibs/sm_meat/tris.md2" },
    { "models/monsters/fiendstrogg/gibs/g_arm.md2", 2 },
    { "models/monsters/fiendstrogg/gibs/g_leg.md2", 2 },
    { "models/monsters/fiendstrogg/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

void DIE(fiend_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    if (self->health <= self->gib_health) {
        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        if (self->style == Strogg)
            ThrowGibs(self, damage, fiend_gibs_strogg);
        else
            ThrowGibs(self, damage, fiend_gibs_plain);
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;

    self->deadflag = true;
    self->takedamage = true;

    M_SetAnimation(self, &fiend_move_die);
}

// Sight
void MONSTERINFO_SIGHT(fiend_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_VOICE, SOUND.sight, 1, ATTN_NORM);
}

// Search
void MONSTERINFO_SEARCH(fiend_search)(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.search, 1, ATTN_NORM);
}

void PR_monster_fiend(void)
{
    sound[0].swing = G_SoundIndex("mutant/mutatck1.wav");
    sound[0].hit = G_SoundIndex("demon/dhit2.wav");
    sound[0].jump = G_SoundIndex("demon/djump.wav");
    sound[0].land = G_SoundIndex("demon/dland2.wav");
    sound[0].pain = G_SoundIndex("demon/dpain1.wav");
    sound[0].death = G_SoundIndex("demon/ddeath.wav");
    sound[0].search = G_SoundIndex("demon/idle1.wav");
    sound[0].sight = G_SoundIndex("demon/sight2.wav");
}

void PR_monster_fiend_strogg(void)
{
    sound[1].swing = G_SoundIndex("mutant/mutatck1.wav");
    sound[1].hit = G_SoundIndex("demon/dhit2.wav");
    sound[1].jump = G_SoundIndex("demon/djump_s.wav");
    sound[1].land = G_SoundIndex("demon/dland2.wav");
    sound[1].pain = G_SoundIndex("demon/dpain1_s.wav");
    sound[1].death = G_SoundIndex("demon/ddeath_s.wav");
    sound[1].search = G_SoundIndex("demon/idle1_s.wav");
    sound[1].sight = G_SoundIndex("demon/sight2_s.wav");
}

static void SP_monster_fiend_x(edict_t *self)
{
    self->r.box = Box3_FromSize(32, -24, 64);
    self->r.solid = SOLID_BBOX;
    self->movetype = MOVETYPE_STEP;

    self->health = 300 * st.health_multiplier;
    self->gib_health = -80;
    self->mass = 300;

    self->pain = fiend_pain;
    self->die = fiend_die;

    self->monsterinfo.stand = fiend_stand;
    self->monsterinfo.walk = fiend_walk;
    self->monsterinfo.run = fiend_run;
    self->monsterinfo.checkattack = fiend_checkattack;
    self->monsterinfo.attack = fiend_attack;
    self->monsterinfo.melee = fiend_melee;
    self->monsterinfo.sight = fiend_sight;
    self->monsterinfo.search = fiend_search;
    self->monsterinfo.setskin = fiend_setskin;

    trap_LinkEntity(self);

    M_SetAnimation(self, &fiend_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    walkmonster_start(self);
}

void SP_monster_fiend(edict_t *self)
{
    self->style = Plain;
    self->s.modelindex = G_ModelIndex("models/monsters/fiend/tris.md2");
    SP_monster_fiend_x(self);
}

void SP_monster_fiend_prototype(edict_t *self)
{
    self->style = Proto;
    self->s.modelindex = G_ModelIndex("models/monsters/fiend_prototype/tris.md2");
    SP_monster_fiend_x(self);
}

void SP_monster_fiend_strogg(edict_t *self)
{
    self->style = Strogg;
    self->s.modelindex = G_ModelIndex("models/monsters/fiendstrogg/tris.md2");
    SP_monster_fiend_x(self);
    G_PrecacheGibs(fiend_gibs_strogg);
}
