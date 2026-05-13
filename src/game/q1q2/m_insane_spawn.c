// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

insane_spawn

==============================================================================
*/
#include "g_local.h"
#include "m_insane_spawn.h"

#define SPAWNFLAG_INSANE_SPAWN_ALWAYS_STAND 32

static int sound_fist;
static int sound_shake;
static int sound_moan;
static int sound_scream[8];
static int sound_death;
static int sound_hit;
static int sound_jump;

static void insane_spawn_fist(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, sound_fist, 1, ATTN_IDLE);
}

static void insane_spawn_shake(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, sound_shake, 1, ATTN_IDLE);
}

static void insane_spawn_moan(edict_t *self)
{
    // Paril: don't moan every second
    if (self->monsterinfo.attack_finished < level.time) {
        G_StartSound(self, CHAN_VOICE, sound_moan, 1, ATTN_IDLE);
        self->monsterinfo.attack_finished = level.time + random_time_sec(1, 3);
    }
}

static void insane_spawn_scream(edict_t *self)
{
    // Paril: don't moan every second
    if (self->monsterinfo.attack_finished < level.time) {
        G_StartSound(self, CHAN_VOICE, random_element(sound_scream), 1, ATTN_IDLE);
        self->monsterinfo.attack_finished = level.time + random_time_sec(1, 3);
    }
}

void insane_spawn_stand(edict_t *self);
void insane_spawn_walk(edict_t *self);
void insane_spawn_run(edict_t *self);

static void insane_spawn_checkdown(edict_t *self);
static void insane_spawn_checkup(edict_t *self);
static void insane_spawn_onground(edict_t *self);

// Attack
static void insane_spawn_attack_slam(edict_t *self)
{
    vec3_t f, r, start;
    AngleVectors(self->s.angles, &f, &r, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_GENERIC_SLAM], f, r);
    trace_t tr = G_TraceLine(self->s.origin, start, self->s.number, MASK_SOLID);

    G_AddEvent(self, EV_MUZZLEFLASH2, MZ2_GENERIC_SLAM);
    self->gravity = 1.0f;
    self->velocity = vec3_origin;
    self->flags |= FL_KILL_VELOCITY;

    T_SlamRadiusDamage(tr.endpos, self, self, 10, 300, self, self->classname, 165, MOD_UNKNOWN);
}

void TOUCH(insane_spawn_jump_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    if (self->health <= 0) {
        self->touch = NULL;
        return;
    }

    if (self->groundentity) {
        self->s.frame = FRAME_jump9;

        if (self->touch)
            insane_spawn_attack_slam(self);

        self->touch = NULL;
    }
}

static void insane_spawn_high_gravity(edict_t *self)
{
    if (self->velocity.z < 0)
        self->gravity = 2.25f * (800.0f / level.gravity);
    else
        self->gravity = 5.25f * (800.0f / level.gravity);
}

static void insane_spawn_jump_takeoff(edict_t *self)
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
    self->velocity.z = 400;
    self->groundentity = NULL;
    self->monsterinfo.aiflags |= AI_DUCKED;
    self->monsterinfo.attack_finished = level.time + SEC(3);
    self->touch = insane_spawn_jump_touch;
    insane_spawn_high_gravity(self);
}

static void insane_spawn_check_landing(edict_t *self)
{
    insane_spawn_high_gravity(self);

    if (self->groundentity) {
        self->monsterinfo.attack_finished = 0;
        self->monsterinfo.aiflags &= ~AI_DUCKED;
        self->s.frame = FRAME_jump9;
        if (self->touch) {
            insane_spawn_attack_slam(self);
            self->touch = NULL;
        }
        self->flags &= ~FL_KILL_VELOCITY;
        return;
    }

    if (level.time > self->monsterinfo.attack_finished)
        self->monsterinfo.nextframe = FRAME_jump5;
    else
        self->monsterinfo.nextframe = FRAME_jump7;
}

static void insane_spawn_check_reattack(edict_t *self)
{
    if (self->health < 50) {
        if (self->groundentity)
            self->monsterinfo.nextframe = FRAME_jump1;
        else
            self->monsterinfo.nextframe = FRAME_jump9;
    }
}

static const mframe_t insane_spawn_frames_jump[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, insane_spawn_jump_takeoff },
    { ai_move, 0, insane_spawn_high_gravity },
    { ai_move, 0, insane_spawn_check_landing },
    { ai_move, 0, monster_footstep },
    { ai_move },
    { ai_move, 0, insane_spawn_check_reattack }
};
const mmove_t MMOVE_T(insane_spawn_move_jump) = { FRAME_jump1, FRAME_jump9, insane_spawn_frames_jump, insane_spawn_run };

void MONSTERINFO_ATTACK(insane_spawn_attack)(edict_t *self)
{
    G_StartSound(self, CHAN_WEAPON, sound_jump, 1, ATTN_NORM);
    M_SetAnimation(self, &insane_spawn_move_jump);
}

static const mframe_t insane_spawn_frames_stand_normal[] = {
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand, 0, insane_spawn_checkdown }
};
const mmove_t MMOVE_T(insane_spawn_move_stand_normal) = { FRAME_stand60, FRAME_stand65, insane_spawn_frames_stand_normal, insane_spawn_stand };

static const mframe_t insane_spawn_frames_stand_insane[] = {
    { ai_stand, 0, insane_spawn_shake },
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
    { ai_stand, 0, insane_spawn_checkdown }
};
const mmove_t MMOVE_T(insane_spawn_move_stand_insane) = { FRAME_stand65, FRAME_stand94, insane_spawn_frames_stand_insane, insane_spawn_stand };

static const mframe_t insane_spawn_frames_uptodown[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, insane_spawn_moan },
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

    { ai_move, 2.7f },
    { ai_move, 4.1f },
    { ai_move, 6 },
    { ai_move, 7.6f },
    { ai_move, 3.6f },
    { ai_move },
    { ai_move },
    { ai_move, 0, insane_spawn_fist },
    { ai_move },
    { ai_move },

    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, insane_spawn_fist },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(insane_spawn_move_uptodown) = { FRAME_stand1, FRAME_stand40, insane_spawn_frames_uptodown, insane_spawn_onground };

static const mframe_t insane_spawn_frames_downtoup[] = {
    { ai_move, -0.7f }, // 41
    { ai_move, -1.2f }, // 42
    { ai_move, -1.5f }, // 43
    { ai_move, -4.5f }, // 44
    { ai_move, -3.5f }, // 45
    { ai_move, -0.2f }, // 46
    { ai_move },        // 47
    { ai_move, -1.3f }, // 48
    { ai_move, -3 },    // 49
    { ai_move, -2 },    // 50
    { ai_move },        // 51
    { ai_move },        // 52
    { ai_move },        // 53
    { ai_move, -3.3f }, // 54
    { ai_move, -1.6f }, // 55
    { ai_move, -0.3f }, // 56
    { ai_move },        // 57
    { ai_move },        // 58
    { ai_move }         // 59
};
const mmove_t MMOVE_T(insane_spawn_move_downtoup) = { FRAME_stand41, FRAME_stand59, insane_spawn_frames_downtoup, insane_spawn_stand };

static const mframe_t insane_spawn_frames_jumpdown[] = {
    { ai_move, 0.2f },
    { ai_move, 11.5f },
    { ai_move, 5.1f },
    { ai_move, 7.1f },
    { ai_move }
};
const mmove_t MMOVE_T(insane_spawn_move_jumpdown) = { FRAME_stand96, FRAME_stand100, insane_spawn_frames_jumpdown, insane_spawn_onground };

static const mframe_t insane_spawn_frames_down[] = {
    { ai_move }, // 100
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }, // 110
    { ai_move, -1.7f },
    { ai_move, -1.6f },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, insane_spawn_fist },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }, // 120
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }, // 130
    { ai_move },
    { ai_move },
    { ai_move, 0, insane_spawn_moan },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }, // 140
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }, // 150
    { ai_move, 0.5f },
    { ai_move },
    { ai_move, -0.2f, insane_spawn_scream },
    { ai_move },
    { ai_move, 0.2f },
    { ai_move, 0.4f },
    { ai_move, 0.6f },
    { ai_move, 0.8f },
    { ai_move, 0.7f },
    { ai_move, 0, insane_spawn_checkup } // 160
};
const mmove_t MMOVE_T(insane_spawn_move_down) = { FRAME_stand100, FRAME_stand160, insane_spawn_frames_down, insane_spawn_onground };

static const mframe_t insane_spawn_frames_walk_normal[] = {
    { ai_walk, 0, insane_spawn_scream },
    { ai_walk, 2.5f },
    { ai_walk, 3.5f },
    { ai_walk, 1.7f },
    { ai_walk, 2.3f },
    { ai_walk, 2.4f },
    { ai_walk, 2.2f, monster_footstep },
    { ai_walk, 4.2f },
    { ai_walk, 5.6f },
    { ai_walk, 3.3f },
    { ai_walk, 2.4f },
    { ai_walk, 0.9f },
    { ai_walk, 0, monster_footstep }
};
const mmove_t MMOVE_T(insane_spawn_move_walk_normal) = { FRAME_walk27, FRAME_walk39, insane_spawn_frames_walk_normal, insane_spawn_walk };

static const mframe_t insane_spawn_frames_walk_insane[] = {
    { ai_walk, 0, insane_spawn_scream }, // walk 1
    { ai_walk, 3.4f },             // walk 2
    { ai_walk, 3.6f },             // 3
    { ai_walk, 2.9f },             // 4
    { ai_walk, 2.2f },             // 5
    { ai_walk, 2.6f, monster_footstep },               // 6
    { ai_walk },                   // 7
    { ai_walk, 0.7f },             // 8
    { ai_walk, 4.8f },             // 9
    { ai_walk, 5.3f },             // 10
    { ai_walk, 1.1f },             // 11
    { ai_walk, 2, monster_footstep },                  // 12
    { ai_walk, 0.5f },             // 13
    { ai_walk },                   // 14
    { ai_walk },                   // 15
    { ai_walk, 4.9f },             // 16
    { ai_walk, 6.7f },             // 17
    { ai_walk, 3.8f },             // 18
    { ai_walk, 2, monster_footstep },                  // 19
    { ai_walk, 0.2f },             // 20
    { ai_walk },                   // 21
    { ai_walk, 3.4f },             // 22
    { ai_walk, 6.4f },             // 23
    { ai_walk, 5 },                // 24
    { ai_walk, 1.8f, monster_footstep },               // 25
    { ai_walk }                    // 26
};
const mmove_t MMOVE_T(insane_spawn_move_walk_insane) = { FRAME_walk1, FRAME_walk26, insane_spawn_frames_walk_insane, insane_spawn_walk };

static const mframe_t insane_spawn_frames_stand_pain[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, monster_footstep },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, monster_footstep }
};
const mmove_t MMOVE_T(insane_spawn_move_stand_pain) = { FRAME_st_pain2, FRAME_st_pain12, insane_spawn_frames_stand_pain, insane_spawn_run };

void MONSTERINFO_WALK(insane_spawn_walk)(edict_t *self)
{
    if (frandom())
        M_SetAnimation(self, &insane_spawn_move_walk_normal);
    else
        M_SetAnimation(self, &insane_spawn_move_walk_insane);
}

static const mframe_t insane_spawn_frames_run[] = {
    { ai_run, 30, insane_spawn_scream },
    { ai_run, 15 },
    { ai_run, 12, monster_footstep },
    { ai_run, 15 },
    { ai_run, 25 },
    { ai_run, 15 },
    { ai_run, 12, monster_footstep },
    { ai_run, 15 },
    { ai_run, 25 }
};
const mmove_t MMOVE_T(insane_spawn_move_run) = { FRAME_run1, FRAME_run9, insane_spawn_frames_run, NULL};

void MONSTERINFO_RUN(insane_spawn_run)(edict_t *self)
{
    if (self->health < 50) {
        self->monsterinfo.attack_finished = 0;
        M_SetAnimation(self, &insane_spawn_move_jump);
    } else {
        M_SetAnimation(self, &insane_spawn_move_run);
    }
}

void PAIN(insane_spawn_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (level.time < self->fly_sound_debounce_time)
        return;

    self->fly_sound_debounce_time = level.time + FRAME_TIME;
    G_StartSound(self, CHAN_VOICE, sound_hit, 1, ATTN_NORM);

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    if (((self->s.frame >= FRAME_stand99) && (self->s.frame <= FRAME_stand160)) ||
        ((self->s.frame >= FRAME_stand1 && self->s.frame <= FRAME_stand40)))
        return; // no crawl pain animation for insane spawn

    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + SEC(3);
    M_SetAnimation(self, &insane_spawn_move_stand_pain);
}

static void insane_spawn_onground(edict_t *self)
{
    M_SetAnimation(self, &insane_spawn_move_down);
}

static void insane_spawn_checkdown(edict_t *self)
{
    if (self->spawnflags & SPAWNFLAG_INSANE_SPAWN_ALWAYS_STAND) // Always stand
        return;

    if (frandom() < 0.3f) {
        if (brandom())
            M_SetAnimation(self, &insane_spawn_move_uptodown);
        else
            M_SetAnimation(self, &insane_spawn_move_jumpdown);
    }
}

static void insane_spawn_checkup(edict_t *self)
{
    if (brandom())
        M_SetAnimation(self, &insane_spawn_move_downtoup);
}

void MONSTERINFO_STAND(insane_spawn_stand)(edict_t *self)
{
    if (brandom())
        M_SetAnimation(self, &insane_spawn_move_stand_normal);
    else
        M_SetAnimation(self, &insane_spawn_move_stand_insane);
}

static const gib_def_t insane_spawn_gibs[] = {
    { "models/monsters/insane_spawn/gibs/g_arm.md2", 2 },
    { "models/monsters/insane_spawn/gibs/g_leg.md2", 2 },
    { "models/monsters/insane_spawn/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

void DIE(insane_spawn_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    G_AddEvent(self, EV_ROCKET_EXPLOSION, 0);

    T_RadiusDamage(self, attacker, 500, self, self->classname, 100, DAMAGE_NONE, MOD_EXPLOSIVE);

    G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_IDLE);

    ThrowGibs(self, damage, insane_spawn_gibs);
    self->deadflag = true;
}

void MONSTERINFO_SETSKIN(insane_spawn_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

void PR_monster_insane_spawn(void)
{
    sound_fist = G_SoundIndex("insanespawn/insane11.wav");
    sound_hit = G_SoundIndex("insanespawn/hit1.wav");
    sound_shake = G_SoundIndex("insanespawn/insane5.wav");
    sound_moan = G_SoundIndex("insanespawn/insane7.wav");
    sound_scream[0] = G_SoundIndex("insanespawn/insane1.wav");
    sound_scream[1] = G_SoundIndex("insanespawn/insane2.wav");
    sound_scream[2] = G_SoundIndex("insanespawn/insane3.wav");
    sound_scream[3] = G_SoundIndex("insanespawn/insane4.wav");
    sound_scream[4] = G_SoundIndex("insanespawn/insane6.wav");
    sound_scream[5] = G_SoundIndex("insanespawn/insane8.wav");
    sound_scream[6] = G_SoundIndex("insanespawn/insane9.wav");
    sound_scream[7] = G_SoundIndex("insanespawn/insane10.wav");
    sound_death = G_SoundIndex("insanespawn/insane12.wav");
    sound_jump = G_SoundIndex("berserk/jump.wav");
    G_SoundIndex("mutant/thud1.wav");
    G_SoundIndex("world/explod2.wav");
}

void SP_monster_insane_spawn(edict_t *self)
{
    G_PrecacheGibs(insane_spawn_gibs);

    self->s.modelindex = G_ModelIndex("models/monsters/insane_spawn/tris.md2");
    self->r.box = Box3_FromSize(16, -24, 32);
    self->r.solid = SOLID_BBOX;
    self->movetype = MOVETYPE_STEP;

    self->health = 100 * st.health_multiplier;
    self->gib_health = -50;
    self->mass = 300;

    self->pain = insane_spawn_pain;
    self->die = insane_spawn_die;

    self->monsterinfo.stand = insane_spawn_stand;
    self->monsterinfo.walk = insane_spawn_walk;
    self->monsterinfo.run = insane_spawn_run;
    self->monsterinfo.attack = insane_spawn_attack;
    self->monsterinfo.setskin = insane_spawn_setskin;

    trap_LinkEntity(self);

    M_SetAnimation(self, &insane_spawn_move_stand_normal);
    self->monsterinfo.scale = MODEL_SCALE;

    walkmonster_start(self);
}
