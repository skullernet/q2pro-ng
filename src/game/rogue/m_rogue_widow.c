// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

black widow

==============================================================================
*/

// self->timestamp used to prevent rapid fire of railgun
// self->plat2flags used for fire count (flashes)

#include "g_local.h"
#include "m_rogue_widow.h"

#define RAIL_TIME           SEC(3)
#define BLASTER_TIME        SEC(2)
#define BLASTER2_DAMAGE     10
#define WIDOW_RAIL_DAMAGE   50

static int sound_pain1;
static int sound_pain2;
static int sound_pain3;
static int sound_rail;

static unsigned shotsfired;

static const vec3_t spawnpoints[] = {
    { 30, 100, 16 },
    { 30, -100, 16 }
};

static const vec3_t beameffects[] = {
    { 12.58f, -43.71f, 68.88f },
    { 3.43f, 58.72f, 68.41f }
};

static const float sweep_angles[] = {
    32, 26, 20, 10, 0, -6.5f, -13, -27, -41
};

static const vec3_t stalker_mins = { -28, -28, -18 };
static const vec3_t stalker_maxs = { 28, 28, 18 };

static unsigned int widow_damage_multiplier;

void widow_run(edict_t *self);
static void widow_attack_blaster(edict_t *self);
static void widow_reattack_blaster(edict_t *self);

void widow_start_spawn(edict_t *self);
void widow_done_spawn(edict_t *self);
static void widow_spawn_check(edict_t *self);
static void widow_attack_rail(edict_t *self);

//static void widow_start_run_5(edict_t *self);
//static void widow_start_run_10(edict_t *self);
static void widow_start_run_12(edict_t *self);

void WidowCalcSlots(edict_t *self);
void WidowPowerups(edict_t *self);

void MONSTERINFO_SEARCH(widow_search)(edict_t *self)
{
}

void MONSTERINFO_SIGHT(widow_sight)(edict_t *self, edict_t *other)
{
    self->monsterinfo.fire_wait = 0;
}

const mmove_t widow_move_attack_post_blaster;
const mmove_t widow_move_attack_post_blaster_r;
const mmove_t widow_move_attack_post_blaster_l;
const mmove_t widow_move_attack_blaster;

static float target_angle(edict_t *self)
{
    vec3_t target;
    float  enemy_yaw;

    VectorSubtract(self->s.origin, self->enemy->s.origin, target);
    enemy_yaw = self->s.angles[YAW] - vectoyaw(target);
    if (enemy_yaw < 0)
        enemy_yaw += 360.0f;

    // this gets me 0 degrees = forward
    enemy_yaw -= 180.0f;
    // positive is to right, negative to left

    return enemy_yaw;
}

static int WidowTorso(edict_t *self)
{
    float enemy_yaw = target_angle(self);

    if (enemy_yaw >= 105) {
        M_SetAnimation(self, &widow_move_attack_post_blaster_r);
        self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
        return 0;
    }

    if (enemy_yaw <= -75.0f) {
        M_SetAnimation(self, &widow_move_attack_post_blaster_l);
        self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
        return 0;
    }

    for (int i = 0; i < 18; i++)
        if (enemy_yaw >= 95 - 10 * i)
            return FRAME_fired03 + i;

    return 0;
}

#define VARIANCE    15.0f

static void WidowBlaster(edict_t *self)
{
    vec3_t                   forward, right, target, vec, targ_angles;
    vec3_t                   start;
    monster_muzzleflash_id_t flashnum;
    effects_t                effect;

    if (!self->enemy)
        return;

    shotsfired++;
    if (!(shotsfired % 4))
        effect = EF_BLASTER;
    else
        effect = EF_NONE;

    AngleVectors(self->s.angles, forward, right, NULL);
    if ((self->s.frame >= FRAME_spawn05) && (self->s.frame <= FRAME_spawn13)) {
        // sweep
        flashnum = MZ2_WIDOW_BLASTER_SWEEP1 + self->s.frame - FRAME_spawn05;
        G_ProjectSource(self->s.origin, monster_flash_offset[flashnum], forward, right, start);
        VectorSubtract(self->enemy->s.origin, start, target);
        vectoangles(target, targ_angles);

        VectorCopy(self->s.angles, vec);
        vec[PITCH] += targ_angles[PITCH];
        vec[YAW] -= sweep_angles[flashnum - MZ2_WIDOW_BLASTER_SWEEP1];

        AngleVectors(vec, forward, NULL, NULL);
        monster_fire_blaster2(self, start, forward, BLASTER2_DAMAGE * widow_damage_multiplier, 1000, flashnum, effect);
    } else if ((self->s.frame >= FRAME_fired02a) && (self->s.frame <= FRAME_fired20)) {
        vec3_t angles;
        float  aim_angle, target_angle;
        float  error;

        self->monsterinfo.aiflags |= AI_MANUAL_STEERING;

        self->monsterinfo.nextframe = WidowTorso(self);

        if (!self->monsterinfo.nextframe)
            self->monsterinfo.nextframe = self->s.frame;

        if (self->s.frame == FRAME_fired02a)
            flashnum = MZ2_WIDOW_BLASTER_0;
        else
            flashnum = MZ2_WIDOW_BLASTER_100 + self->s.frame - FRAME_fired03;

        G_ProjectSource(self->s.origin, monster_flash_offset[flashnum], forward, right, start);

        PredictAim(self, self->enemy, start, 1000, true, crandom() * 0.1f, forward, NULL);

        // clamp it to within 10 degrees of the aiming angle (where she's facing)
        vectoangles(forward, angles);
        // give me 100 -> -70
        aim_angle = (float)(100 - (10 * (flashnum - MZ2_WIDOW_BLASTER_100)));
        if (aim_angle <= 0)
            aim_angle += 360;
        target_angle = self->s.angles[YAW] - angles[YAW];
        if (target_angle <= 0)
            target_angle += 360;

        error = aim_angle - target_angle;

        // positive error is to entity's left, aka positive direction in engine
        // unfortunately, I decided that for the aim_angle, positive was right.  *sigh*
        if (error > VARIANCE) {
            angles[YAW] = (self->s.angles[YAW] - aim_angle) + VARIANCE;
            AngleVectors(angles, forward, NULL, NULL);
        } else if (error < -VARIANCE) {
            angles[YAW] = (self->s.angles[YAW] - aim_angle) - VARIANCE;
            AngleVectors(angles, forward, NULL, NULL);
        }

        monster_fire_blaster2(self, start, forward, BLASTER2_DAMAGE * widow_damage_multiplier, 1000, flashnum, effect);
    } else if ((self->s.frame >= FRAME_run01) && (self->s.frame <= FRAME_run08)) {
        flashnum = MZ2_WIDOW_RUN_1 + self->s.frame - FRAME_run01;
        G_ProjectSource(self->s.origin, monster_flash_offset[flashnum], forward, right, start);

        VectorSubtract(self->enemy->s.origin, start, target);
        target[2] += self->enemy->viewheight;
        VectorNormalize(target);

        monster_fire_blaster2(self, start, target, BLASTER2_DAMAGE * widow_damage_multiplier, 1000, flashnum, effect);
    }
}

static void WidowSpawn(edict_t *self)
{
    vec3_t   f, r, u, startpoint, spawnpoint;
    edict_t *ent, *designated_enemy;
    int      i;

    AngleVectors(self->s.angles, f, r, u);

    for (i = 0; i < 2; i++) {
        G_ProjectSource2(self->s.origin, spawnpoints[i], f, r, u, startpoint);

        if (!FindSpawnPoint(startpoint, stalker_mins, stalker_maxs, spawnpoint, 64, true))
            continue;

        ent = CreateGroundMonster(spawnpoint, self->s.angles, stalker_mins, stalker_maxs, "monster_stalker", 256);
        if (!ent)
            continue;

        self->monsterinfo.monster_used++;
        ent->monsterinfo.commander = self;
        ent->monsterinfo.slots_from_commander = 1;

        ent->nextthink = level.time;
        ent->think(ent);

        ent->monsterinfo.aiflags |= AI_SPAWNED_COMMANDER | AI_DO_NOT_COUNT | AI_IGNORE_SHOTS;

        if (!coop.integer) {
            designated_enemy = self->enemy;
        } else {
            designated_enemy = PickCoopTarget(ent);
            if (designated_enemy) {
                // try to avoid using my enemy
                if (designated_enemy == self->enemy) {
                    designated_enemy = PickCoopTarget(ent);
                    if (!designated_enemy)
                        designated_enemy = self->enemy;
                }
            } else
                designated_enemy = self->enemy;
        }

        if ((designated_enemy->r.inuse) && (designated_enemy->health > 0)) {
            ent->enemy = designated_enemy;
            FoundTarget(ent);
            ent->monsterinfo.attack(ent);
        }
    }
}

static void widow_spawn_check(edict_t *self)
{
    WidowBlaster(self);
    WidowSpawn(self);
}

static void widow_ready_spawn(edict_t *self)
{
    vec3_t f, r, u, mid, startpoint, spawnpoint;
    int    i;

    WidowBlaster(self);
    AngleVectors(self->s.angles, f, r, u);

    VectorAvg(stalker_mins, stalker_maxs, mid); // FIXME
    float radius = Distance(stalker_maxs, stalker_mins) * 0.5f;

    for (i = 0; i < 2; i++) {
        G_ProjectSource2(self->s.origin, spawnpoints[i], f, r, u, startpoint);
        if (FindSpawnPoint(startpoint, stalker_mins, stalker_maxs, spawnpoint, 64, true)) {
            VectorAdd(spawnpoint, mid, spawnpoint);
            SpawnGrow_Spawn(spawnpoint, radius, radius * 2);
        }
    }
}

static void widow_step(edict_t *self)
{
    G_StartSound(self, CHAN_BODY, G_SoundIndex("widow/bwstep3.wav"), 1, ATTN_NORM);
}

static const mframe_t widow_frames_stand[] = {
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
const mmove_t MMOVE_T(widow_move_stand) = { FRAME_idle01, FRAME_idle11, widow_frames_stand, NULL };

static const mframe_t widow_frames_walk[] = {
    { ai_walk, 2.79f, widow_step },
    { ai_walk, 2.77f },
    { ai_walk, 3.53f },
    { ai_walk, 3.97f },
    { ai_walk, 4.13f }, // 5
    { ai_walk, 4.09f },
    { ai_walk, 3.84f },
    { ai_walk, 3.62f, widow_step },
    { ai_walk, 3.29f },
    { ai_walk, 6.08f }, // 10
    { ai_walk, 6.94f },
    { ai_walk, 5.73f },
    { ai_walk, 2.85f }
};
const mmove_t MMOVE_T(widow_move_walk) = { FRAME_walk01, FRAME_walk13, widow_frames_walk, NULL };

static const mframe_t widow_frames_run[] = {
    { ai_run, 2.79f, widow_step },
    { ai_run, 2.77f },
    { ai_run, 3.53f },
    { ai_run, 3.97f },
    { ai_run, 4.13f }, // 5
    { ai_run, 4.09f },
    { ai_run, 3.84f },
    { ai_run, 3.62f, widow_step },
    { ai_run, 3.29f },
    { ai_run, 6.08f }, // 10
    { ai_run, 6.94f },
    { ai_run, 5.73f },
    { ai_run, 2.85f }
};
const mmove_t MMOVE_T(widow_move_run) = { FRAME_walk01, FRAME_walk13, widow_frames_run, NULL };

static void widow_stepshoot(edict_t *self)
{
    G_StartSound(self, CHAN_BODY, G_SoundIndex("widow/bwstep2.wav"), 1, ATTN_NORM);
    WidowBlaster(self);
}

static const mframe_t widow_frames_run_attack[] = {
    { ai_charge, 13, widow_stepshoot },
    { ai_charge, 11.72f, WidowBlaster },
    { ai_charge, 18.04f, WidowBlaster },
    { ai_charge, 14.58f, WidowBlaster },
    { ai_charge, 13, widow_stepshoot }, // 5
    { ai_charge, 12.12f, WidowBlaster },
    { ai_charge, 19.63f, WidowBlaster },
    { ai_charge, 11.37f, WidowBlaster }
};
const mmove_t MMOVE_T(widow_move_run_attack) = { FRAME_run01, FRAME_run08, widow_frames_run_attack, widow_run };

//
// These three allow specific entry into the run sequence
//

#if 0
static void widow_start_run_5(edict_t *self)
{
    M_SetAnimation(self, &widow_move_run);
    self->monsterinfo.nextframe = FRAME_walk05;
}

static void widow_start_run_10(edict_t *self)
{
    M_SetAnimation(self, &widow_move_run);
    self->monsterinfo.nextframe = FRAME_walk10;
}
#endif

static void widow_start_run_12(edict_t *self)
{
    M_SetAnimation(self, &widow_move_run);
    self->monsterinfo.nextframe = FRAME_walk12;
}

static const mframe_t widow_frames_attack_pre_blaster[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, widow_attack_blaster }
};
const mmove_t MMOVE_T(widow_move_attack_pre_blaster) = { FRAME_fired01, FRAME_fired02a, widow_frames_attack_pre_blaster, NULL };

// Loop this
static const mframe_t widow_frames_attack_blaster[] = {
    { ai_charge, 0, widow_reattack_blaster }, // straight ahead
    { ai_charge, 0, widow_reattack_blaster }, // 100 degrees right
    { ai_charge, 0, widow_reattack_blaster },
    { ai_charge, 0, widow_reattack_blaster },
    { ai_charge, 0, widow_reattack_blaster },
    { ai_charge, 0, widow_reattack_blaster },
    { ai_charge, 0, widow_reattack_blaster }, // 50 degrees right
    { ai_charge, 0, widow_reattack_blaster },
    { ai_charge, 0, widow_reattack_blaster },
    { ai_charge, 0, widow_reattack_blaster },
    { ai_charge, 0, widow_reattack_blaster },
    { ai_charge, 0, widow_reattack_blaster }, // straight
    { ai_charge, 0, widow_reattack_blaster },
    { ai_charge, 0, widow_reattack_blaster },
    { ai_charge, 0, widow_reattack_blaster },
    { ai_charge, 0, widow_reattack_blaster },
    { ai_charge, 0, widow_reattack_blaster }, // 50 degrees left
    { ai_charge, 0, widow_reattack_blaster },
    { ai_charge, 0, widow_reattack_blaster } // 70 degrees left
};
const mmove_t MMOVE_T(widow_move_attack_blaster) = { FRAME_fired02a, FRAME_fired20, widow_frames_attack_blaster, NULL };

static const mframe_t widow_frames_attack_post_blaster[] = {
    { ai_charge },
    { ai_charge }
};
const mmove_t MMOVE_T(widow_move_attack_post_blaster) = { FRAME_fired21, FRAME_fired22, widow_frames_attack_post_blaster, widow_run };

static const mframe_t widow_frames_attack_post_blaster_r[] = {
    { ai_charge, -2 },
    { ai_charge, -10 },
    { ai_charge, -2 },
    { ai_charge },
    { ai_charge, 0, widow_start_run_12 }
};
const mmove_t MMOVE_T(widow_move_attack_post_blaster_r) = { FRAME_transa01, FRAME_transa05, widow_frames_attack_post_blaster_r, NULL };

static const mframe_t widow_frames_attack_post_blaster_l[] = {
    { ai_charge },
    { ai_charge, 14 },
    { ai_charge, -2 },
    { ai_charge, 10 },
    { ai_charge, 10, widow_start_run_12 }
};
const mmove_t MMOVE_T(widow_move_attack_post_blaster_l) = { FRAME_transb01, FRAME_transb05, widow_frames_attack_post_blaster_l, NULL };

const mmove_t widow_move_attack_rail;
const mmove_t widow_move_attack_rail_l;
const mmove_t widow_move_attack_rail_r;

static void WidowRail(edict_t *self)
{
    vec3_t                   start;
    vec3_t                   dir;
    vec3_t                   forward, right;
    monster_muzzleflash_id_t flash;

    AngleVectors(self->s.angles, forward, right, NULL);

    if (self->monsterinfo.active_move == &widow_move_attack_rail_l)
        flash = MZ2_WIDOW_RAIL_LEFT;
    else if (self->monsterinfo.active_move == &widow_move_attack_rail_r)
        flash = MZ2_WIDOW_RAIL_RIGHT;
    else
        flash = MZ2_WIDOW_RAIL;

     G_ProjectSource(self->s.origin, monster_flash_offset[flash], forward, right, start);

    // calc direction to where we targeted
    VectorSubtract(self->pos1, start, dir);
    VectorNormalize(dir);

    monster_fire_railgun(self, start, dir, WIDOW_RAIL_DAMAGE * widow_damage_multiplier, 100, flash);
    self->timestamp = level.time + RAIL_TIME;
}

static void WidowSaveLoc(edict_t *self)
{
    VectorCopy(self->enemy->s.origin, self->pos1); // save for aiming the shot
    self->pos1[2] += self->enemy->viewheight;
}

static void widow_start_rail(edict_t *self)
{
    self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
}

static void widow_rail_done(edict_t *self)
{
    self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
}

static const mframe_t widow_frames_attack_pre_rail[] = {
    { ai_charge, 0, widow_start_rail },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, widow_attack_rail }
};
const mmove_t MMOVE_T(widow_move_attack_pre_rail) = { FRAME_transc01, FRAME_transc04, widow_frames_attack_pre_rail, NULL };

static const mframe_t widow_frames_attack_rail[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, WidowSaveLoc },
    { ai_charge, -10, WidowRail },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, widow_rail_done }
};
const mmove_t MMOVE_T(widow_move_attack_rail) = { FRAME_firea01, FRAME_firea09, widow_frames_attack_rail, widow_run };

static const mframe_t widow_frames_attack_rail_r[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, WidowSaveLoc },
    { ai_charge, -10, WidowRail },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, widow_rail_done }
};
const mmove_t MMOVE_T(widow_move_attack_rail_r) = { FRAME_fireb01, FRAME_fireb09, widow_frames_attack_rail_r, widow_run };

static const mframe_t widow_frames_attack_rail_l[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, WidowSaveLoc },
    { ai_charge, -10, WidowRail },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, widow_rail_done }
};
const mmove_t MMOVE_T(widow_move_attack_rail_l) = { FRAME_firec01, FRAME_firec09, widow_frames_attack_rail_l, widow_run };

static void widow_attack_rail(edict_t *self)
{
    float enemy_angle;

    enemy_angle = target_angle(self);

    if (enemy_angle < -15)
        M_SetAnimation(self, &widow_move_attack_rail_l);
    else if (enemy_angle > 15)
        M_SetAnimation(self, &widow_move_attack_rail_r);
    else
        M_SetAnimation(self, &widow_move_attack_rail);
}

void widow_start_spawn(edict_t *self)
{
    self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
}

void widow_done_spawn(edict_t *self)
{
    self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
}

static const mframe_t widow_frames_spawn[] = {
    { ai_charge }, // 1
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, widow_start_spawn },
    { ai_charge },                       // 5
    { ai_charge, 0, WidowBlaster },      // 6
    { ai_charge, 0, widow_ready_spawn }, // 7
    { ai_charge, 0, WidowBlaster },
    { ai_charge, 0, WidowBlaster }, // 9
    { ai_charge, 0, widow_spawn_check },
    { ai_charge, 0, WidowBlaster }, // 11
    { ai_charge, 0, WidowBlaster },
    { ai_charge, 0, WidowBlaster }, // 13
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, widow_done_spawn }
};
const mmove_t MMOVE_T(widow_move_spawn) = { FRAME_spawn01, FRAME_spawn18, widow_frames_spawn, widow_run };

static const mframe_t widow_frames_pain_heavy[] = {
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
const mmove_t MMOVE_T(widow_move_pain_heavy) = { FRAME_pain01, FRAME_pain13, widow_frames_pain_heavy, widow_run };

static const mframe_t widow_frames_pain_light[] = {
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(widow_move_pain_light) = { FRAME_pain201, FRAME_pain203, widow_frames_pain_light, widow_run };

static void spawn_out_start(edict_t *self)
{
    vec3_t startpoint, f, r, u;

    AngleVectors(self->s.angles, f, r, u);

    G_ProjectSource2(self->s.origin, beameffects[0], f, r, u, startpoint);
    G_TempEntity(startpoint, EV_WIDOWBEAMOUT, 0);

    G_ProjectSource2(self->s.origin, beameffects[1], f, r, u, startpoint);
    G_TempEntity(startpoint, EV_WIDOWBEAMOUT, 0);

    G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/bwidowbeamout.wav"), 1, ATTN_NORM);
}

static void spawn_out_do(edict_t *self)
{
    vec3_t startpoint, f, r, u;

    AngleVectors(self->s.angles, f, r, u);
    G_ProjectSource2(self->s.origin, beameffects[0], f, r, u, startpoint);
    G_TempEntity(startpoint, EV_WIDOWSPLASH, 0);

    G_ProjectSource2(self->s.origin, beameffects[1], f, r, u, startpoint);
    G_TempEntity(startpoint, EV_WIDOWSPLASH, 0);

    VectorCopy(self->s.origin, startpoint);
    startpoint[2] += 36;
    G_TempEntity(startpoint, EV_BOSSTPORT, 0);

    Widowlegs_Spawn(self->s.origin, self->s.angles);

    G_FreeEdict(self);
}

static const mframe_t widow_frames_death[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }, // 5
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, spawn_out_start }, // 10
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }, // 15
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }, // 20
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }, // 25
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }, // 30
    { ai_move, 0, spawn_out_do }
};
const mmove_t MMOVE_T(widow_move_death) = { FRAME_death01, FRAME_death31, widow_frames_death, NULL };

static void widow_attack_kick(edict_t *self)
{
    vec3_t aim = { 100, 0, 4 };
    if (self->enemy->groundentity)
        fire_hit(self, aim, irandom2(50, 56), 500);
    else // not as much kick if they're in the air .. makes it harder to land on her head
        fire_hit(self, aim, irandom2(50, 56), 250);
}

static const mframe_t widow_frames_attack_kick[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, widow_attack_kick },
    { ai_move }, // 5
    { ai_move },
    { ai_move },
    { ai_move }
};

const mmove_t MMOVE_T(widow_move_attack_kick) = { FRAME_kick01, FRAME_kick08, widow_frames_attack_kick, widow_run };

void MONSTERINFO_STAND(widow_stand)(edict_t *self)
{
    G_StartSound(self, CHAN_WEAPON, G_SoundIndex("widow/laugh.wav"), 1, ATTN_NORM);
    M_SetAnimation(self, &widow_move_stand);
}

void MONSTERINFO_RUN(widow_run)(edict_t *self)
{
    self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;

    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
        M_SetAnimation(self, &widow_move_stand);
    else
        M_SetAnimation(self, &widow_move_run);
}

void MONSTERINFO_WALK(widow_walk)(edict_t *self)
{
    M_SetAnimation(self, &widow_move_walk);
}

void MONSTERINFO_ATTACK(widow_attack)(edict_t *self)
{
    float luck;
    bool  rail_frames = false, blaster_frames = false, blocked = false, anger = false;

    self->movetarget = NULL;

    if (self->monsterinfo.aiflags & AI_BLOCKED) {
        blocked = true;
        self->monsterinfo.aiflags &= ~AI_BLOCKED;
    }

    if (self->monsterinfo.aiflags & AI_TARGET_ANGER) {
        anger = true;
        self->monsterinfo.aiflags &= ~AI_TARGET_ANGER;
    }

    if ((!self->enemy) || (!self->enemy->r.inuse))
        return;

    if (self->bad_area) {
        if ((frandom() < 0.1f) || (level.time < self->timestamp))
            M_SetAnimation(self, &widow_move_attack_pre_blaster);
        else {
            G_StartSound(self, CHAN_WEAPON, sound_rail, 1, ATTN_NORM);
            M_SetAnimation(self, &widow_move_attack_pre_rail);
        }
        return;
    }

    // frames FRAME_walk13, FRAME_walk01, FRAME_walk02, FRAME_walk03 are rail gun start frames
    // frames FRAME_walk09, FRAME_walk10, FRAME_walk11, FRAME_walk12 are spawn & blaster start frames

    if ((self->s.frame == FRAME_walk13) || ((self->s.frame >= FRAME_walk01) && (self->s.frame <= FRAME_walk03)))
        rail_frames = true;

    if ((self->s.frame >= FRAME_walk09) && (self->s.frame <= FRAME_walk12))
        blaster_frames = true;

    WidowCalcSlots(self);

    // if we can't see the target, spawn stuff regardless of frame
    if ((self->monsterinfo.attack_state == AS_BLIND) && (M_SlotsLeft(self) >= 2)) {
        M_SetAnimation(self, &widow_move_spawn);
        return;
    }

    // accept bias towards spawning regardless of frame
    if (blocked && (M_SlotsLeft(self) >= 2)) {
        M_SetAnimation(self, &widow_move_spawn);
        return;
    }

    if ((realrange(self, self->enemy) > 300) && (!anger) && (brandom()) && (!blocked)) {
        M_SetAnimation(self, &widow_move_run_attack);
        return;
    }

    if (blaster_frames) {
        if (M_SlotsLeft(self) >= 2) {
            M_SetAnimation(self, &widow_move_spawn);
            return;
        }
        if (self->monsterinfo.fire_wait + BLASTER_TIME <= level.time) {
            M_SetAnimation(self, &widow_move_attack_pre_blaster);
            return;
        }
    }

    if (rail_frames) {
        if (!(level.time < self->timestamp)) {
            G_StartSound(self, CHAN_WEAPON, sound_rail, 1, ATTN_NORM);
            M_SetAnimation(self, &widow_move_attack_pre_rail);
        }
    }

    if ((rail_frames) || (blaster_frames))
        return;

    luck = frandom();
    if (M_SlotsLeft(self) >= 2) {
        if ((luck <= 0.40f) && (self->monsterinfo.fire_wait + BLASTER_TIME <= level.time))
            M_SetAnimation(self, &widow_move_attack_pre_blaster);
        else if ((luck <= 0.7f) && !(level.time < self->timestamp)) {
            G_StartSound(self, CHAN_WEAPON, sound_rail, 1, ATTN_NORM);
            M_SetAnimation(self, &widow_move_attack_pre_rail);
        } else
            M_SetAnimation(self, &widow_move_spawn);
    } else {
        if (level.time < self->timestamp)
            M_SetAnimation(self, &widow_move_attack_pre_blaster);
        else if ((luck <= 0.50f) || (level.time + BLASTER_TIME >= self->monsterinfo.fire_wait)) {
            G_StartSound(self, CHAN_WEAPON, sound_rail, 1, ATTN_NORM);
            M_SetAnimation(self, &widow_move_attack_pre_rail);
        } else // holdout to blaster
            M_SetAnimation(self, &widow_move_attack_pre_blaster);
    }
}

static void widow_attack_blaster(edict_t *self)
{
    self->monsterinfo.fire_wait = level.time + random_time_sec(1, 3);
    M_SetAnimation(self, &widow_move_attack_blaster);
    self->monsterinfo.nextframe = WidowTorso(self);
}

static void widow_reattack_blaster(edict_t *self)
{
    WidowBlaster(self);

    // if WidowBlaster bailed us out of the frames, just bail
    if ((self->monsterinfo.active_move == &widow_move_attack_post_blaster_l) ||
        (self->monsterinfo.active_move == &widow_move_attack_post_blaster_r))
        return;

    // if we're not done with the attack, don't leave the sequence
    if (self->monsterinfo.fire_wait >= level.time)
        return;

    self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

    M_SetAnimation(self, &widow_move_attack_post_blaster);
}

void PAIN(widow_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + SEC(5);

    if (damage < 15)
        G_StartSound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NONE);
    else if (damage < 75)
        G_StartSound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NONE);
    else
        G_StartSound(self, CHAN_VOICE, sound_pain3, 1, ATTN_NONE);

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    self->monsterinfo.fire_wait = 0;

    if (damage >= 15) {
        if (damage < 75) {
            if ((skill.integer < 3) && (frandom() < (0.6f - (0.2f * skill.integer)))) {
                M_SetAnimation(self, &widow_move_pain_light);
                self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
            }
        } else {
            if ((skill.integer < 3) && (frandom() < (0.75f - (0.1f * skill.integer)))) {
                M_SetAnimation(self, &widow_move_pain_heavy);
                self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
            }
        }
    }
}

void MONSTERINFO_SETSKIN(widow_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

#if 0
static void widow_dead(edict_t *self)
{
    VectorSet(self->r.mins, -56, -56, 0);
    VectorSet(self->r.maxs, 56, 56, 80);
    self->movetype = MOVETYPE_TOSS;
    self->r.svflags |= SVF_DEADMONSTER;
    self->nextthink = 0;
    trap_LinkEntity(self);
}
#endif

void DIE(widow_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t point, mod_t mod)
{
    self->deadflag = true;
    self->takedamage = false;
    self->count = 0;
    self->monsterinfo.quad_time = 0;
    self->monsterinfo.double_time = 0;
    self->monsterinfo.invincible_time = 0;
    M_SetAnimation(self, &widow_move_death);
}

void MONSTERINFO_MELEE(widow_melee)(edict_t *self)
{
    //  monster_done_dodge (self);
    M_SetAnimation(self, &widow_move_attack_kick);
}

static void WidowGoinQuad(edict_t *self, gtime_t time)
{
    self->monsterinfo.quad_time = time;
    widow_damage_multiplier = 4;
}

static void WidowDouble(edict_t *self, gtime_t time)
{
    self->monsterinfo.double_time = time;
    widow_damage_multiplier = 2;
}

static void WidowPent(edict_t *self, gtime_t time)
{
    self->monsterinfo.invincible_time = time;
}

static void WidowPowerArmor(edict_t *self)
{
    self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
    // I don't like this, but it works
    if (self->monsterinfo.power_armor_power <= 0)
        self->monsterinfo.power_armor_power += 250 * skill.integer;
}

static void WidowRespondPowerup(edict_t *self, edict_t *other)
{
    if (other->s.effects & EF_QUAD) {
        if (skill.integer == 1)
            WidowDouble(self, other->client->quad_time);
        else if (skill.integer == 2)
            WidowGoinQuad(self, other->client->quad_time);
        else if (skill.integer == 3) {
            WidowGoinQuad(self, other->client->quad_time);
            WidowPowerArmor(self);
        }
    } else if (other->s.effects & EF_DOUBLE) {
        if (skill.integer == 2)
            WidowDouble(self, other->client->double_time);
        else if (skill.integer == 3) {
            WidowDouble(self, other->client->double_time);
            WidowPowerArmor(self);
        }
    } else
        widow_damage_multiplier = 1;

    if (other->s.effects & EF_PENT) {
        if (skill.integer == 1)
            WidowPowerArmor(self);
        else if (skill.integer == 2)
            WidowPent(self, other->client->invincible_time);
        else if (skill.integer == 3) {
            WidowPent(self, other->client->invincible_time);
            WidowPowerArmor(self);
        }
    }
}

void WidowPowerups(edict_t *self)
{
    edict_t *ent;

    if (!coop.integer) {
        WidowRespondPowerup(self, self->enemy);
    } else {
        // in coop, check for pents, then quads, then doubles
        for (int player = 0; player < game.maxclients; player++) {
            ent = &g_edicts[player];
            if (!ent->r.inuse)
                continue;
            if (!ent->client)
                continue;
            if (ent->s.effects & EF_PENT) {
                WidowRespondPowerup(self, ent);
                return;
            }
        }

        for (int player = 0; player < game.maxclients; player++) {
            ent = &g_edicts[player];
            if (!ent->r.inuse)
                continue;
            if (!ent->client)
                continue;
            if (ent->s.effects & EF_QUAD) {
                WidowRespondPowerup(self, ent);
                return;
            }
        }

        for (int player = 0; player < game.maxclients; player++) {
            ent = &g_edicts[player];
            if (!ent->r.inuse)
                continue;
            if (!ent->client)
                continue;
            if (ent->s.effects & EF_DOUBLE) {
                WidowRespondPowerup(self, ent);
                return;
            }
        }
    }
}

bool MONSTERINFO_CHECKATTACK(Widow_CheckAttack)(edict_t *self)
{
    if (!self->enemy)
        return false;

    WidowPowerups(self);

    if (self->monsterinfo.active_move == &widow_move_run) {
        // if we're in run, make sure we're in a good frame for attacking before doing anything else
        // frames 1,2,3,9,10,11,13 good to fire
        switch (self->s.frame) {
        case FRAME_walk04:
        case FRAME_walk05:
        case FRAME_walk06:
        case FRAME_walk07:
        case FRAME_walk08:
        case FRAME_walk12:
            return false;
        default:
            break;
        }
    }

    // give a LARGE bias to spawning things when we have room
    // use AI_BLOCKED as a signal to attack to spawn
    if ((frandom() < 0.8f) && (M_SlotsLeft(self) >= 2) && (realrange(self, self->enemy) > 150)) {
        self->monsterinfo.aiflags |= AI_BLOCKED;
        self->monsterinfo.attack_state = AS_MISSILE;
        return true;
    }

    return M_CheckAttack_Base(self, 0.4f, 0.8f, 0.7f, 0.6f, 0.5f, 0.0f);
}

bool MONSTERINFO_BLOCKED(widow_blocked)(edict_t *self, float dist)
{
    // if we get blocked while we're in our run/attack mode, turn on a meaningless (in this context)AI flag,
    // and call attack to get a new attack sequence.  make sure to turn it off when we're done.
    //
    // I'm using AI_TARGET_ANGER for this purpose

    if (self->monsterinfo.active_move == &widow_move_run_attack) {
        self->monsterinfo.aiflags |= AI_TARGET_ANGER;
        if (self->monsterinfo.checkattack(self))
            self->monsterinfo.attack(self);
        else
            self->monsterinfo.run(self);
        return true;
    }

    return false;
}

void WidowCalcSlots(edict_t *self)
{
    switch (skill.integer) {
    case 0:
    case 1:
        self->monsterinfo.monster_slots = 3;
        break;
    case 2:
        self->monsterinfo.monster_slots = 4;
        break;
    case 3:
        self->monsterinfo.monster_slots = 6;
        break;
    default:
        self->monsterinfo.monster_slots = 3;
        break;
    }
    if (coop.integer)
        self->monsterinfo.monster_slots = min(6, self->monsterinfo.monster_slots + (skill.integer * (CountPlayers() - 1)));
}

static void WidowPrecache(void)
{
    // cache in all of the stalker stuff, widow stuff, spawngro stuff, gibs
    G_SoundIndex("stalker/pain.wav");
    G_SoundIndex("stalker/death.wav");
    G_SoundIndex("stalker/sight.wav");
    G_SoundIndex("stalker/melee1.wav");
    G_SoundIndex("stalker/melee2.wav");
    G_SoundIndex("stalker/idle.wav");

    G_SoundIndex("tank/tnkatck3.wav");
    G_ModelIndex("models/objects/laser/tris.md2");

    G_ModelIndex("models/monsters/stalker/tris.md2");
    G_ModelIndex("models/items/spawngro3/tris.md2");
    G_ModelIndex("models/objects/gibs/sm_metal/tris.md2");
    G_ModelIndex("models/objects/gibs/gear/tris.md2");
    G_ModelIndex("models/monsters/blackwidow/gib1/tris.md2");
    G_ModelIndex("models/monsters/blackwidow/gib2/tris.md2");
    G_ModelIndex("models/monsters/blackwidow/gib3/tris.md2");
    G_ModelIndex("models/monsters/blackwidow/gib4/tris.md2");
    G_ModelIndex("models/monsters/blackwidow2/gib1/tris.md2");
    G_ModelIndex("models/monsters/blackwidow2/gib2/tris.md2");
    G_ModelIndex("models/monsters/blackwidow2/gib3/tris.md2");
    G_ModelIndex("models/monsters/blackwidow2/gib4/tris.md2");
    G_ModelIndex("models/monsters/legs/tris.md2");
    G_SoundIndex("misc/bwidowbeamout.wav");

    G_SoundIndex("misc/bigtele.wav");
    G_SoundIndex("widow/bwstep3.wav");
    G_SoundIndex("widow/bwstep2.wav");
    G_SoundIndex("widow/bwstep1.wav");
}

static void widow_precache_global(void)
{
    sound_pain1 = G_SoundIndex("widow/bw1pain1.wav");
    sound_pain2 = G_SoundIndex("widow/bw1pain2.wav");
    sound_pain3 = G_SoundIndex("widow/bw1pain3.wav");
    sound_rail = G_SoundIndex("gladiator/railgun.wav");
}

/*QUAKED monster_widow (1 .5 0) (-40 -40 0) (40 40 144) Ambush Trigger_Spawn Sight
 */
void SP_monster_widow(edict_t *self)
{
    if (!M_AllowSpawn(self)) {
        G_FreeEdict(self);
        return;
    }

    G_AddPrecache(widow_precache_global);

    self->movetype = MOVETYPE_STEP;
    self->r.solid = SOLID_BBOX;
    self->s.modelindex = G_ModelIndex("models/monsters/blackwidow/tris.md2");
    VectorSet(self->r.mins, -40, -40, 0);
    VectorSet(self->r.maxs, 40, 40, 144);

    self->health = (2000 + 1000 * skill.integer) * st.health_multiplier;
    if (coop.integer)
        self->health += 500 * skill.integer;
    self->gib_health = -5000;
    self->mass = 1500;

    if (skill.integer == 3) {
        if (!ED_WasKeySpecified("power_armor_type"))
            self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
        if (!ED_WasKeySpecified("power_armor_power"))
            self->monsterinfo.power_armor_power = 500;
    }

    self->yaw_speed = 30;

    self->flags |= FL_IMMUNE_LASER;
    self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

    self->pain = widow_pain;
    self->die = widow_die;

    self->monsterinfo.melee = widow_melee;
    self->monsterinfo.stand = widow_stand;
    self->monsterinfo.walk = widow_walk;
    self->monsterinfo.run = widow_run;
    self->monsterinfo.attack = widow_attack;
    self->monsterinfo.search = widow_search;
    self->monsterinfo.checkattack = Widow_CheckAttack;
    self->monsterinfo.sight = widow_sight;
    self->monsterinfo.setskin = widow_setskin;
    self->monsterinfo.blocked = widow_blocked;

    trap_LinkEntity(self);

    M_SetAnimation(self, &widow_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    WidowPrecache();
    WidowCalcSlots(self);
    widow_damage_multiplier = 1;

    walkmonster_start(self);
}
