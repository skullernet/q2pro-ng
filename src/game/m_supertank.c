// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

SUPERTANK

==============================================================================
*/

#include "g_local.h"
#include "m_supertank.h"

#define SPAWNFLAG_SUPERTANK_POWERSHIELD 8
#define SPAWNFLAG_SUPERTANK_LONG_DEATH  16 // n64

static int sound_pain1;
static int sound_pain2;
static int sound_pain3;
static int sound_death;
static int sound_search1;
static int sound_search2;
static int sound_tread;
static int sound_melee;

static void TreadSound(edict_t *self)
{
    G_StartSound(self, CHAN_BODY, sound_tread, 1, ATTN_NORM);
}

void MONSTERINFO_SEARCH(supertank_search)(edict_t *self)
{
    if (brandom())
        G_StartSound(self, CHAN_VOICE, sound_search1, 1, ATTN_NORM);
    else
        G_StartSound(self, CHAN_VOICE, sound_search2, 1, ATTN_NORM);
}

static void supertank_dead(edict_t *self);
static void supertankRocket(edict_t *self);
static void supertankMachineGun(edict_t *self);
static void supertank_reattack1(edict_t *self);

//
// stand
//

static const mframe_t supertank_frames_stand[] = {
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
const mmove_t MMOVE_T(supertank_move_stand) = { FRAME_stand_1, FRAME_stand_60, supertank_frames_stand, NULL };

void MONSTERINFO_STAND(supertank_stand)(edict_t *self)
{
    M_SetAnimation(self, &supertank_move_stand);
}

static const mframe_t supertank_frames_run[] = {
    { ai_run, 12, TreadSound },
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
const mmove_t MMOVE_T(supertank_move_run) = { FRAME_forwrd_1, FRAME_forwrd_18, supertank_frames_run, NULL };

//
// walk
//

static const mframe_t supertank_frames_forward[] = {
    { ai_walk, 4, TreadSound },
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
const mmove_t MMOVE_T(supertank_move_forward) = { FRAME_forwrd_1, FRAME_forwrd_18, supertank_frames_forward, NULL };

void MONSTERINFO_WALK(supertank_walk)(edict_t *self)
{
    M_SetAnimation(self, &supertank_move_forward);
}

void MONSTERINFO_RUN(supertank_run)(edict_t *self)
{
    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
        M_SetAnimation(self, &supertank_move_stand);
    else
        M_SetAnimation(self, &supertank_move_run);
}

#if 0
static const mframe_t supertank_frames_turn_right[] = {
    { ai_move, 0, TreadSound },
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
const mmove_t MMOVE_T(supertank_move_turn_right) = { FRAME_right_1, FRAME_right_18, supertank_frames_turn_right, supertank_run };

static const mframe_t supertank_frames_turn_left[] = {
    { ai_move, 0, TreadSound },
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
const mmove_t MMOVE_T(supertank_move_turn_left) = { FRAME_left_1, FRAME_left_18, supertank_frames_turn_left, supertank_run };
#endif

static const mframe_t supertank_frames_pain3[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(supertank_move_pain3) = { FRAME_pain3_9, FRAME_pain3_12, supertank_frames_pain3, supertank_run };

static const mframe_t supertank_frames_pain2[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(supertank_move_pain2) = { FRAME_pain2_5, FRAME_pain2_8, supertank_frames_pain2, supertank_run };

static const mframe_t supertank_frames_pain1[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(supertank_move_pain1) = { FRAME_pain1_1, FRAME_pain1_4, supertank_frames_pain1, supertank_run };

static void BossLoop(edict_t *self)
{
    if (!(self->spawnflags & SPAWNFLAG_SUPERTANK_LONG_DEATH))
        return;

    if (self->count)
        self->count--;
    else
        self->spawnflags &= ~SPAWNFLAG_SUPERTANK_LONG_DEATH;

    self->monsterinfo.nextframe = FRAME_death_19;
}

static void supertankGrenade(edict_t *self)
{
    vec3_t                   forward, right;
    vec3_t                   start;
    monster_muzzleflash_id_t flash_number;

    if (!self->enemy || !self->enemy->r.inuse) // PGM
        return;                              // PGM

    if (self->s.frame == FRAME_attak4_1)
        flash_number = MZ2_SUPERTANK_GRENADE_1;
    else
        flash_number = MZ2_SUPERTANK_GRENADE_2;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

    vec3_t aim_point;
    M_PredictAim(self, self->enemy, start, 0, false, crandom_open() * 0.1f, &forward, &aim_point);

    for (int i = 0; i < 5; i++) {
        float speed = 500 + i * 100;

        if (!M_CalculatePitchToFire(self, aim_point, start, &forward, speed, 2.5f, true, false))
            continue;

        if (self->style)
            monster_fire_tesla(self, start, forward, 1, speed, flash_number);
        else
            monster_fire_grenade(self, start, forward, 50, speed, flash_number, 0, 0);
        break;
    }
}

static const mframe_t supertank_frames_death1[] = {
    { ai_move, 0, BossExplode },
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
    { ai_move, 0, BossLoop }
};
const mmove_t MMOVE_T(supertank_move_death) = { FRAME_death_1, FRAME_death_24, supertank_frames_death1, supertank_dead };

static const mframe_t supertank_frames_attack4[] = {
    { ai_move, 0, supertankGrenade },
    { ai_move },
    { ai_move },
    { ai_move, 0, supertankGrenade },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(supertank_move_attack4) = { FRAME_attak4_1, FRAME_attak4_6, supertank_frames_attack4, supertank_run };

static const mframe_t supertank_frames_attack2[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, supertankRocket },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, supertankRocket },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, supertankRocket },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(supertank_move_attack2) = { FRAME_attak2_1, FRAME_attak2_27, supertank_frames_attack2, supertank_run };

static const mframe_t supertank_frames_attack1[] = {
    { ai_charge, 0, supertankMachineGun },
    { ai_charge, 0, supertankMachineGun },
    { ai_charge, 0, supertankMachineGun },
    { ai_charge, 0, supertankMachineGun },
    { ai_charge, 0, supertankMachineGun },
    { ai_charge, 0, supertankMachineGun },
};
const mmove_t MMOVE_T(supertank_move_attack1) = { FRAME_attak1_1, FRAME_attak1_6, supertank_frames_attack1, supertank_reattack1 };

static const mframe_t supertank_frames_end_attack1[] = {
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
const mmove_t MMOVE_T(supertank_move_end_attack1) = { FRAME_attak1_7, FRAME_attak1_20, supertank_frames_end_attack1, supertank_run };

static void supertank_reattack1(edict_t *self)
{
    if (visible(self, self->enemy)) {
        if (self->timestamp >= level.time || frandom() < 0.3f)
            M_SetAnimation(self, &supertank_move_attack1);
        else
            M_SetAnimation(self, &supertank_move_end_attack1);
    } else
        M_SetAnimation(self, &supertank_move_end_attack1);
}

void PAIN(supertank_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (level.time < self->pain_debounce_time)
        return;

    // Lessen the chance of him going into his pain frames
    if (mod != MOD_CHAINFIST) {
        if (damage <= 25)
            if (frandom() < 0.2f)
                return;

        // Don't go into pain if he's firing his rockets
        if ((self->s.frame >= FRAME_attak2_1) && (self->s.frame <= FRAME_attak2_14))
            return;
    }

    if (damage <= 10)
        G_StartSound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM);
    else if (damage <= 25)
        G_StartSound(self, CHAN_VOICE, sound_pain3, 1, ATTN_NORM);
    else
        G_StartSound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM);

    self->pain_debounce_time = level.time + SEC(3);

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    if (damage <= 10)
        M_SetAnimation(self, &supertank_move_pain1);
    else if (damage <= 25)
        M_SetAnimation(self, &supertank_move_pain2);
    else
        M_SetAnimation(self, &supertank_move_pain3);
}

void MONSTERINFO_SETSKIN(supertank_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum |= 1;
    else
        self->s.skinnum &= ~1;
}

static void supertankRocket(edict_t *self)
{
    vec3_t                   forward, right;
    vec3_t                   start;
    vec3_t                   dir;
    vec3_t                   vec;
    monster_muzzleflash_id_t flash_number;

    if (!self->enemy || !self->enemy->r.inuse) // PGM
        return;                              // PGM

    if (self->s.frame == FRAME_attak2_8)
        flash_number = MZ2_SUPERTANK_ROCKET_1;
    else if (self->s.frame == FRAME_attak2_11)
        flash_number = MZ2_SUPERTANK_ROCKET_2;
    else // (self->s.frame == FRAME_attak2_14)
        flash_number = MZ2_SUPERTANK_ROCKET_3;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

    if (self->spawnflags & SPAWNFLAG_SUPERTANK_POWERSHIELD) {
        vec = self->enemy->s.origin;
        vec.z += self->enemy->viewheight;
        dir = Vec3_Direction(vec, start);
        monster_fire_heat(self, start, dir, 40, 500, flash_number, 0.075f);
    } else {
        M_PredictAim(self, self->enemy, start, 750, false, 0, &forward, NULL);
        monster_fire_rocket(self, start, forward, 50, 750, flash_number);
    }
}

static void supertank_fire_plasma(edict_t *self)
{
    vec3_t                   start, aim;
    vec3_t                   forward, right;
    monster_muzzleflash_id_t flash_number;

    flash_number = MZ2_SUPERSHAMBLERTANK_PLASMA_1 + (self->s.frame - FRAME_attak1_1);

    int damage = 35;
    int radius_damage = 45;

    if (self->s.frame > FRAME_attak1_3) {
        damage /= 2;
        radius_damage /= 2;
    }

    AngleVectors(Vec3(0, self->s.angles.yaw, 0), &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

    M_PredictAim(self, self->enemy, start, 725, false, frandom() * 0.3f, &aim, NULL);
    for (int i = 0; i < 3; i++)
        aim.xyz[i] += crandom_open() * 0.025f;
    monster_fire_plasma(self, start, aim, damage, 725, flash_number, radius_damage, radius_damage);
}

static void supertank_fire_bullet(edict_t *self)
{
    vec3_t                   start;
    vec3_t                   forward, right;
    monster_muzzleflash_id_t flash_number;

    flash_number = MZ2_SUPERTANK_MACHINEGUN_1 + (self->s.frame - FRAME_attak1_1);

    AngleVectors(Vec3(0, self->s.angles.yaw, 0), &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

    M_PredictAim(self, self->enemy, start, 0, true, -0.1f, &forward, NULL);
    monster_fire_bullet(self, start, forward, 6, 4, DEFAULT_BULLET_HSPREAD * 3, DEFAULT_BULLET_VSPREAD * 3, flash_number);
}

static void supertankMachineGun(edict_t *self)
{
    if (!self->enemy || !self->enemy->r.inuse)
        return;

    if (self->s.frame - FRAME_attak1_1 >= 6)
        return;

    if (self->style)
        supertank_fire_plasma(self);
    else
        supertank_fire_bullet(self);
}

static void StroggTankChainsaw(edict_t *self)
{
    if (!self->enemy || !self->enemy->r.inuse)
        return;
    vec3_t aim = { 100, 0, -24 };
    int damage = (frandom() + frandom() + frandom()) * 4;
    fire_hit(self, aim, damage, damage);
}

// Swing
static const mframe_t supershamblertank_frames_melee[] = {
    { ai_charge, 11 },
    { ai_charge, 1 },
    { ai_charge, 4 },
    { ai_charge, 19, StroggTankChainsaw },
    { ai_charge, 13, StroggTankChainsaw },
    { ai_charge, 10, StroggTankChainsaw },
    { ai_charge, 10, StroggTankChainsaw },
    { ai_charge, 10, StroggTankChainsaw },
    { ai_charge, 10, StroggTankChainsaw },
    { ai_charge, 10, StroggTankChainsaw },
    { ai_charge, 3 },
    { ai_charge, 8 },
    { ai_charge, 9 },
    { ai_charge, 0 }
};
const mmove_t MMOVE_T(supershamblertank_move_melee) = { FRAME_forwrd_1, FRAME_forwrd_14, supershamblertank_frames_melee, supertank_run };

// Melee
void MONSTERINFO_MELEE(supershamblertank_melee)(edict_t *self)
{
    M_SetAnimation(self, &supershamblertank_move_melee);
    G_StartSound(self, CHAN_WEAPON, sound_melee, 1, ATTN_NORM);
}

void MONSTERINFO_ATTACK(supertank_attack)(edict_t *self)
{
    vec3_t vec;
    float  range;

    vec = Vec3_Sub(self->enemy->s.origin, self->s.origin);
    range = range_to(self, self->enemy);

    // Attack 1 == Chaingun
    // Attack 2 == Rocket Launcher
    // Attack 3 == Grenade Launcher
    bool chaingun_good = M_CheckClearShot(self, monster_flash_offset[MZ2_SUPERTANK_MACHINEGUN_1]);
    bool rocket_good = M_CheckClearShot(self, monster_flash_offset[MZ2_SUPERTANK_ROCKET_1]);
    bool grenade_good = M_CheckClearShot(self, monster_flash_offset[MZ2_SUPERTANK_GRENADE_1]);

    // fire rockets more often at distance
    if (chaingun_good && (!rocket_good || range <= 540 || frandom() < 0.3f)) {
        // prefer grenade if the enemy is above us
        if (grenade_good && (range >= 350 || vec.z > 120 || frandom() < 0.2f))
            M_SetAnimation(self, &supertank_move_attack4);
        else {
            M_SetAnimation(self, &supertank_move_attack1);
            self->timestamp = level.time + random_time_sec(1.5f, 2.7f);
        }
    } else if (rocket_good) {
        // prefer grenade if the enemy is above us
        if (grenade_good && (vec.z > 120 || frandom() < 0.2f))
            M_SetAnimation(self, &supertank_move_attack4);
        else
            M_SetAnimation(self, &supertank_move_attack2);
    } else if (grenade_good)
        M_SetAnimation(self, &supertank_move_attack4);
}

//
// death
//

static const gib_def_t supertank_gibs[] = {
    { "models/objects/gibs/sm_meat/tris.md2", 2 },
    { "models/objects/gibs/sm_metal/tris.md2", 2, GIB_METALLIC },
    { "models/monsters/boss1/gibs/cgun.md2", 1, GIB_SKINNED | GIB_METALLIC },
    { "models/monsters/boss1/gibs/chest.md2", 1, GIB_SKINNED },
    { "models/monsters/boss1/gibs/core.md2", 1, GIB_SKINNED },
    { "models/monsters/boss1/gibs/ltread.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/boss1/gibs/rgun.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/boss1/gibs/rtread.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/boss1/gibs/tube.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/boss1/gibs/head.md2", 1, GIB_SKINNED | GIB_METALLIC | GIB_HEAD },
    { 0 }
};

static const gib_def_t supershamblertank_gibs[] = {
    { "models/objects/gibs/sm_meat/tris.md2", 2 },
    { "models/objects/gibs/sm_metal/tris.md2", 2, GIB_METALLIC },
    { "models/monsters/boss1/gibs/cgun.md2", 1, GIB_SKINNED | GIB_METALLIC },
    { "models/monsters/boss1/gibs/chest.md2", 1, GIB_SKINNED },
    { "models/monsters/boss1/gibs/core.md2", 1, GIB_SKINNED },
    { "models/monsters/boss1/gibs/ltread.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/boss1/gibs/rgun.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/boss1/gibs/tube.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/shamblertank/gibs/g_arm.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/shamblertank/gibs/g_leg.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/shamblertank/gibs/g_head.md2", 1, GIB_SKINNED | GIB_METALLIC | GIB_HEAD },
    { 0 }
};

static void supertank_gib(edict_t *self)
{
    G_AddEvent(self, EV_EXPLOSION1_BIG, 0);

    self->s.sound = 0;
    self->s.skinnum /= 2;

    if (self->style)
        ThrowGibs(self, 500, supershamblertank_gibs);
    else
        ThrowGibs(self, 500, supertank_gibs);
}

static void supertank_dead(edict_t *self)
{
    // no blowy on deady
    if (self->spawnflags & SPAWNFLAG_MONSTER_DEAD) {
        self->deadflag = false;
        self->takedamage = true;
        return;
    }

    supertank_gib(self);
}

void DIE(supertank_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    if (self->spawnflags & SPAWNFLAG_MONSTER_DEAD){
        // check for gib
        if (M_CheckGib(self, mod)) {
            supertank_gib(self);
            self->deadflag = true;
            return;
        }

        if (self->deadflag)
            return;
    } else {
        G_StartSound(self, CHAN_VOICE, sound_death, 1, ATTN_NORM);
        self->deadflag = true;
        self->takedamage = false;
    }

    M_SetAnimation(self, &supertank_move_death);
}

//===========
// PGM
bool MONSTERINFO_BLOCKED(supertank_blocked)(edict_t *self, float dist)
{
    if (blocked_checkplat(self, dist))
        return true;

    return false;
}
// PGM
//===========

//
// monster_supertank
//

void PR_monster_supertank(void)
{
    sound_pain1 = G_SoundIndex("bosstank/btkpain1.wav");
    sound_pain2 = G_SoundIndex("bosstank/btkpain2.wav");
    sound_pain3 = G_SoundIndex("bosstank/btkpain3.wav");
    sound_death = G_SoundIndex("bosstank/btkdeth1.wav");
    sound_search1 = G_SoundIndex("bosstank/btkunqv1.wav");
    sound_search2 = G_SoundIndex("bosstank/btkunqv2.wav");
    sound_tread = G_SoundIndex("bosstank/btkengn1.wav");
}

void PR_monster_supershamblertank(void)
{
    PR_monster_supertank();
    sound_melee = G_SoundIndex("ogre/ogsawatk.wav");
}

// RAFAEL (Powershield)

static void SP_monster_supertank_x(edict_t *self)
{
    G_SoundIndex("gunner/gunatck3.wav");
    G_SoundIndex("infantry/infatck1.wav");
    G_SoundIndex("tank/rocket.wav");

    self->movetype = MOVETYPE_STEP;
    self->r.solid = SOLID_BBOX;

    self->r.box = Box3_FromSize(64, 0, 112);

    self->health *= st.health_multiplier;
    self->gib_health = -500;
    self->mass = 800;

    self->pain = supertank_pain;
    self->die = supertank_die;
    self->monsterinfo.stand = supertank_stand;
    self->monsterinfo.walk = supertank_walk;
    self->monsterinfo.run = supertank_run;
    self->monsterinfo.attack = supertank_attack;
    self->monsterinfo.search = supertank_search;
    self->monsterinfo.blocked = supertank_blocked; // PGM
    self->monsterinfo.setskin = supertank_setskin;

    M_SetAnimation(self, &supertank_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    // RAFAEL
    if (self->spawnflags & SPAWNFLAG_SUPERTANK_POWERSHIELD) {
        if (!ED_WasKeySpecified("power_armor_type"))
            self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
        if (!ED_WasKeySpecified("power_armor_power"))
            self->monsterinfo.power_armor_power = 400;
    }
    // RAFAEL

    // PMM
    self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
    // pmm

    // TODO
    if (level.is_n64) {
        self->spawnflags |= SPAWNFLAG_SUPERTANK_LONG_DEATH;
        self->count = 10;
    }

    walkmonster_start(self);
}

//
// monster_boss5
// RAFAEL
//

/*QUAKED monster_supertank (1 .5 0) (-64 -64 0) (64 64 72) Ambush Trigger_Spawn Sight Powershield LongDeath
 */
void SP_monster_supertank(edict_t *self)
{
    self->style = 0;
    self->health = 1500;
    self->s.modelindex = G_ModelIndex("models/monsters/boss1/tris.md2");

    SP_monster_supertank_x(self);
    G_PrecacheGibs(supertank_gibs);
}

/*QUAKED monster_boss5 (1 .5 0) (-64 -64 0) (64 64 72) Ambush Trigger_Spawn Sight
 */
void SP_monster_boss5(edict_t *self)
{
    self->style = 0;
    self->health = 1500;
    self->spawnflags |= SPAWNFLAG_SUPERTANK_POWERSHIELD;
    self->s.skinnum = 2;
    self->s.modelindex = G_ModelIndex("models/monsters/boss1/tris.md2");

    SP_monster_supertank_x(self);

    G_PrecacheGibs(supertank_gibs);
    G_SoundIndex("weapons/railgr1a.wav");
}

/*QUAKED monster_supershamblertank (1 .5 0) (-64 -64 0) (64 64 72) Ambush Trigger_Spawn Sight Powershield LongDeath
 */
void SP_monster_supershamblertank(edict_t *self)
{
    self->style = 1;
    self->health = 3000;
    self->s.modelindex = G_ModelIndex("models/monsters/shamblertank/tris.md2");
    self->monsterinfo.melee = supershamblertank_melee;

    SP_monster_supertank_x(self);

    G_PrecacheGibs(supershamblertank_gibs);
    G_SoundIndex("weapons/plasshot.wav");
}
