// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

SHAMBLER

==============================================================================
*/

#include "g_local.h"
#include "m_shambler.h"

#define SOUND   sound[!!self->style]

enum { Plain, Proto, Strogg, Shamacudda };

static struct {
    int pain;
    int idle;
    int die;
    int sight;
    int windup;
    int melee1;
    int melee2;
    int smack;
    int boom;
} sound[2];

//
// misc
//

void MONSTERINFO_SIGHT(shambler_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_VOICE, SOUND.sight, 1, ATTN_NORM);
}

static const vec3_t lightning_left_hand[] = {
    { 44, 36, 25 },
    { 10, 44, 57 },
    { -1, 40, 70 },
    { -10, 34, 75 },
    { 7.4f, 24, 89 }
};

static const vec3_t lightning_right_hand[] = {
    { 28, -38, 25 },
    { 31, -7, 70 },
    { 20, 0, 80 },
    { 16, 1.2f, 81 },
    { 27, -11, 83 }
};

static void shambler_lightning_update(edict_t *self)
{
    edict_t *lightning = self->beam;

    if (!lightning || self->s.frame - FRAME_magic01 >= q_countof(lightning_left_hand))
        return;

    vec3_t f, r;
    AngleVectors(self->s.angles, &f, &r, NULL);
    lightning->s.origin = M_ProjectFlashSource(self, lightning_left_hand[self->s.frame - FRAME_magic01], f, r);
    lightning->s.old_origin = M_ProjectFlashSource(self, lightning_right_hand[self->s.frame - FRAME_magic01], f, r);
    trap_LinkEntity(lightning);
}

static void shambler_windup(edict_t *self)
{
    G_StartSound(self, CHAN_WEAPON, SOUND.windup, 1, ATTN_NORM);

    self->beam = G_SpawnLightning(self);
    shambler_lightning_update(self);
}

static void shamacudda_lightning_update(edict_t *self)
{
    edict_t *left = self->beam;
    edict_t *right = self->beam2;

    if (self->s.frame - FRAME_magic01 >= q_countof(lightning_left_hand))
        return;

    vec3_t f, r;
    AngleVectors(self->s.angles, &f, &r, NULL);

    if (left) {
        left->s.origin = M_ProjectFlashSource(self, lightning_left_hand[self->s.frame - FRAME_magic01], f, r);
        trap_LinkEntity(left);
    }

    if (right) {
        right->s.origin = M_ProjectFlashSource(self, lightning_right_hand[self->s.frame - FRAME_magic01], f, r);
        trap_LinkEntity(right);
    }
}

static void shamacudda_windup(edict_t *self)
{
    float scale = self->s.scale ? self->s.scale : 1.0f;

    G_StartSound(self, CHAN_WEAPON, SOUND.windup, 1, ATTN_NORM);

    edict_t *left = self->beam = G_Spawn();
    left->s.scale = scale * 0.5f;
    left->s.alpha = self->s.alpha;
    left->s.modelindex = G_ModelIndex("sprites/s_bfx1.sp2");
    left->r.ownernum = self->s.number;

    edict_t *right = self->beam2 = G_Spawn();
    right->s.scale = scale * 0.5f;
    right->s.alpha = self->s.alpha;
    right->s.modelindex = G_ModelIndex("sprites/s_bfx1.sp2");
    right->r.ownernum = self->s.number;

    shamacudda_lightning_update(self);
}

void MONSTERINFO_IDLE(shambler_idle)(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.idle, 1, ATTN_IDLE);
}

static void shambler_maybe_idle(edict_t *self)
{
    if (frandom() > 0.8f)
        G_StartSound(self, CHAN_VOICE, SOUND.idle, 1, ATTN_IDLE);
}

//
// stand
//

static const mframe_t shambler_frames_stand[] = {
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
const mmove_t MMOVE_T(shambler_move_stand) = { FRAME_stand01, FRAME_stand17, shambler_frames_stand, NULL };

void MONSTERINFO_STAND(shambler_stand)(edict_t *self)
{
    M_SetAnimation(self, &shambler_move_stand);
}

//
// walk
//

static const mframe_t shambler_frames_walk[] = {
    { ai_walk, 10 }, // FIXME: add footsteps?
    { ai_walk, 9 },
    { ai_walk, 9 },
    { ai_walk, 5 },
    { ai_walk, 6 },
    { ai_walk, 12 },
    { ai_walk, 8 },
    { ai_walk, 3 },
    { ai_walk, 13 },
    { ai_walk, 9 },
    { ai_walk, 7, shambler_maybe_idle },
    { ai_walk, 5 },
};
const mmove_t MMOVE_T(shambler_move_walk) = { FRAME_walk01, FRAME_walk12, shambler_frames_walk, NULL };

void MONSTERINFO_WALK(shambler_walk)(edict_t *self)
{
    M_SetAnimation(self, &shambler_move_walk);
}

//
// run
//

static const mframe_t shambler_frames_run[] = {
    { ai_run, 20 }, // FIXME: add footsteps?
    { ai_run, 24 },
    { ai_run, 20 },
    { ai_run, 20 },
    { ai_run, 24 },
    { ai_run, 20, shambler_maybe_idle },
};
const mmove_t MMOVE_T(shambler_move_run) = { FRAME_run01, FRAME_run06, shambler_frames_run, NULL };

void MONSTERINFO_RUN(shambler_run)(edict_t *self)
{
    if (self->enemy && self->enemy->client)
        self->monsterinfo.aiflags |= AI_BRUTAL;
    else
        self->monsterinfo.aiflags &= ~AI_BRUTAL;

    if (self->monsterinfo.aiflags & AI_STAND_GROUND) {
        M_SetAnimation(self, &shambler_move_stand);
        return;
    }

    M_SetAnimation(self, &shambler_move_run);
}

//
// pain
//

// FIXME: needs halved explosion damage

static const mframe_t shambler_frames_pain[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
};
const mmove_t MMOVE_T(shambler_move_pain) = { FRAME_pain01, FRAME_pain06, shambler_frames_pain, shambler_run };

void PAIN(shambler_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (level.time < self->timestamp)
        return;

    self->timestamp = level.time + FRAME_TIME;
    G_StartSound(self, CHAN_AUTO, SOUND.pain, 1, ATTN_NORM);

    if (mod != MOD_CHAINFIST && damage <= 30 && frandom() > 0.2f)
        return;

    // If hard or nightmare, don't go into pain while attacking
    if (skill.integer >= 2) {
        if ((self->s.frame >= FRAME_smash01) && (self->s.frame <= FRAME_smash12))
            return;
        if ((self->s.frame >= FRAME_swingl01) && (self->s.frame <= FRAME_swingl09))
            return;
        if ((self->s.frame >= FRAME_swingr01) && (self->s.frame <= FRAME_swingr09))
            return;
    }

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + SEC(2);
    M_SetAnimation(self, &shambler_move_pain);
}

void MONSTERINFO_SETSKIN(shambler_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

//
// attacks
//

static void shambler_windup_done(edict_t *self)
{
    self->monsterinfo.nextframe = FRAME_magic09;

    if (self->style == Plain)
        G_StartSound(self, CHAN_WEAPON, SOUND.boom, 1, ATTN_NORM);

    M_FreeBeams(self);
}

#define SPAWNFLAG_SHAMBLER_PRECISE  1

static vec3_t FindShamblerOffset(edict_t *self)
{
    vec3_t offset = { 0, 0, 48 };

    for (int i = 0; i < 8; i++) {
        if (M_CheckClearShot(self, offset))
            return offset;
        offset.z -= 4;
    }

    return Vec3(0, 0, 48);
}

static void ShamblerCastLightning(edict_t *self)
{
    if (!self->enemy || !self->enemy->r.inuse)
        return;

    vec3_t start, end;
    vec3_t dir;
    vec3_t forward, right;
    vec3_t offset;

    offset = FindShamblerOffset(self);

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, offset, forward, right);

    // calc direction to where we targeted
    M_PredictAim(self, self->enemy, start, 0, false, (self->spawnflags & SPAWNFLAG_SHAMBLER_PRECISE) ? 0.0f : 0.1f, &dir, NULL);

    end = Vec3_MA(start, 8192, dir);
    trace_t tr = G_TraceLine(start, end, self->s.number, MASK_PROJECTILE | CONTENTS_SLIME | CONTENTS_LAVA);

    if (!self->beam)
        self->beam = G_SpawnLightning(self);

    self->beam->s.old_origin = G_SnapVector(start);
    self->beam->s.origin = G_SnapVectorTowards(tr.endpos, start);
    trap_LinkEntity(self->beam);

    fire_bullet(self, start, dir, irandom2(8, 12), 15, 0, 0, MOD_TESLA);
}

static const mframe_t shambler_frames_magic[] = {
    { ai_charge, 0, shambler_windup },
    { ai_charge, 0, shambler_lightning_update },
    { ai_charge, 0, shambler_lightning_update },
    { ai_move, 0, shambler_lightning_update },
    { ai_move, 0, shambler_lightning_update },
    { ai_move, 0, shambler_windup_done },
    { ai_move },
    { ai_charge },
    { ai_move, 0, ShamblerCastLightning },
    { ai_move, 0, ShamblerCastLightning },
    { ai_move, 0, ShamblerCastLightning },
    { ai_move },
};
const mmove_t MMOVE_T(shambler_attack_magic) = { FRAME_magic01, FRAME_magic12, shambler_frames_magic, shambler_run };

static void ShamblerBFG(edict_t *self)
{
    if (!self->enemy || !self->enemy->r.inuse)
        return;

    vec3_t start;
    vec3_t dir;
    vec3_t forward, right;
    vec3_t offset;

    offset = FindShamblerOffset(self);

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, offset, forward, right);
    dir = Vec3_Direction(self->enemy->s.origin, start);

    monster_fire_bfg(self, start, dir, 50, 300, 100, 200, MZ2_SHAMBLER_BFG);
}

static const mframe_t shambler_frames_bfg[] = {
    { ai_charge, 0, shamacudda_windup },
    { ai_charge, 0, shamacudda_lightning_update },
    { ai_charge, 0, shamacudda_lightning_update },
    { ai_move, 0, shamacudda_lightning_update },
    { ai_move, 0, shamacudda_lightning_update },
    { ai_move, 0, shambler_windup_done },
    { ai_move },
    { ai_charge },
    { ai_move, 0, ShamblerBFG },
    { ai_move },
    { ai_move },
    { ai_move },
};
const mmove_t MMOVE_T(shambler_attack_bfg) = { FRAME_magic01, FRAME_magic12, shambler_frames_bfg, shambler_run };

static void ShamblerChecker(edict_t *self)
{
    self->fly_sound_debounce_time = level.time + SEC(1.5f);
}

static void ShamblerRocket(edict_t *self)
{
    vec3_t  start, forward, right, dir;

    if (!self->enemy || !self->enemy->r.inuse)
        return;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_SHAMBLER_ROCKET], forward, right);

    M_PredictAim(self, self->enemy, start, 650, false, (self->spawnflags & SPAWNFLAG_SHAMBLER_PRECISE) ? 0.0f : 0.1f, &dir, NULL);
    monster_fire_rocket(self, start, dir, 100, 650, MZ2_SHAMBLER_ROCKET);

    if (self->fly_sound_debounce_time > level.time)
        self->monsterinfo.nextframe = FRAME_magic08;
}

static const mframe_t shambler_frames_rocket2[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, ShamblerChecker },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, ShamblerRocket },
    { ai_charge },
    { ai_charge }
};
const mmove_t MMOVE_T(shambler_attack_rocket2) = { FRAME_magic01, FRAME_magic12, shambler_frames_rocket2, shambler_run };

static const mframe_t shambler_frames_rocket[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, ShamblerRocket },
    { ai_charge },
    { ai_charge }
};
const mmove_t MMOVE_T(shambler_attack_rocket) = { FRAME_magic01, FRAME_magic12, shambler_frames_rocket, shambler_run };

void MONSTERINFO_ATTACK(shambler_attack)(edict_t *self)
{
    switch (self->style) {
    case Shamacudda:
        M_SetAnimation(self, &shambler_attack_bfg);
        break;
    case Proto:
    case Strogg:
        if (self->health < (self->max_health / 2))
            M_SetAnimation(self, &shambler_attack_rocket2);
        else
            M_SetAnimation(self, &shambler_attack_rocket);
        break;
    default:
        M_SetAnimation(self, &shambler_attack_magic);
        break;
    }
}

//
// melee
//

static void shambler_melee1(edict_t *self)
{
    G_StartSound(self, CHAN_WEAPON, SOUND.melee1, 1, ATTN_NORM);
}

static void shambler_melee2(edict_t *self)
{
    G_StartSound(self, CHAN_WEAPON, SOUND.melee2, 1, ATTN_NORM);
}

static void sham_swingl9(edict_t *self);
static void sham_swingr9(edict_t *self);

static void sham_smash10(edict_t *self)
{
    if (!self->enemy)
        return;

    ai_charge(self, 0);

    if (!CanDamage(self->enemy, self))
        return;

    vec3_t aim = { MELEE_DISTANCE, self->r.box.mins.x, -4 };
    bool hit = fire_hit(self, aim, irandom2(110, 120), 120); // Slower attack

    if (hit)
        G_StartSound(self, CHAN_WEAPON, SOUND.smack, 1, ATTN_NORM);
}

static void ShamClaw(edict_t *self)
{
    if (!self->enemy)
        return;

    ai_charge(self, 10);

    if (!CanDamage(self->enemy, self))
        return;

    vec3_t aim = { MELEE_DISTANCE, self->r.box.mins.x, -4 };
    bool hit = fire_hit(self, aim, irandom2(70, 80), 80); // Slower attack

    if (hit)
        G_StartSound(self, CHAN_WEAPON, SOUND.smack, 1, ATTN_NORM);
}

static const mframe_t shambler_frames_smash[] = {
    { ai_charge, 2, shambler_melee1 },
    { ai_charge, 6 },
    { ai_charge, 6 },
    { ai_charge, 5 },
    { ai_charge, 4 },
    { ai_charge, 1 },
    { ai_charge, 0 },
    { ai_charge, 0 },
    { ai_charge, 0 },
    { ai_charge, 0, sham_smash10 },
    { ai_charge, 5 },
    { ai_charge, 4 },
};

const mmove_t MMOVE_T(shambler_attack_smash) = { FRAME_smash01, FRAME_smash12, shambler_frames_smash, shambler_run };

static const mframe_t shambler_frames_swingl[] = {
    { ai_charge, 5, shambler_melee1 },
    { ai_charge, 3 },
    { ai_charge, 7 },
    { ai_charge, 3 },
    { ai_charge, 7 },
    { ai_charge, 9 },
    { ai_charge, 5, ShamClaw },
    { ai_charge, 4 },
    { ai_charge, 8, sham_swingl9 },
};

const mmove_t MMOVE_T(shambler_attack_swingl) = { FRAME_swingl01, FRAME_swingl09, shambler_frames_swingl, shambler_run };

static const mframe_t shambler_frames_swingr[] = {
    { ai_charge, 1, shambler_melee2 },
    { ai_charge, 8 },
    { ai_charge, 14 },
    { ai_charge, 7 },
    { ai_charge, 3 },
    { ai_charge, 6 },
    { ai_charge, 6, ShamClaw },
    { ai_charge, 3 },
    { ai_charge, 8, sham_swingr9 },
};

const mmove_t MMOVE_T(shambler_attack_swingr) = { FRAME_swingr01, FRAME_swingr09, shambler_frames_swingr, shambler_run };

static void sham_swingl9(edict_t *self)
{
    ai_charge(self, 8);

    if (brandom() && self->enemy && range_to(self, self->enemy) < MELEE_DISTANCE)
        M_SetAnimation(self, &shambler_attack_swingr);
}

static void sham_swingr9(edict_t *self)
{
    ai_charge(self, 1);
    ai_charge(self, 10);

    if (brandom() && self->enemy && range_to(self, self->enemy) < MELEE_DISTANCE)
        M_SetAnimation(self, &shambler_attack_swingl);
}

void MONSTERINFO_MELEE(shambler_melee)(edict_t *self)
{
    float chance = frandom();

    if (chance > 0.6f || self->health == 600)
        M_SetAnimation(self, &shambler_attack_smash);
    else if (chance > 0.3f)
        M_SetAnimation(self, &shambler_attack_swingl);
    else
        M_SetAnimation(self, &shambler_attack_swingr);
}

//
// death
//

static void shambler_dead(edict_t *self)
{
    self->r.box = Box3_FromSize(16, -24, 0);
    monster_dead(self);
}

static void shambler_shrink(edict_t *self)
{
    self->r.box.maxs.z = 0;
    self->r.svflags |= SVF_DEADMONSTER;
    trap_LinkEntity(self);
}

static const mframe_t shambler_frames_death[] = {
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0, shambler_shrink },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 }, // FIXME: thud?
};
const mmove_t MMOVE_T(shambler_move_death) = { FRAME_death01, FRAME_death11, shambler_frames_death, shambler_dead };

// FIXME: better gibs for shambler, shambler head
static const gib_def_t shambler_gibs_plain[] = {
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/objects/gibs/chest/tris.md2", 1 },
    { "models/objects/gibs/head2/tris.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t shambler_gibs_proto[] = {
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/objects/gibs/chest/tris.md2", 1 },
    { "models/monsters/shambler_prototype/gibs/g_arm.md2", 1 },
    { "models/monsters/shambler_prototype/gibs/g_leg.md2", 1 },
    { "models/monsters/shambler_prototype/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t shambler_gibs_strogg[] = {
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/objects/gibs/chest/tris.md2", 1 },
    { "models/monsters/shamblerstrogg/gibs/g_arm.md2", 1 },
    { "models/monsters/shamblerstrogg/gibs/g_leg.md2", 1 },
    { "models/monsters/shamblerstrogg/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t shambler_gibs_cuda[] = {
    { "models/monsters/shamacudda/gibs/arm/tris.md2", 1, GIB_ACID },
    { "models/monsters/shamacudda/gibs/torso/tris.md2", 1, GIB_ACID },
    { "models/monsters/shamacudda/gibs/leg/tris.md2", 1, GIB_ACID },
    { "models/monsters/shamacudda/gibs/head/tris.md2", 1, GIB_ACID | GIB_HEAD },
    { 0 }
};

void DIE(shambler_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    M_FreeBeams(self);

    // check for gib
    if (M_CheckGib(self, mod)) {
        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        switch (self->style) {
        case Shamacudda:
            ThrowGibs(self, damage, shambler_gibs_cuda);
            break;
        case Strogg:
            ThrowGibs(self, damage, shambler_gibs_strogg);
            break;
        case Proto:
            ThrowGibs(self, damage, shambler_gibs_proto);
            break;
        default:
            ThrowGibs(self, damage, shambler_gibs_plain);
            break;
        }
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;

    // regular death
    G_StartSound(self, CHAN_VOICE, SOUND.die, 1, ATTN_NORM);

    self->deadflag = true;
    self->takedamage = true;

    if ((self->style == Proto || self->style == Strogg) && brandom())
        Drop_Item(self, GetItemByIndex(IT_AMMO_ROCKETS))->count = 2;

    M_SetAnimation(self, &shambler_move_death);
}

void PR_monster_shambler(void)
{
    sound[0].pain = G_SoundIndex("shambler/shurt2.wav");
    sound[0].idle = G_SoundIndex("shambler/sidle.wav");
    sound[0].die = G_SoundIndex("shambler/sdeath.wav");
    sound[0].windup = G_SoundIndex("shambler/sattck1.wav");
    sound[0].melee1 = G_SoundIndex("shambler/melee1.wav");
    sound[0].melee2 = G_SoundIndex("shambler/melee2.wav");
    sound[0].sight = G_SoundIndex("shambler/ssight.wav");
    sound[0].smack = G_SoundIndex("shambler/smack.wav");
    sound[0].boom = G_SoundIndex("shambler/sboom.wav");
}

void PR_monster_shambler_strogg(void)
{
    sound[1].pain = G_SoundIndex("shambler/shurt2_s.wav");
    sound[1].idle = G_SoundIndex("shambler/sidle_s.wav");
    sound[1].die = G_SoundIndex("shambler/sdeath_s.wav");
    sound[1].windup = G_SoundIndex("shambler/sattck1.wav");
    sound[1].melee1 = G_SoundIndex("shambler/melee1_s.wav");
    sound[1].melee2 = G_SoundIndex("shambler/melee2_s.wav");
    sound[1].sight = G_SoundIndex("shambler/ssight_s.wav");
    sound[1].smack = G_SoundIndex("shambler/smack.wav");
    sound[1].boom = G_SoundIndex("shambler/sboom.wav");
}

static void SP_monster_shambler_x(edict_t *self)
{
    self->movetype = MOVETYPE_STEP;
    self->r.solid = SOLID_BBOX;
    self->r.box = Box3_FromSize(32, -24, 64);

    self->health = 600 * st.health_multiplier;
    self->gib_health = -60;
    self->mass = 500;

    self->pain = shambler_pain;
    self->die = shambler_die;
    self->monsterinfo.stand = shambler_stand;
    self->monsterinfo.walk = shambler_walk;
    self->monsterinfo.run = shambler_run;
    self->monsterinfo.dodge = NULL;
    self->monsterinfo.attack = shambler_attack;
    self->monsterinfo.melee = shambler_melee;
    self->monsterinfo.sight = shambler_sight;
    self->monsterinfo.idle = shambler_idle;
    self->monsterinfo.blocked = NULL;
    if (self->style)
        self->monsterinfo.setskin = shambler_setskin;

    trap_LinkEntity(self);

    if (self->spawnflags & SPAWNFLAG_SHAMBLER_PRECISE)
        self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

    M_SetAnimation(self, &shambler_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    walkmonster_start(self);
}

void SP_monster_shambler(edict_t *self)
{
    self->style = Plain;
    self->s.modelindex = G_ModelIndex("models/monsters/shambler/tris.md2");
    SP_monster_shambler_x(self);
    G_ModelIndex("models/proj/lightning/tris.md2");
}

void SP_monster_shambler_prototype(edict_t *self)
{
    self->style = Proto;
    self->s.modelindex = G_ModelIndex("models/monsters/shambler_prototype/tris.md2");
    SP_monster_shambler_x(self);
    G_PrecacheGibs(shambler_gibs_proto);
}

void SP_monster_shambler_strogg(edict_t *self)
{
    self->style = Strogg;
    self->s.modelindex = G_ModelIndex("models/monsters/shamblerstrogg/tris.md2");
    SP_monster_shambler_x(self);
    G_PrecacheGibs(shambler_gibs_strogg);
}

void SP_monster_shamacudda(edict_t *self)
{
    self->style = Shamacudda;
    self->flags |= FL_ACIDIC;
    self->s.modelindex = G_ModelIndex("models/monsters/shamacudda/tris.md2");

    SP_monster_shambler_x(self);

    G_PrecacheGibs(shambler_gibs_cuda);
    G_ModelIndex("sprites/s_bfx1.sp2");
    G_ModelIndex("sprites/s_bfx2.sp2");
    G_ModelIndex("sprites/s_bfx3.sp2");
    G_SoundIndex("makron/bfg_fire.wav");
}
