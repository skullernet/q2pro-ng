// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

parasite

==============================================================================
*/

#include "g_local.h"
#include "m_dog_strogg.h"

void DogLeapTouch(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self);

static int sound_melee;
static int sound_pain1;
static int sound_pain2;
static int sound_die;
static int sound_sight;
static int sound_tap;
static int sound_scratch;
static int sound_search;

void dog_str_stand(edict_t *self);
void dog_str_start_run(edict_t *self);
static void dog_str_run(edict_t *self);
static void dog_str_walk(edict_t *self);
static void dog_str_do_fidget(edict_t *self);
static void dog_str_refidget(edict_t *self);

void MONSTERINFO_SIGHT(dog_str_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_WEAPON, sound_sight, 1, ATTN_NORM);
}

static void dog_str_tap(edict_t *self)
{
    G_StartSound(self, CHAN_WEAPON, sound_tap, 0.75f, 2.75f);
}

static void dog_str_scratch(edict_t *self)
{
    G_StartSound(self, CHAN_WEAPON, sound_scratch, 0.75f, 2.75f);
}

#if 0
static void dog_str_search(edict_t *self)
{
    G_StartSound(self, CHAN_WEAPON, sound_search, 1, ATTN_IDLE);
}
#endif

static const mframe_t dog_str_frames_start_fidget[] = {
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand }
};
const mmove_t MMOVE_T(dog_str_move_start_fidget) = { FRAME_stand18, FRAME_stand21, dog_str_frames_start_fidget, dog_str_do_fidget };

static const mframe_t dog_str_frames_fidget[] = {
    { ai_stand, 0, dog_str_scratch },
    { ai_stand },
    { ai_stand },
    { ai_stand, 0, dog_str_scratch },
    { ai_stand },
    { ai_stand }
};
const mmove_t MMOVE_T(dog_str_move_fidget) = { FRAME_stand22, FRAME_stand27, dog_str_frames_fidget, dog_str_refidget };

static const mframe_t dog_str_frames_end_fidget[] = {
    { ai_stand, 0, dog_str_scratch },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand }
};
const mmove_t MMOVE_T(dog_str_move_end_fidget) = { FRAME_stand28, FRAME_stand35, dog_str_frames_end_fidget, dog_str_stand };

static void dog_str_do_fidget(edict_t *self)
{
    M_SetAnimation(self, &dog_str_move_fidget);
}

static void dog_str_refidget(edict_t *self)
{
    if (frandom() <= 0.8f)
        M_SetAnimation(self, &dog_str_move_fidget);
    else
        M_SetAnimation(self, &dog_str_move_end_fidget);
}

void MONSTERINFO_IDLE(dog_str_idle)(edict_t *self)
{
    if (self->enemy)
        return;

    M_SetAnimation(self, &dog_str_move_start_fidget);
}

static const mframe_t dog_str_frames_stand[] = {
    { ai_stand },
    { ai_stand },
    { ai_stand, 0, dog_str_tap },
    { ai_stand },
    { ai_stand, 0, dog_str_tap },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand, 0, dog_str_tap },
    { ai_stand },
    { ai_stand, 0, dog_str_tap },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand, 0, dog_str_tap },
    { ai_stand },
    { ai_stand, 0, dog_str_tap }
};
const mmove_t MMOVE_T(dog_str_move_stand) = { FRAME_stand01, FRAME_stand17, dog_str_frames_stand, dog_str_stand };

void MONSTERINFO_STAND(dog_str_stand)(edict_t *self)
{
    M_SetAnimation(self, &dog_str_move_stand);
}

static const mframe_t dog_str_frames_run[] = {
    { ai_run, 30 },
    { ai_run, 30 },
    { ai_run, 22, monster_footstep },
    { ai_run, 19, monster_footstep },
    { ai_run, 24 },
    { ai_run, 28, monster_footstep },
    { ai_run, 25, monster_footstep }
};
const mmove_t MMOVE_T(dog_str_move_run) = { FRAME_run03, FRAME_run09, dog_str_frames_run, NULL };

static const mframe_t dog_str_frames_start_run[] = {
    { ai_run },
    { ai_run, 30 },
};
const mmove_t MMOVE_T(dog_str_move_start_run) = { FRAME_run01, FRAME_run02, dog_str_frames_start_run, dog_str_run };

#if 0
static const mframe_t dog_str_frames_stop_run[] = {
    { ai_run, 20 },
    { ai_run, 20 },
    { ai_run, 12 },
    { ai_run, 10 },
    { ai_run },
    { ai_run }
};
const mmove_t MMOVE_T(dog_str_move_stop_run) = { FRAME_run10, FRAME_run15, dog_str_frames_stop_run, NULL };
#endif

void MONSTERINFO_RUN(dog_str_start_run)(edict_t *self)
{
    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
        M_SetAnimation(self, &dog_str_move_stand);
    else
        M_SetAnimation(self, &dog_str_move_start_run);
}

static void dog_str_run(edict_t *self)
{
    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
        M_SetAnimation(self, &dog_str_move_stand);
    else
        M_SetAnimation(self, &dog_str_move_run);
}

static const mframe_t dog_str_frames_walk[] = {
    { ai_walk, 30 },
    { ai_walk, 30 },
    { ai_walk, 22, monster_footstep },
    { ai_walk, 19, monster_footstep },
    { ai_walk, 24 },
    { ai_walk, 28, monster_footstep },
    { ai_walk, 25, monster_footstep }
};
const mmove_t MMOVE_T(dog_str_move_walk) = { FRAME_run03, FRAME_run09, dog_str_frames_walk, dog_str_walk };

static const mframe_t dog_str_frames_start_walk[] = {
    { ai_walk, 0 },
    { ai_walk, 30, dog_str_walk }
};
const mmove_t MMOVE_T(dog_str_move_start_walk) = { FRAME_run01, FRAME_run02, dog_str_frames_start_walk, NULL };

#if 0
static const mframe_t dog_str_frames_stop_walk[] = {
    { ai_walk, 20 },
    { ai_walk, 20 },
    { ai_walk, 12 },
    { ai_walk, 10 },
    { ai_walk },
    { ai_walk }
};
const mmove_t MMOVE_T(dog_str_move_stop_walk) = { FRAME_run10, FRAME_run15, dog_str_frames_stop_walk, NULL };
#endif

void MONSTERINFO_WALK(dog_str_start_walk)(edict_t *self)
{
    M_SetAnimation(self, &dog_str_move_start_walk);
}

static void dog_str_walk(edict_t *self)
{
    M_SetAnimation(self, &dog_str_move_walk);
}

// Leap
static void DogStrLeaper(edict_t *self)
{
    if (!self->enemy || !self->enemy->r.inuse)
        return;

    float length = Vec3_Distance(self->s.origin, self->enemy->s.origin);
    float fwd_speed = length * 1.95f;
    vec3_t forward;

    G_StartSound(self, CHAN_VOICE, sound_melee, 1, ATTN_NORM);

    AngleVectors(self->s.angles, &forward, NULL, NULL);
    self->s.origin.z += 1;
    self->velocity = Vec3_Scale(forward, fwd_speed);
    self->velocity.z = 250;
    self->groundentity = NULL;
    self->touch = DogLeapTouch;
    self->monsterinfo.attack_finished = level.time + SEC(3);
}

static const mframe_t dog_str_frames_leap[] = {
    { ai_charge },
    { ai_charge, 0, DogStrLeaper },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(dog_str_move_leap) = { FRAME_jump01, FRAME_jump08, dog_str_frames_leap, dog_str_start_run };

void MONSTERINFO_ATTACK(dog_str_leap)(edict_t *self)
{
    if (range_to(self, self->enemy) > 300)
        return;
    M_SetAnimation(self, &dog_str_move_leap);
}

// melee
static void DogStrBite(edict_t *self)
{
    vec3_t aim = { MELEE_DISTANCE, 0, 8 };
    G_StartSound(self, CHAN_VOICE, sound_melee, 1, ATTN_NORM);
    if (!fire_hit(self, aim, irandom2(5, 11), 10))
        self->monsterinfo.melee_debounce_time = level.time + SEC(1.2f);
}

static const mframe_t dog_str_frames_melee[] = {
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 10, DogStrBite },
    { ai_charge, 10 },
    { ai_charge, 10 },
    { ai_charge, 10 }
};
const mmove_t MMOVE_T(dog_str_move_melee) = { FRAME_drain02, FRAME_drain08, dog_str_frames_melee, dog_str_start_run };

void MONSTERINFO_MELEE(dog_str_melee)(edict_t *self)
{
    if (range_to(self, self->enemy) > MELEE_DISTANCE)
        return;
    M_SetAnimation(self, &dog_str_move_melee);
}

//================
// ROGUE
static void dog_str_jump_down(edict_t *self)
{
    vec3_t forward, up;

    AngleVectors(self->s.angles, &forward, NULL, &up);
    self->velocity = Vec3_MA(self->velocity, 100, forward);
    self->velocity = Vec3_MA(self->velocity, 300, up);
}

static void dog_str_jump_up(edict_t *self)
{
    vec3_t forward, up;

    AngleVectors(self->s.angles, &forward, NULL, &up);
    self->velocity = Vec3_MA(self->velocity, 200, forward);
    self->velocity = Vec3_MA(self->velocity, 450, up);
}

static void dog_str_jump_wait_land(edict_t *self)
{
    if (self->groundentity == NULL) {
        self->monsterinfo.nextframe = self->s.frame;

        if (monster_jump_finished(self))
            self->monsterinfo.nextframe = self->s.frame + 1;
    } else
        self->monsterinfo.nextframe = self->s.frame + 1;
}

static const mframe_t dog_str_frames_jump_up[] = {
    { ai_move, -8 },
    { ai_move, -8 },
    { ai_move, -8 },
    { ai_move, -8, dog_str_jump_up },
    { ai_move },
    { ai_move },
    { ai_move, 0, dog_str_jump_wait_land },
    { ai_move }
};
const mmove_t MMOVE_T(dog_str_move_jump_up) = { FRAME_jump01, FRAME_jump08, dog_str_frames_jump_up, dog_str_run };

static const mframe_t dog_str_frames_jump_down[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, dog_str_jump_down },
    { ai_move },
    { ai_move },
    { ai_move, 0, dog_str_jump_wait_land },
    { ai_move }
};
const mmove_t MMOVE_T(dog_str_move_jump_down) = { FRAME_jump01, FRAME_jump08, dog_str_frames_jump_down, dog_str_run };

static void dog_str_jump(edict_t *self, blocked_jump_result_t result)
{
    if (!self->enemy)
        return;

    if (result == JUMP_UP)
        M_SetAnimation(self, &dog_str_move_jump_up);
    else
        M_SetAnimation(self, &dog_str_move_jump_down);
}

/*
===
Blocked
===
*/
bool MONSTERINFO_BLOCKED(dog_str_blocked)(edict_t *self, float dist)
{
    blocked_jump_result_t result = blocked_checkjump(self, dist);
    if (result != JUMP_NONE) {
        if (result != JUMP_TURN)
            dog_str_jump(self, result);
        return true;
    }

    if (blocked_checkplat(self, dist))
        return true;

    return false;
}
// ROGUE
//================

/*
===
Death Stuff Starts
===
*/

static void dog_str_dead(edict_t *self)
{
    self->r.box.maxs.z = -8 * G_EntityScale(self);
    monster_dead(self);
}

static void dog_str_shrink(edict_t *self)
{
    self->r.box.maxs.z = 0;
    self->r.svflags |= SVF_DEADMONSTER;
    trap_LinkEntity(self);
}

static const mframe_t dog_str_frames_death[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, dog_str_shrink },
    { ai_move, 0, monster_footstep },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(dog_str_move_death) = { FRAME_death101, FRAME_death107, dog_str_frames_death, dog_str_dead };

static const gib_def_t dog_str_gibs[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 3 },
    { "models/monsters/parasite/gibs/chest.md2", 1, GIB_SKINNED },
    { "models/monsters/dogstrogg/gibs/g_arm.md2", 2, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/dogstrogg/gibs/g_leg.md2", 2, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/dogstrogg/gibs/g_head.md2", 1, GIB_SKINNED | GIB_HEAD },
    { 0 }
};

void DIE(dog_str_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    // check for gib
    if (M_CheckGib(self, mod)) {
        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        self->s.skinnum /= 2;
        ThrowGibs(self, damage, dog_str_gibs);
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;

    // regular death
    G_StartSound(self, CHAN_VOICE, sound_die, 1, ATTN_NORM);
    self->deadflag = true;
    self->takedamage = true;
    M_SetAnimation(self, &dog_str_move_death);
}

/*
===
End Death Stuff
===
*/

static void dog_str_skipframe(edict_t *self)
{
    self->monsterinfo.nextframe = FRAME_pain105;
}

static const mframe_t dog_str_frames_pain1[] = {
    { ai_move },
    { ai_move },
    { ai_move, 0, dog_str_skipframe },
    { ai_move, 0, monster_footstep },
    { ai_move },
    { ai_move },
    { ai_move, 6, monster_footstep },
    { ai_move, 16 },
    { ai_move, -6, monster_footstep },
    { ai_move, -7 },
    { ai_move }
};
const mmove_t MMOVE_T(dog_str_move_pain1) = { FRAME_pain101, FRAME_pain111, dog_str_frames_pain1, dog_str_start_run };

void PAIN(dog_str_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + SEC(3);

    if (brandom())
        G_StartSound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM);
    else
        G_StartSound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM);

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    M_SetAnimation(self, &dog_str_move_pain1);
}

void MONSTERINFO_SETSKIN(dog_str_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

void PR_monster_dog_strogg(void)
{
    sound_tap = G_SoundIndex("parasite/paridle1.wav");
    sound_scratch = G_SoundIndex("parasite/paridle2.wav");
    sound_search = G_SoundIndex("parasite/parsrch1.wav");
    sound_melee = G_SoundIndex("dog/dattack1_s.wav");
    sound_die = G_SoundIndex("dog/ddeath_s.wav");
    sound_pain1 = G_SoundIndex("dog/dpain1_s.wav");
    sound_pain2 = G_SoundIndex("dog/dpain1_s.wav");
    sound_sight = G_SoundIndex("dog/dsight_s.wav");
}

#define SPAWNFLAG_DOG_STR_NO_JUMPING   8

void SP_monster_dog_strogg(edict_t *self)
{
    self->s.modelindex = G_ModelIndex("models/monsters/dogstrogg/tris.md2");

    G_PrecacheGibs(dog_str_gibs);

    self->movetype = MOVETYPE_STEP;
    self->r.box = Box3_FromSize(16, -24, 24);
    self->r.solid = SOLID_BBOX;

    self->health = 35 * st.health_multiplier;
    self->gib_health = -50;
    self->mass = 250;
    self->yaw_speed = 30;

    self->pain = dog_str_pain;
    self->die = dog_str_die;

    self->monsterinfo.stand = dog_str_stand;
    self->monsterinfo.walk = dog_str_start_walk;
    self->monsterinfo.run = dog_str_start_run;
    self->monsterinfo.melee = dog_str_melee;
    self->monsterinfo.attack = dog_str_leap;
    self->monsterinfo.sight = dog_str_sight;
    self->monsterinfo.idle = dog_str_idle;
    self->monsterinfo.blocked = dog_str_blocked; // PGM
    self->monsterinfo.setskin = dog_str_setskin;

    M_SetAnimation(self, &dog_str_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;
    self->monsterinfo.can_jump = !(self->spawnflags & SPAWNFLAG_DOG_STR_NO_JUMPING);
    self->monsterinfo.drop_height = 256;
    self->monsterinfo.jump_height = 68;

    walkmonster_start(self);
}
