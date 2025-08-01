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

    AngleVectors(self->s.angles, forward, right, NULL);
    M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right, start);

    vec3_t aim_point;
    PredictAim(self, self->enemy, start, 0, false, crandom_open() * 0.1f, forward, aim_point);

    for (int i = 0; i < 5; i++) {
        float speed = 500 + i * 100;

        if (!M_CalculatePitchToFire(self, aim_point, start, forward, speed, 2.5f, true, false))
            continue;

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
    if (mod.id != MOD_CHAINFIST) {
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

    AngleVectors(self->s.angles, forward, right, NULL);
    M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right, start);

    if (self->spawnflags & SPAWNFLAG_SUPERTANK_POWERSHIELD) {
        VectorCopy(self->enemy->s.origin, vec);
        vec[2] += self->enemy->viewheight;
        VectorSubtract(vec, start, dir);
        VectorNormalize(dir);
        monster_fire_heat(self, start, dir, 40, 500, flash_number, 0.075f);
    } else {
        PredictAim(self, self->enemy, start, 750, false, 0, forward, NULL);
        monster_fire_rocket(self, start, forward, 50, 750, flash_number);
    }
}

static void supertankMachineGun(edict_t *self)
{
    vec3_t                   dir;
    vec3_t                   start;
    vec3_t                   forward, right;
    monster_muzzleflash_id_t flash_number;

    if (!self->enemy || !self->enemy->r.inuse) // PGM
        return;                              // PGM

    flash_number = MZ2_SUPERTANK_MACHINEGUN_1 + (self->s.frame - FRAME_attak1_1);

    dir[0] = 0;
    dir[1] = self->s.angles[1];
    dir[2] = 0;

    AngleVectors(dir, forward, right, NULL);
    M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right, start);
    PredictAim(self, self->enemy, start, 0, true, -0.1f, forward, NULL);
    monster_fire_bullet(self, start, forward, 6, 4, DEFAULT_BULLET_HSPREAD * 3, DEFAULT_BULLET_VSPREAD * 3, flash_number);
}

void MONSTERINFO_ATTACK(supertank_attack)(edict_t *self)
{
    vec3_t vec;
    float  range;

    VectorSubtract(self->enemy->s.origin, self->s.origin, vec);
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
        if (grenade_good && (range >= 350 || vec[2] > 120 || frandom() < 0.2f))
            M_SetAnimation(self, &supertank_move_attack4);
        else {
            M_SetAnimation(self, &supertank_move_attack1);
            self->timestamp = level.time + random_time_sec(1.5f, 2.7f);
        }
    } else if (rocket_good) {
        // prefer grenade if the enemy is above us
        if (grenade_good && (vec[2] > 120 || frandom() < 0.2f))
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

static void supertank_gib(edict_t *self)
{
    G_AddEvent(self, EV_EXPLOSION1_BIG, 0);

    self->s.sound = 0;
    self->s.skinnum /= 2;

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

void DIE(supertank_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t point, mod_t mod)
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

static void supertank_precache(void)
{
    sound_pain1 = G_SoundIndex("bosstank/btkpain1.wav");
    sound_pain2 = G_SoundIndex("bosstank/btkpain2.wav");
    sound_pain3 = G_SoundIndex("bosstank/btkpain3.wav");
    sound_death = G_SoundIndex("bosstank/btkdeth1.wav");
    sound_search1 = G_SoundIndex("bosstank/btkunqv1.wav");
    sound_search2 = G_SoundIndex("bosstank/btkunqv2.wav");
    sound_tread = G_SoundIndex("bosstank/btkengn1.wav");
}

// RAFAEL (Powershield)

/*QUAKED monster_supertank (1 .5 0) (-64 -64 0) (64 64 72) Ambush Trigger_Spawn Sight Powershield LongDeath
 */
void SP_monster_supertank(edict_t *self)
{
    if (!M_AllowSpawn(self)) {
        G_FreeEdict(self);
        return;
    }

    G_AddPrecache(supertank_precache);

    G_SoundIndex("gunner/gunatck3.wav");
    G_SoundIndex("infantry/infatck1.wav");
    G_SoundIndex("tank/rocket.wav");

    self->movetype = MOVETYPE_STEP;
    self->r.solid = SOLID_BBOX;
    self->s.modelindex = G_ModelIndex("models/monsters/boss1/tris.md2");

    PrecacheGibs(supertank_gibs);

    VectorSet(self->r.mins, -64, -64, 0);
    VectorSet(self->r.maxs, 64, 64, 112);

    self->health = 1500 * st.health_multiplier;
    self->gib_health = -500;
    self->mass = 800;

    self->pain = supertank_pain;
    self->die = supertank_die;
    self->monsterinfo.stand = supertank_stand;
    self->monsterinfo.walk = supertank_walk;
    self->monsterinfo.run = supertank_run;
    self->monsterinfo.dodge = NULL;
    self->monsterinfo.attack = supertank_attack;
    self->monsterinfo.search = supertank_search;
    self->monsterinfo.melee = NULL;
    self->monsterinfo.sight = NULL;
    self->monsterinfo.blocked = supertank_blocked; // PGM
    self->monsterinfo.setskin = supertank_setskin;

    trap_LinkEntity(self);

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

    walkmonster_start(self);

    // PMM
    self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
    // pmm

    // TODO
    if (level.is_n64) {
        self->spawnflags |= SPAWNFLAG_SUPERTANK_LONG_DEATH;
        self->count = 10;
    }
}

//
// monster_boss5
// RAFAEL
//

/*QUAKED monster_boss5 (1 .5 0) (-64 -64 0) (64 64 72) Ambush Trigger_Spawn Sight
 */
void SP_monster_boss5(edict_t *self)
{
    self->spawnflags |= SPAWNFLAG_SUPERTANK_POWERSHIELD;
    SP_monster_supertank(self);
    G_SoundIndex("weapons/railgr1a.wav");
    self->s.skinnum = 2;
}
