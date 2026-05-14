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
// m_hknight.c

#include "g_local.h"
#include "m_hellknight.h"

#define SPAWNFLAG_HELLKNIGHT_NOJUMPING 8

#define SOUND   sound[!!self->style]

enum { Plain, Proto, Hyper, Berserker };

static struct {
    int melee;
    int death;
    int sight;
    int search;
    int pain;
    int jump;
} sound[2];

void hellknight_run(edict_t *self);
void hellknight_melee(edict_t *self);

static void SwingSword(edict_t *self)
{
    vec3_t aim = { MELEE_DISTANCE, 0, 24 };
    int damage = (frandom() + frandom() + frandom()) * 3;
    if (!fire_hit(self, aim, damage, 20))
        self->monsterinfo.melee_debounce_time = level.time + SEC(1.2f);

    if (self->touch_debounce_time < level.time) {
        G_StartSound(self, CHAN_WEAPON, sound[self->style == Hyper].melee, 1, ATTN_NORM);
        self->touch_debounce_time = level.time + SEC(0.7f);
    }
}

// Stand
static const mframe_t hellknight_frames_stand[] = {
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
const mmove_t MMOVE_T(hellknight_move_stand) = { FRAME_stand01, FRAME_stand09, hellknight_frames_stand, NULL };

void MONSTERINFO_STAND(hellknight_stand)(edict_t *self)
{
    M_SetAnimation(self, &hellknight_move_stand);
}

// Charge
static const mframe_t hellknight_frames_charge[] = {
    { ai_charge, 20 },
    { ai_charge, 25, monster_footstep },
    { ai_charge, 18 },
    { ai_charge, 16 },
    { ai_charge, 14 },
    { ai_charge, 20, SwingSword },
    { ai_charge, 21, SwingSword },
    { ai_charge, 13, SwingSword },
    { ai_charge, 20, SwingSword },
    { ai_charge, 20, SwingSword },
    { ai_charge, 18, SwingSword },
    { ai_charge, 16 },
    { ai_charge, 20 },
    { ai_charge, 14, monster_footstep },
    { ai_charge, 25 },
    { ai_charge, 21 }
};
const mmove_t MMOVE_T(hellknight_move_charge) = { FRAME_char_a01, FRAME_char_a16, hellknight_frames_charge, hellknight_run };

static bool CheckForCharge(edict_t *self)
{
    if (!self->enemy)
        return false;

    if (!visible(self, self->enemy))
        return false;

    if (level.time < self->monsterinfo.attack_finished)
        return false;

    if (fabsf(self->s.origin.z - self->enemy->s.origin.z) > 20)
        return false;

    if (range_to(self, self->enemy) < 80)
        return false;

    self->monsterinfo.attack_finished = level.time + SEC(2);
    return true;
}

// Run
static const mframe_t hellknight_frames_run[] = {
    { ai_run, 20 },
    { ai_run, 18, monster_footstep },
    { ai_run, 25 },
    { ai_run, 16 },
    { ai_run, 14 },
    { ai_run, 25, monster_footstep },
    { ai_run, 21 },
    { ai_run, 13 }
};
const mmove_t MMOVE_T(hellknight_move_run) = { FRAME_run01, FRAME_run08, hellknight_frames_run, NULL };

void MONSTERINFO_RUN(hellknight_run)(edict_t *self)
{
    if (CheckForCharge(self))
        M_SetAnimation(self, &hellknight_move_charge);
    else
        M_SetAnimation(self, &hellknight_move_run);
}

// walk
static const mframe_t hellknight_frames_walk[] = {
    { ai_walk, 2 },
    { ai_walk, 5 },
    { ai_walk, 5 },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 3 },
    { ai_walk, 3 },
    { ai_walk, 4, monster_footstep },
    { ai_walk, 3 },
    { ai_walk, 4 },
    { ai_walk, 6 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 4 },
    { ai_walk, 3 },
    { ai_walk, 3 },
    { ai_walk, 3 },
    { ai_walk, 2, monster_footstep }
};
const mmove_t MMOVE_T(hellknight_move_walk) = { FRAME_walk01, FRAME_walk20, hellknight_frames_walk, NULL };

void MONSTERINFO_WALK(hellknight_walk)(edict_t *self)
{
    M_SetAnimation(self, &hellknight_move_walk);
}

static void hellknight_reset_magic(edict_t *self)
{
    self->radius_dmg = -2;
    if (self->enemy && range_to(self, self->enemy) < 320 && (frandom() < 0.75f))
        self->monsterinfo.attack_finished = level.time + SEC(1);
}

void TOUCH(magic_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    edict_t *owner = &g_edicts[self->r.ownernum];

    if (other == owner)
        return;

    if (tr->surface_flags & SURF_SKY) {
        G_FreeEdict(self);
        return;
    }

    if (other->takedamage) {
        T_Damage(other, self, owner, self->velocity, self->s.origin, tr->plane.dir, self->dmg, 1, DAMAGE_ENERGY, MOD_UNKNOWN);
        G_FreeEdict(self);
    } else {
        G_BecomeEvent(self, EV_HELLKNIGHT_MAGIC, tr->plane.dir);
    }
}

static void fire_magic(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed)
{
    edict_t *magic;

    magic = G_SpawnMissile(self, start, dir, speed);
    magic->s.effects |= EF_IONRIPPER;
    magic->s.modelindex = G_ModelIndex("models/monsters/spikestrogg/tris.md2");
    magic->touch = magic_touch;
    magic->nextthink = level.time + SEC(10);
    magic->think = G_FreeEdict;
    magic->dmg = damage;
    magic->enemy = self->enemy;
    magic->classname = "magic";
    trap_LinkEntity(magic);

    G_CheckMissileImpact(self, magic);
}

static void FireMagic(edict_t *self)
{
    vec3_t start, forward, right, dir;

    if (!self->enemy || !self->enemy->r.inuse)
        return;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_HELLKNIGHT_MAGIC], forward, right);

    dir = Vec3_Sub(self->enemy->s.origin, start);
    dir.z += self->radius_dmg * 60;
    dir = Vec3_Normalize(dir);
    self->radius_dmg++;

    fire_magic(self, start, dir, 9, 300);
    G_AddEvent(self, EV_MUZZLEFLASH2, MZ2_HELLKNIGHT_MAGIC);
}

static void FireMagic_Hyper(edict_t *self)
{
    vec3_t start, forward, right, dir;

    if (!self->enemy || !self->enemy->r.inuse)
        return;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_HELLKNIGHT_HYPER], forward, right);
    dir = Vec3_Direction(self->enemy->s.origin, start);

    fire_magic(self, start, dir, 9, 300);
    G_AddEvent(self, EV_MUZZLEFLASH2, MZ2_HELLKNIGHT_HYPER);
}

static void FireMagic_Hyper_Start(edict_t *self)
{
    self->fly_sound_debounce_time = level.time + SEC(1.5f);
    FireMagic_Hyper(self);
}

static void FireMagic_Hyper_End(edict_t *self)
{
    FireMagic_Hyper(self);
    if (self->fly_sound_debounce_time > level.time)
        self->s.frame = FRAME_magica10;
    else
        self->monsterinfo.attack_finished = level.time + SEC(2);
}

static void hellknight_attack_slam(edict_t *self)
{
    vec3_t f, r, start;
    AngleVectors(self->s.angles, &f, &r, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_GENERIC_SLAM], f, r);
    trace_t tr = G_TraceLine(self->s.origin, start, self->s.number, MASK_SOLID);

    G_AddEvent(self, EV_MUZZLEFLASH2, MZ2_GENERIC_SLAM);
    self->gravity = 1.0f;
    self->velocity = vec3_origin;
    self->flags |= FL_KILL_VELOCITY;

    T_SlamRadiusDamage(tr.endpos, self, self, 50, 300, self, NULL, 165, MOD_UNKNOWN);
}

void TOUCH(hellknight_jump_touch_slam)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    if (self->health <= 0) {
        self->touch = NULL;
        return;
    }

    if (self->groundentity) {
        self->s.frame = FRAME_magicc11;

        if (self->touch)
            hellknight_attack_slam(self);

        self->touch = NULL;
    }
}

static void hellknight_high_gravity(edict_t *self)
{
    if (self->velocity.z < 0)
        self->gravity = 2.25f * (800.0f / level.gravity);
    else
        self->gravity = 5.25f * (800.0f / level.gravity);
}

static void hellknight_jump_takeoff(edict_t *self)
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
    self->touch = hellknight_jump_touch_slam;
    hellknight_high_gravity(self);
}

static void hellknight_check_landing(edict_t *self)
{
    hellknight_high_gravity(self);

    if (self->groundentity) {
        self->monsterinfo.attack_finished = 0;
        self->monsterinfo.aiflags &= ~AI_DUCKED;
        self->s.frame = FRAME_magicc11;
        if (self->touch) {
            hellknight_attack_slam(self);
            self->touch = NULL;
        }
        self->flags &= ~FL_KILL_VELOCITY;
        return;
    }

    if (level.time > self->monsterinfo.attack_finished)
        self->monsterinfo.nextframe = FRAME_magicc04;
    else
        self->monsterinfo.nextframe = FRAME_magicc06;
}

static const mframe_t hknight_bsk_frames_attack[] = {
    { ai_charge },
    { ai_charge },
    { ai_move, 0, hellknight_jump_takeoff },
    { ai_move, 0, hellknight_high_gravity },
    { ai_move, 0, hellknight_check_landing },
    { ai_move, 0, monster_footstep },
    { ai_move },
    { ai_move, 0, monster_footstep },
    { ai_charge, 0, monster_footstep },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(hknight_bsk_move_attack) = { FRAME_magicc01, FRAME_magicc11, hknight_bsk_frames_attack, hellknight_run };

// Attack
static const mframe_t hellknight_frames_attack[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, hellknight_reset_magic },
    { ai_charge, 0, FireMagic },
    { ai_charge, 0, FireMagic },
    { ai_charge, 0, FireMagic },
    { ai_charge, 0, FireMagic },
    { ai_charge, 0, FireMagic },
    { ai_charge, 0, FireMagic }
};
const mmove_t MMOVE_T(hellknight_move_attack) = { FRAME_magicc01, FRAME_magicc11, hellknight_frames_attack, hellknight_run };

static const mframe_t hknight_hyper_frames_attack[] = {
    { ai_charge, 0, FireMagic_Hyper_Start },
    { ai_charge, 0, FireMagic_Hyper },
    { ai_charge, 0, FireMagic_Hyper },
    { ai_charge, 0, FireMagic_Hyper_End }
};
const mmove_t MMOVE_T(hknight_hyper_move_attack) = { FRAME_magica10, FRAME_magica13, hknight_hyper_frames_attack, hellknight_run };

void MONSTERINFO_ATTACK(hellknight_attack)(edict_t *self)
{
    if (self->monsterinfo.melee_debounce_time <= level.time && (range_to(self, self->enemy) < MELEE_DISTANCE)) {
        hellknight_melee(self);
        return;
    }

    switch (self->style) {
    case Berserker:
        if (!(self->spawnflags & SPAWNFLAG_HELLKNIGHT_NOJUMPING) && (self->timestamp < level.time) && range_to(self, self->enemy) > 50) {
            M_SetAnimation(self, &hknight_bsk_move_attack);
            G_StartSound(self, CHAN_WEAPON, sound[1].jump, 1, ATTN_NORM);
            self->timestamp = level.time + SEC(5);
        }
        break;
    case Hyper:
        M_SetAnimation(self, &hknight_hyper_move_attack);
        break;
    default:
        M_SetAnimation(self, &hellknight_move_attack);
        break;
    }
}

// Slice
static const mframe_t hellknight_frames_slice[] = {
    { ai_charge, 9 },
    { ai_charge, 6 },
    { ai_charge, 13 },
    { ai_charge, 4 },
    { ai_charge, 7, SwingSword },
    { ai_charge, 15, SwingSword },
    { ai_charge, 8, SwingSword },
    { ai_charge, 2, SwingSword },
    { ai_charge, 0, SwingSword },
    { ai_charge, 3 }
};
const mmove_t MMOVE_T(hellknight_move_slice) = { FRAME_slice01, FRAME_slice10, hellknight_frames_slice, hellknight_run };

// Smash
static const mframe_t hellknight_frames_smash[] = {
    { ai_charge, 1 },
    { ai_charge, 13 },
    { ai_charge, 9 },
    { ai_charge, 11 },
    { ai_charge, 10, SwingSword },
    { ai_charge, 7, SwingSword },
    { ai_charge, 12, SwingSword },
    { ai_charge, 2, SwingSword },
    { ai_charge, 3, SwingSword },
    { ai_charge },
    { ai_charge }
};
const mmove_t MMOVE_T(hellknight_move_smash) = { FRAME_smash01, FRAME_smash11, hellknight_frames_smash, hellknight_run };

// Watk
static const mframe_t hellknight_frames_watk[] = {
    { ai_charge, 2 },
    { ai_charge, 0 },
    { ai_charge, 0 },
    { ai_charge, 0, SwingSword },
    { ai_charge, 0, SwingSword },
    { ai_charge, 0, SwingSword },
    { ai_charge, 1 },
    { ai_charge, 4 },
    { ai_charge, 5 },
    { ai_charge, 3, SwingSword },
    { ai_charge, 2, SwingSword },
    { ai_charge, 2, SwingSword },
    { ai_charge, 0 },
    { ai_charge, 0 },
    { ai_charge, 0 },
    { ai_charge, 1 },
    { ai_charge, 1, SwingSword },
    { ai_charge, 3, SwingSword },
    { ai_charge, 4, SwingSword },
    { ai_charge, 6 },
    { ai_charge, 7 },
    { ai_charge, 3 }
};
const mmove_t MMOVE_T(hellknight_move_watk) = { FRAME_w_attack01, FRAME_w_attack22, hellknight_frames_watk, hellknight_run };

// Melee
void MONSTERINFO_MELEE(hellknight_melee)(edict_t *self)
{
    if (self->monsterinfo.melee_debounce_time > level.time)
        return;

    self->dmg_radius++;

    if (self->dmg_radius == 1)
        M_SetAnimation(self, &hellknight_move_slice);
    else if (self->dmg_radius == 2)
        M_SetAnimation(self, &hellknight_move_smash);
    else if (self->dmg_radius == 3) {
        M_SetAnimation(self, &hellknight_move_watk);
        self->dmg_radius = 0;
    }
}

// Pain
static const mframe_t hellknight_frames_pain[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(hellknight_move_pain) = { FRAME_pain01, FRAME_pain05, hellknight_frames_pain, hellknight_run };

void PAIN(hellknight_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    // if we're jumping, don't pain
    if (self->monsterinfo.active_move == &hknight_bsk_move_attack)
        return;
    if (level.time < self->pain_debounce_time)
        return;
    self->pain_debounce_time = level.time + SEC(1);

    G_StartSound(self, CHAN_VOICE, SOUND.pain, 1, ATTN_NORM);

    if (M_ShouldReactToPain(self, mod))
        M_SetAnimation(self, &hellknight_move_pain);
}

static void hellknight_dead(edict_t *self)
{
    self->r.box.maxs.z = -8 * G_EntityScale(self);
    monster_dead(self);
}

static void hellknight_shrink(edict_t *self)
{
    self->r.box.maxs.z = 8 * G_EntityScale(self);
    self->r.svflags |= SVF_DEADMONSTER;
    trap_LinkEntity(self);
}

// Death (1)
static const mframe_t hellknight_frames_die1[] = {
    { ai_move },
    { ai_move, 10 },
    { ai_move, 8 },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, hellknight_shrink },
    { ai_move, 10 },
    { ai_move, 11 },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(hellknight_move_die1) = { FRAME_death01, FRAME_death12, hellknight_frames_die1, hellknight_dead };

// Death (2)
static const mframe_t hellknight_frames_die2[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, hellknight_shrink },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(hellknight_move_die2) = { FRAME_deathb01, FRAME_deathb09, hellknight_frames_die2, hellknight_dead };

static const gib_def_t hellknight_gibs_plain[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/monsters/hellknight/gibs/g_arm.md2", 2 },
    { "models/monsters/hellknight/gibs/g_leg.md2", 2 },
    { "models/monsters/hellknight/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t hellknight_gibs_proto[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/monsters/hellknight_prototype/gibs/g_arm.md2", 2 },
    { "models/monsters/hellknight_prototype/gibs/g_leg.md2", 2 },
    { "models/monsters/hellknight_prototype/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t hellknight_gibs_hyper[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/monsters/hyperknight/gibs/torso.md2", 1 },
    { "models/monsters/hyperknight/gibs/g_arm.md2", 1 },
    { "models/monsters/hyperknight/gibs/g_arm2.md2", 1 },
    { "models/monsters/hyperknight/gibs/g_leg.md2", 1 },
    { "models/monsters/hyperknight/gibs/g_leg2.md2", 1 },
    { "models/monsters/hyperknight/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t hellknight_gibs_bsk[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/monsters/hellberserkerstrogg/gibs/g_arm.md2", 2 },
    { "models/monsters/hellberserkerstrogg/gibs/g_leg.md2", 2 },
    { "models/monsters/hellberserkerstrogg/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

void DIE(hellknight_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    if (self->health <= self->gib_health) {
        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        switch (self->style) {
        case Berserker:
            ThrowGibs(self, damage, hellknight_gibs_bsk);
            break;
        case Hyper:
            ThrowGibs(self, damage, hellknight_gibs_hyper);
            break;
        case Proto:
            ThrowGibs(self, damage, hellknight_gibs_proto);
            break;
        default:
            ThrowGibs(self, damage, hellknight_gibs_plain);
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

    if (brandom())
        M_SetAnimation(self, &hellknight_move_die1);
    else
        M_SetAnimation(self, &hellknight_move_die2);
}

// Sight
void MONSTERINFO_SIGHT(hellknight_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_VOICE, SOUND.sight, 1, ATTN_NORM);
}

// Search
void MONSTERINFO_SEARCH(hellknight_search)(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.search, 1, ATTN_NORM);
}

void MONSTERINFO_SETSKIN(hellknight_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

void PR_monster_hellknight(void)
{
    sound[0].melee = G_SoundIndex("hknight/slash1.wav");
    sound[0].death = G_SoundIndex("hknight/death1.wav");
    sound[0].sight = G_SoundIndex("hknight/sight1.wav");
    sound[0].search = G_SoundIndex("hknight/idle.wav");
    sound[0].pain = G_SoundIndex("hknight/pain1.wav");
}

void PR_monster_hellknight_strogg(void)
{
    sound[0].melee = G_SoundIndex("hknight/slash1.wav");
    sound[1].melee = G_SoundIndex("berserk/attack.wav");
    sound[1].death = G_SoundIndex("hknight/death1_s.wav");
    sound[1].sight = G_SoundIndex("hknight/sight1_s.wav");
    sound[1].search = G_SoundIndex("hknight/idle_s.wav");
    sound[1].pain = G_SoundIndex("hknight/pain1_S.wav");
    sound[1].jump = G_SoundIndex("berserk/jump.wav");
}

static void SP_monster_hellknight_x(edict_t *self)
{
    G_SoundIndex("hknight/attack1.wav");
    G_SoundIndex("hknight/hit.wav");

    self->r.box = Box3_FromSize(16, -24, 40);
    self->r.solid = SOLID_BBOX;
    self->movetype = MOVETYPE_STEP;

    self->health = 250 * st.health_multiplier;
    self->gib_health = -40;
    self->mass = 250;

    self->pain = hellknight_pain;
    self->die = hellknight_die;
    self->monsterinfo.stand = hellknight_stand;
    self->monsterinfo.walk = hellknight_walk;
    self->monsterinfo.run = hellknight_run;
    self->monsterinfo.attack = hellknight_attack;
    self->monsterinfo.melee = hellknight_melee;
    self->monsterinfo.sight = hellknight_sight;
    self->monsterinfo.search = hellknight_search;
    self->monsterinfo.setskin = hellknight_setskin;

    trap_LinkEntity(self);

    M_SetAnimation(self, &hellknight_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    walkmonster_start(self);
}

void SP_monster_hellknight(edict_t *self)
{
    self->style = Plain;
    self->s.modelindex = G_ModelIndex("models/monsters/hellknight/tris.md2");
    SP_monster_hellknight_x(self);
    G_PrecacheGibs(hellknight_gibs_plain);
}

void SP_monster_hknight_prototype(edict_t *self)
{
    self->style = Proto;
    self->s.modelindex = G_ModelIndex("models/monsters/hellknight_prototype/tris.md2");
    SP_monster_hellknight_x(self);
    G_PrecacheGibs(hellknight_gibs_proto);
}

void SP_monster_hknight_hyper(edict_t *self)
{
    self->style = Hyper;
    self->s.modelindex = G_ModelIndex("models/monsters/hyperknight/tris.md2");
    SP_monster_hellknight_x(self);
    G_PrecacheGibs(hellknight_gibs_hyper);
}

void SP_monster_hknight_bsk_strogg(edict_t *self)
{
    self->style = Berserker;
    self->s.modelindex = G_ModelIndex("models/monsters/hellberserkerstrogg/tris.md2");
    SP_monster_hellknight_x(self);
    G_PrecacheGibs(hellknight_gibs_bsk);
}
