// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

chtjor

==============================================================================
*/

#include "g_local.h"
#include "m_chtjor.h"

static int sound_pain1;
static int sound_pain2;
static int sound_pain3;
static int sound_idle;
static int sound_death;
static int sound_search1;
static int sound_search2;
static int sound_search3;
static int sound_attack1, sound_attack1_loop, sound_attack1_end;
static int sound_attack2;
static int sound_firegun;
static int sound_step_left;
static int sound_step_right;
static int sound_death_hit;

static void fire_rocket_chtjor(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, monster_muzzleflash_id_t flashtype)
{
    fire_rocket(self, start, dir, damage, speed, damage + 20, damage);
    if (brandom())
        G_AddEvent(self, EV_MUZZLEFLASH2, flashtype);
}

static void fire_rocket_shamjorg(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, monster_muzzleflash_id_t flashtype)
{
    fire_plasma(self, start, dir, damage, speed, 45, 45);
    if (brandom())
        G_AddEvent(self, EV_MUZZLEFLASH2, flashtype);
}

static void chtjor_attack1_end_sound(edict_t *self)
{
    if (self->monsterinfo.weapon_sound) {
        G_StartSound(self, CHAN_WEAPON, sound_attack1_end, 1, ATTN_NORM);
        self->monsterinfo.weapon_sound = 0;
    }
}

void MONSTERINFO_SEARCH(chtjor_search)(edict_t *self)
{
    float r = frandom();

    if (r <= 0.3f)
        G_StartSound(self, CHAN_VOICE, sound_search1, 1, ATTN_NORM);
    else if (r <= 0.6f)
        G_StartSound(self, CHAN_VOICE, sound_search2, 1, ATTN_NORM);
    else
        G_StartSound(self, CHAN_VOICE, sound_search3, 1, ATTN_NORM);
}

static void chtjor_dead(edict_t *self);
static void chtjorBFG(edict_t *self);
static void chtjor_firebullet(edict_t *self);
static void chtjor_firebullet_left(edict_t *self);
static void chtjor_firebullet_right(edict_t *self);
static void chtjor_reattack1(edict_t *self);
static void chtjor_attack1(edict_t *self);
static void chtjor_idle(edict_t *self);
static void chtjor_step_left(edict_t *self);
static void chtjor_step_right(edict_t *self);
static void chtjor_death_hit(edict_t *self);

//
// stand
//
static const mframe_t chtjor_frames_stand[] = {
    { ai_stand, 0, chtjor_idle },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand }, // 10
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand }, // 20
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand }, // 30
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand, 19 },
    { ai_stand, 11, chtjor_step_left },
    { ai_stand },
    { ai_stand },
    { ai_stand, 6 },
    { ai_stand, 9, chtjor_step_right },
    { ai_stand }, // 40
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand, -2, NULL },
    { ai_stand, -17, chtjor_step_left },
    { ai_stand },
    { ai_stand, -12 },                 // 50
    { ai_stand, -14, chtjor_step_right } // 51
};
const mmove_t MMOVE_T(chtjor_move_stand) = { FRAME_stand01, FRAME_stand51, chtjor_frames_stand, NULL };

static void chtjor_idle(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, sound_idle, 1, ATTN_NORM);
}

static void chtjor_death_hit(edict_t *self)
{
    G_StartSound(self, CHAN_BODY, sound_death_hit, 1, ATTN_NORM);
}

static void chtjor_step_left(edict_t *self)
{
    G_StartSound(self, CHAN_BODY, sound_step_left, 1, ATTN_NORM);
}

static void chtjor_step_right(edict_t *self)
{
    G_StartSound(self, CHAN_BODY, sound_step_right, 1, ATTN_NORM);
}

void MONSTERINFO_STAND(chtjor_stand)(edict_t *self)
{
    M_SetAnimation(self, &chtjor_move_stand);

    chtjor_attack1_end_sound(self);
}

#if 0
void chtjorstep(edict_t *self)
{
    uint32_t i;
    edict_t *e;

    for (i = 1; i <= MAX_CLIENTS; i++) {
        e = &g_edicts[i];
        if (e->inuse && e->client) {
            float r = range_to(self, e);
            if (r <= RANGE_MELEE){
                r = 1.0;
            }else {
                r = r * 0.0025;
            }

            vec3_t& angles = e->client->ps.kick_angles;
            e->client->v_dmg_pitch = (-100 * 0.10f) / r;
            e->client->v_dmg_roll = 0.10 / r;
            e->client->v_dmg_time = level.time + 0.1_sec;
            e->client->quake_time = (level.time + 100_ms) / r;

            float factor = min(1.0f, (e->client->quake_time.seconds() / level.time.seconds()) * 0.25f);
            factor = factor / r;
            angles.x += crandom_open() * factor * 100;
            angles.z += crandom_open() * factor * 100;
            angles.y += crandom_open() * factor * 100;
        }
    }
}
#endif

static const mframe_t chtjor_frames_run[] = {
    { ai_run, 17, chtjor_step_left },
    { ai_run, 0},
    { ai_run },
    { ai_run, 0},
    { ai_run, 12 },
    { ai_run, 8 },
    { ai_run, 10 },
    { ai_run, 33, chtjor_step_right },
    { ai_run, 0},
    { ai_run },
    { ai_run, 0},
    { ai_run, 9 },
    { ai_run, 9 },
    { ai_run, 9 }
};
const mmove_t MMOVE_T(chtjor_move_run) = { FRAME_walk06, FRAME_walk19, chtjor_frames_run, NULL };

//
// walk
//
#if 0
static const mframe_t chtjor_frames_start_walk[] = {
    { ai_walk, 5 },
    { ai_walk, 6 },
    { ai_walk, 7 },
    { ai_walk, 9 },
    { ai_walk, 15 }
};
const mmove_t MMOVE_T(chtjor_move_start_walk) = { FRAME_walk01, FRAME_walk05, chtjor_frames_start_walk, NULL };
#endif

static const mframe_t chtjor_frames_walk[] = {
    { ai_walk, 17 },
    { ai_walk, 0 },
    { ai_walk },
    { ai_walk, 0 },
    { ai_walk, 12 },
    { ai_walk, 8 },
    { ai_walk, 10 },
    { ai_walk, 33 },
    { ai_walk, 0 },
    { ai_walk },
    { ai_walk, 0 },
    { ai_walk, 9 },
    { ai_walk, 9 },
    { ai_walk, 9 }
};
const mmove_t MMOVE_T(chtjor_move_walk) = { FRAME_walk06, FRAME_walk19, chtjor_frames_walk, NULL };

#if 0
static const mframe_t chtjor_frames_end_walk[] = {
    { ai_walk, 11 },
    { ai_walk },
    { ai_walk },
    { ai_walk },
    { ai_walk, 8 },
    { ai_walk, -8 }
};
const mmove_t MMOVE_T(chtjor_move_end_walk) = { FRAME_walk20, FRAME_walk25, chtjor_frames_end_walk, NULL };
#endif

void MONSTERINFO_WALK(chtjor_walk)(edict_t *self)
{
    M_SetAnimation(self, &chtjor_move_walk);
}

void MONSTERINFO_RUN(chtjor_run)(edict_t *self)
{
    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
        M_SetAnimation(self, &chtjor_move_stand);
    else
        M_SetAnimation(self, &chtjor_move_run);

    chtjor_attack1_end_sound(self);
}

static const mframe_t chtjor_frames_pain3[] = {
    { ai_move, -28 },
    { ai_move, -6 },
    { ai_move, -3, chtjor_step_left },
    { ai_move, -9 },
    { ai_move, 0, chtjor_step_right },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, -7 },
    { ai_move, 1 },
    { ai_move, -11 },
    { ai_move, -4 },
    { ai_move },
    { ai_move },
    { ai_move, 10 },
    { ai_move, 11 },
    { ai_move },
    { ai_move, 10 },
    { ai_move, 3 },
    { ai_move, 10 },
    { ai_move, 7, chtjor_step_left },
    { ai_move, 17 },
    { ai_move },
    { ai_move },
    { ai_move, 0, chtjor_step_right }
};
const mmove_t MMOVE_T(chtjor_move_pain3) = { FRAME_pain301, FRAME_pain325, chtjor_frames_pain3, chtjor_run };

static const mframe_t chtjor_frames_pain2[] = {
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(chtjor_move_pain2) = { FRAME_pain201, FRAME_pain203, chtjor_frames_pain2, chtjor_run };

static const mframe_t chtjor_frames_pain1[] = {
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(chtjor_move_pain1) = { FRAME_pain101, FRAME_pain103, chtjor_frames_pain1, chtjor_run };

static const mframe_t chtjor_frames_death1[] = {
    { ai_move, 0, BossExplode },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, -2 },
    { ai_move, -5 },
    { ai_move, -8 },
    { ai_move, -15, chtjor_step_left },
    { ai_move }, // 10
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, -11 },
    { ai_move, -25 },
    { ai_move, -10, chtjor_step_right },
    { ai_move },
    { ai_move }, // 20
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, -21 },
    { ai_move, -10 },
    { ai_move, -16, chtjor_step_left },
    { ai_move },
    { ai_move },
    { ai_move }, // 30
    { ai_move },
    { ai_move },
    { ai_move, 22 },
    { ai_move, 33, chtjor_step_left },
    { ai_move },
    { ai_move },
    { ai_move, 28 },
    { ai_move, 28, chtjor_step_right },
    { ai_move },
    { ai_move }, // 40
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, -19 },
    { ai_move, 0, chtjor_death_hit },
    { ai_move },
    { ai_move } // 50
};
const mmove_t MMOVE_T(chtjor_move_death) = { FRAME_death01, FRAME_death50, chtjor_frames_death1, chtjor_dead };

static const mframe_t chtjor_frames_attack2[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, chtjorBFG },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(chtjor_move_attack2) = { FRAME_attak201, FRAME_attak213, chtjor_frames_attack2, chtjor_run };

static const mframe_t chtjor_frames_start_attack1[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge }
};
const mmove_t MMOVE_T(chtjor_move_start_attack1) = { FRAME_attak101, FRAME_attak108, chtjor_frames_start_attack1, chtjor_attack1 };

static const mframe_t chtjor_frames_attack1[] = {
#if 1
    { ai_charge, 0, chtjor_firebullet },
    { ai_charge },
    { ai_charge, 0, chtjor_firebullet },
    { ai_charge },
    { ai_charge, 0, chtjor_firebullet },
    { ai_charge }
#else
    { ai_charge, 0, chtjor_firebullet_left },
    { ai_charge, 0, chtjor_firebullet_right },
    { ai_charge, 0, chtjor_firebullet_left },
    { ai_charge, 0, chtjor_firebullet_right },
    { ai_charge, 0, chtjor_firebullet_left },
    { ai_charge, 0, chtjor_firebullet_right },
#endif
};
const mmove_t MMOVE_T(chtjor_move_attack1) = { FRAME_attak109, FRAME_attak114, chtjor_frames_attack1, chtjor_reattack1 };

static const mframe_t chtjor_frames_end_attack1[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(chtjor_move_end_attack1) = { FRAME_attak115, FRAME_attak118, chtjor_frames_end_attack1, chtjor_run };

void chtjor_reattack1(edict_t *self)
{
    if (visible(self, self->enemy)) {
        if (frandom() < 0.9f)
            M_SetAnimation(self, &chtjor_move_attack1);
        else {
            M_SetAnimation(self, &chtjor_move_end_attack1);
            chtjor_attack1_end_sound(self);
        }
    } else {
        M_SetAnimation(self, &chtjor_move_end_attack1);
        chtjor_attack1_end_sound(self);
    }
}

static void chtjor_attack1(edict_t *self)
{
    M_SetAnimation(self, &chtjor_move_attack1);
}

void PAIN(chtjor_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (level.time < self->pain_debounce_time)
        return;

    // Lessen the chance of him going into his pain frames if he takes little damage
    if (mod != MOD_CHAINFIST) {
        if (damage <= 40)
            if (frandom() <= 0.6f)
                return;

        if ((self->s.frame >= FRAME_attak101) && (self->s.frame <= FRAME_attak108))
            if (frandom() <= 0.005f)
                return;

        if ((self->s.frame >= FRAME_attak109) && (self->s.frame <= FRAME_attak114))
            if (frandom() <= 0.00005f)
                return;

        if ((self->s.frame >= FRAME_attak201) && (self->s.frame <= FRAME_attak208))
            if (frandom() <= 0.005f)
                return;
    }

    self->pain_debounce_time = level.time + SEC(3);

    bool do_pain3 = false;

    if (damage > 50) {
        if (damage <= 100) {
            G_StartSound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM);
        } else {
            if (frandom() <= 0.3f) {
                do_pain3 = true;
                G_StartSound(self, CHAN_VOICE, sound_pain3, 1, ATTN_NORM);
            }
        }
    }

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    chtjor_attack1_end_sound(self);

    if (damage <= 50)
        M_SetAnimation(self, &chtjor_move_pain1);
    else if (damage <= 100)
        M_SetAnimation(self, &chtjor_move_pain2);
    else if (do_pain3)
        M_SetAnimation(self, &chtjor_move_pain3);
}

void MONSTERINFO_SETSKIN(chtjor_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

static void chtjorBFG(edict_t *self)
{
    vec3_t forward, right;
    vec3_t start, vec, dir;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_CHTJOR_BFG], forward, right);

    vec = self->enemy->s.origin;
    vec.z += self->enemy->viewheight;

    dir = Vec3_Direction(vec, start);
    monster_fire_bfg(self, start, dir, 50, 300, 100, 200, MZ2_CHTJOR_BFG);
}

static void chtjor_firebullet_right(edict_t *self)
{
    vec3_t forward, right, start;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_CHTJOR_ROCKET_R], forward, right);

    M_PredictAim(self, self->enemy, start, 0, false, -0.2f, &forward, NULL);

    if (self->style)
        fire_rocket_shamjorg(self, start, forward, 35, 725, MZ2_CHTJOR_ROCKET_R);
    else
        fire_rocket_chtjor(self, start, forward, 50, 650, MZ2_CHTJOR_ROCKET_R);
}

static void chtjor_firebullet_left(edict_t *self)
{
    vec3_t forward, right, start;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_CHTJOR_ROCKET_L], forward, right);

    M_PredictAim(self, self->enemy, start, 0, false, 0.2f, &forward, NULL);

    if (self->style)
        fire_rocket_shamjorg(self, start, forward, 35, 725, MZ2_CHTJOR_ROCKET_L);
    else
        fire_rocket_chtjor(self, start, forward, 50, 650, MZ2_CHTJOR_ROCKET_L);
}

static void chtjor_firebullet(edict_t *self)
{
    chtjor_firebullet_left(self);
    chtjor_firebullet_right(self);
}

void MONSTERINFO_ATTACK(chtjor_attack)(edict_t *self)
{
    if (frandom() <= 0.75f) {
        G_StartSound(self, CHAN_WEAPON, sound_attack1, 1, ATTN_NORM);
        self->monsterinfo.weapon_sound = G_SoundIndex("boss3/w_loop.wav");
        M_SetAnimation(self, &chtjor_move_start_attack1);
    } else {
        G_StartSound(self, CHAN_VOICE, sound_attack2, 1, ATTN_NORM);
        M_SetAnimation(self, &chtjor_move_attack2);
    }
}

static const gib_def_t chtjor_gibs[] = {
    { "models/objects/gibs/sm_meat/tris.md2", 2 },
    { "models/objects/gibs/sm_metal/tris.md2", 2, GIB_METALLIC },
    { "models/monsters/chtjor/gibs/chest.md2", 1, GIB_SKINNED },
    { "models/monsters/chtjor/gibs/foot.md2", 2, GIB_SKINNED },
    { "models/monsters/chtjor/gibs/gun.md2", 2, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/chtjor/gibs/thigh.md2", 2, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/chtjor/gibs/spine.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/chtjor/gibs/tube.md2", 4, GIB_SKINNED },
    { "models/monsters/chtjor/gibs/spike.md2", 6, GIB_SKINNED },
    { "models/monsters/chtjor/gibs/head.md2", 1, GIB_SKINNED | GIB_METALLIC | GIB_HEAD },
    { 0 }
};

static const gib_def_t shamjorg_gibs[] = {
    { "models/objects/gibs/sm_meat/tris.md2", 2 },
    { "models/objects/gibs/sm_metal/tris.md2", 2, GIB_METALLIC },
    { "models/monsters/shamjorg/gibs/g_arm.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/shamjorg/gibs/g_arm2.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/shamjorg/gibs/g_leg.md2", 2, GIB_SKINNED },
    { "models/monsters/shamjorg/gibs/g_torso.md2", 1, GIB_SKINNED },
    { "models/monsters/shamjorg/gibs/g_head.md2", 1, GIB_SKINNED | GIB_METALLIC | GIB_HEAD },
    { 0 }
};

static void chtjor_dead(edict_t *self)
{
    G_AddEvent(self, EV_EXPLOSION1_BIG, 0);
    self->s.sound = 0;
    if (self->style)
        ThrowGibs(self, 500, shamjorg_gibs);
    else
        ThrowGibs(self, 500, chtjor_gibs);
}

void DIE(chtjor_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    G_StartSound(self, CHAN_VOICE, sound_death, 1, ATTN_NORM);
    chtjor_attack1_end_sound(self);
    self->deadflag = true;
    self->takedamage = false;
    M_SetAnimation(self, &chtjor_move_death);
}

bool MONSTERINFO_CHECKATTACK(chtjor_CheckAttack)(edict_t *self)
{
    return M_CheckAttack_Base(self, 0.4f, 0.8f, 0.6f, 0.4f, 0.2f, 0.0f);
}

void PR_monster_chtjor(void)
{
    sound_pain1 = G_SoundIndex("boss3/bs3pain1.wav");
    sound_pain2 = G_SoundIndex("boss3/bs3pain2.wav");
    sound_pain3 = G_SoundIndex("boss3/bs3pain3.wav");
    sound_death = G_SoundIndex("boss3/bs3deth1.wav");
    sound_attack1 = G_SoundIndex("boss3/bs3atck1.wav");
    sound_attack1_loop = G_SoundIndex("boss3/bs3atck1_loop.wav");
    sound_attack1_end = G_SoundIndex("boss3/bs3atck1_end.wav");
    sound_attack2 = G_SoundIndex("boss3/bs3atck2.wav");
    sound_search1 = G_SoundIndex("boss3/bs3srch1.wav");
    sound_search2 = G_SoundIndex("boss3/bs3srch2.wav");
    sound_search3 = G_SoundIndex("boss3/bs3srch3.wav");
    sound_idle = G_SoundIndex("boss3/bs3idle1.wav");
    sound_step_left = G_SoundIndex("boss3/step1.wav");
    sound_step_right = G_SoundIndex("boss3/step2.wav");
    sound_firegun = G_SoundIndex("boss3/xfire.wav");
    sound_death_hit = G_SoundIndex("boss3/d_hit.wav");
    G_SoundIndex("makron/bfg_fire.wav");
}

static void SP_monster_chtjor_x(edict_t *self)
{
    self->movetype = MOVETYPE_STEP;
    self->r.box = Box3_FromSize(100, 0, 350);
    self->r.solid = SOLID_BBOX;

    self->health = 12000 * st.health_multiplier;
    self->gib_health = -2000;
    self->mass = 1000;

    self->pain = chtjor_pain;
    self->die = chtjor_die;
    self->monsterinfo.stand = chtjor_stand;
    self->monsterinfo.walk = chtjor_walk;
    self->monsterinfo.run = chtjor_run;
    self->monsterinfo.dodge = NULL;
    self->monsterinfo.attack = chtjor_attack;
    self->monsterinfo.search = chtjor_search;
    self->monsterinfo.melee = NULL;
    self->monsterinfo.sight = NULL;
    self->monsterinfo.checkattack = chtjor_CheckAttack;
    self->monsterinfo.setskin = chtjor_setskin;

    M_SetAnimation(self, &chtjor_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
    walkmonster_start(self);
}

/*QUAKED monster_chtjor (1 .5 0) (-80 -80 0) (90 90 140) Ambush Trigger_Spawn Sight */
void SP_monster_chtjor(edict_t *self)
{
    self->style = 0;
    self->s.modelindex = G_ModelIndex("models/monsters/chtjor/tris.md2");
    SP_monster_chtjor_x(self);
    G_PrecacheGibs(chtjor_gibs);
}

/*QUAKED monster_shamjorg (1 .5 0) (-80 -80 0) (90 90 140) Ambush Trigger_Spawn Sight */
void SP_monster_shamjorg(edict_t *self)
{
    self->style = 1;
    self->s.modelindex = G_ModelIndex("models/monsters/shamjorg/tris.md2");
    SP_monster_chtjor_x(self);
    G_PrecacheGibs(shamjorg_gibs);
}
