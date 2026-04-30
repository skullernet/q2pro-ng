// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

black widow, part 2

==============================================================================
*/

// timestamp used to prevent rapid fire of melee attack

#include "g_local.h"
#include "m_rogue_widow2.h"

static int sound_pain1;
static int sound_pain2;
static int sound_pain3;
static int sound_death;
static int sound_search1;
static int sound_tentacles_retract;

// sqrt(64*64*2) + sqrt(28*28*2) => 130.1
static const vec3_t spawnpoints[] = {
    { 30, 135, 0 },
    { 30, -135, 0 }
};

static const float sweep_angles[] = {
    -40, -32, -24, -16, -8, 0, 8, 16, 24, 32, 40
};

static const box3_t stalker_box = {
    .mins = { -28, -28, -18 },
    .maxs = { 28, 28, 18 }
};

void WidowCalcSlots(edict_t *self);
void WidowPowerups(edict_t *self);

void widow2_run(edict_t *self);
static void widow2_attack_beam(edict_t *self);
static void widow2_reattack_beam(edict_t *self);
void widow_start_spawn(edict_t *self);
void widow_done_spawn(edict_t *self);
static void widow2_spawn_check(edict_t *self);
static void Widow2SaveBeamTarget(edict_t *self);

// death stuff
void WidowExplode(edict_t *self);
static void ThrowWidowGibReal(edict_t *self, const char *gibname, int damage, gib_type_t type, vec3_t startpos, bool large, int hitsound, bool fade);
void ThrowWidowGibSized(edict_t *self, const char *gibname, int damage, gib_type_t type, vec3_t startpos, int hitsound, bool fade);
static void ThrowWidowGibLoc(edict_t *self, const char *gibname, int damage, gib_type_t type, vec3_t startpos, bool fade);
void ThrowSmallStuff(edict_t *self, vec3_t point);
static void WidowExplosion1(edict_t *self);
static void WidowExplosion2(edict_t *self);
static void WidowExplosion3(edict_t *self);
static void WidowExplosion4(edict_t *self);
static void WidowExplosion5(edict_t *self);
static void WidowExplosion6(edict_t *self);
static void WidowExplosion7(edict_t *self);
static void WidowExplosionLeg(edict_t *self);
static void ThrowArm1(edict_t *self);
static void ThrowArm2(edict_t *self);
void ClipGibVelocity(edict_t *ent);
// end of death stuff

// these offsets used by the tongue
static const vec3_t offsets[] = {
    { 17.48f, 0.10f, 68.92f },
    { 17.47f, 0.29f, 68.91f },
    { 17.45f, 0.53f, 68.87f },
    { 17.42f, 0.78f, 68.81f },
    { 17.39f, 1.02f, 68.75f },
    { 17.37f, 1.20f, 68.70f },
    { 17.36f, 1.24f, 68.71f },
    { 17.37f, 1.21f, 68.72f },
};

#if 0
static void pauseme(edict_t *self)
{
    self->monsterinfo.aiflags |= AI_HOLD_FRAME;
}
#endif

void MONSTERINFO_SEARCH(widow2_search)(edict_t *self)
{
    if (brandom())
        G_StartSound(self, CHAN_VOICE, sound_search1, 1, ATTN_NONE);
}

static void Widow2Beam(edict_t *self)
{
    vec3_t                   forward, right, target;
    vec3_t                   start, targ_angles, vec;
    monster_muzzleflash_id_t flashnum;

    if ((!self->enemy) || (!self->enemy->r.inuse))
        return;

    AngleVectors(self->s.angles, &forward, &right, NULL);

    if ((self->s.frame >= FRAME_fireb05) && (self->s.frame <= FRAME_fireb09)) {
        // regular beam attack
        Widow2SaveBeamTarget(self);
        flashnum = MZ2_WIDOW2_BEAMER_1 + self->s.frame - FRAME_fireb05;
        start = G_ProjectSource(self->s.origin, monster_flash_offset[flashnum], forward, right);

        target = self->pos2;
        target.z += self->enemy->viewheight - 10;

        forward = Vec3_Direction(target, start);
        monster_fire_heatbeam(self, start, forward, vec3_origin, 10, 50, flashnum);
    } else if ((self->s.frame >= FRAME_spawn04) && (self->s.frame <= FRAME_spawn14)) {
        // sweep
        flashnum = MZ2_WIDOW2_BEAM_SWEEP_1 + self->s.frame - FRAME_spawn04;
        start = G_ProjectSource(self->s.origin, monster_flash_offset[flashnum], forward, right);
        target = Vec3_Sub(self->enemy->s.origin, start);
        targ_angles = vectoangles(target);

        vec = self->s.angles;
        vec.pitch += targ_angles.pitch;
        vec.yaw -= sweep_angles[flashnum - MZ2_WIDOW2_BEAM_SWEEP_1];

        AngleVectors(vec, &forward, NULL, NULL);
        monster_fire_heatbeam(self, start, forward, vec3_origin, 10, 50, flashnum);
    } else {
        Widow2SaveBeamTarget(self);
        start = G_ProjectSource(self->s.origin, monster_flash_offset[MZ2_WIDOW2_BEAMER_1], forward, right);

        target = self->pos2;
        target.z += self->enemy->viewheight - 10;

        forward = Vec3_Direction(target, start);
        monster_fire_heatbeam(self, start, forward, vec3_origin, 10, 50, MZ2_WIDOW2_BEAM_SWEEP_1);
    }
}

static void Widow2Spawn(edict_t *self)
{
    vec3_t   f, r, u, startpoint, spawnpoint;
    edict_t *ent, *designated_enemy;
    int      i;

    AngleVectors(self->s.angles, &f, &r, &u);

    for (i = 0; i < 2; i++) {
        startpoint = G_ProjectSource2(self->s.origin, spawnpoints[i], f, r, u);

        if (!FindSpawnPoint(startpoint, stalker_box, &spawnpoint, 64, true))
            continue;

        ent = CreateGroundMonster(spawnpoint, self->s.angles, stalker_box, "monster_stalker", 256);
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

        if ((designated_enemy) && (designated_enemy->r.inuse) && (designated_enemy->health > 0)) {
            ent->enemy = designated_enemy;
            FoundTarget(ent);
            ent->monsterinfo.attack(ent);
        }
    }
}

static void widow2_spawn_check(edict_t *self)
{
    Widow2Beam(self);
    Widow2Spawn(self);
}

static void widow2_ready_spawn(edict_t *self)
{
    vec3_t f, r, u, mid, startpoint, spawnpoint;
    int    i;

    Widow2Beam(self);
    AngleVectors(self->s.angles, &f, &r, &u);

    mid = Box3_Center(stalker_box); // FIXME
    float radius = Box3_Radius(stalker_box);

    for (i = 0; i < 2; i++) {
        startpoint = G_ProjectSource2(self->s.origin, spawnpoints[i], f, r, u);
        if (FindSpawnPoint(startpoint, stalker_box, &spawnpoint, 64, true)) {
            spawnpoint = Vec3_Add(spawnpoint, mid);
            SpawnGrow_Spawn(spawnpoint, radius, radius * 2);
        }
    }
}

static void widow2_step(edict_t *self)
{
    G_StartSound(self, CHAN_BODY, G_SoundIndex("widow/bwstep1.wav"), 1, ATTN_NORM);
}

static const mframe_t widow2_frames_stand[] = {
    { ai_stand }
};
const mmove_t MMOVE_T(widow2_move_stand) = { FRAME_blackwidow3, FRAME_blackwidow3, widow2_frames_stand, NULL };

static const mframe_t widow2_frames_walk[] = {
    { ai_walk, 9.01f, widow2_step },
    { ai_walk, 7.55f },
    { ai_walk, 7.01f },
    { ai_walk, 6.66f },
    { ai_walk, 6.20f },
    { ai_walk, 5.78f, widow2_step },
    { ai_walk, 7.25f },
    { ai_walk, 8.37f },
    { ai_walk, 10.41f }
};
const mmove_t MMOVE_T(widow2_move_walk) = { FRAME_walk01, FRAME_walk09, widow2_frames_walk, NULL };

static const mframe_t widow2_frames_run[] = {
    { ai_run, 9.01f, widow2_step },
    { ai_run, 7.55f },
    { ai_run, 7.01f },
    { ai_run, 6.66f },
    { ai_run, 6.20f },
    { ai_run, 5.78f, widow2_step },
    { ai_run, 7.25f },
    { ai_run, 8.37f },
    { ai_run, 10.41f }
};
const mmove_t MMOVE_T(widow2_move_run) = { FRAME_walk01, FRAME_walk09, widow2_frames_run, NULL };

static const mframe_t widow2_frames_attack_pre_beam[] = {
    { ai_charge, 4 },
    { ai_charge, 4, widow2_step },
    { ai_charge, 4 },
    { ai_charge, 4, widow2_attack_beam }
};
const mmove_t MMOVE_T(widow2_move_attack_pre_beam) = { FRAME_fireb01, FRAME_fireb04, widow2_frames_attack_pre_beam, NULL };

// Loop this
static const mframe_t widow2_frames_attack_beam[] = {
    { ai_charge, 0, Widow2Beam },
    { ai_charge, 0, Widow2Beam },
    { ai_charge, 0, Widow2Beam },
    { ai_charge, 0, Widow2Beam },
    { ai_charge, 0, widow2_reattack_beam }
};
const mmove_t MMOVE_T(widow2_move_attack_beam) = { FRAME_fireb05, FRAME_fireb09, widow2_frames_attack_beam, NULL };

static const mframe_t widow2_frames_attack_post_beam[] = {
    { ai_charge, 4 },
    { ai_charge, 4 }
};
const mmove_t MMOVE_T(widow2_move_attack_post_beam) = { FRAME_fireb06, FRAME_fireb07, widow2_frames_attack_post_beam, widow2_run };

static void WidowDisrupt(edict_t *self)
{
    vec3_t start;
    vec3_t dir;
    vec3_t forward, right;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = G_ProjectSource(self->s.origin, monster_flash_offset[MZ2_WIDOW_DISRUPTOR], forward, right);

    if (Vec3_Distance(self->pos1, self->enemy->s.origin) < 30) {
        // calc direction to where we targeted
        dir = Vec3_Direction(self->pos1, start);
        monster_fire_tracker(self, start, dir, 20, 500, self->enemy, MZ2_WIDOW_DISRUPTOR);
    } else {
        M_PredictAim(self, self->enemy, start, 1200, true, 0, &dir, NULL);
        monster_fire_tracker(self, start, dir, 20, 1200, NULL, MZ2_WIDOW_DISRUPTOR);
    }

    widow2_step(self);
}

static void Widow2SaveDisruptLoc(edict_t *self)
{
    if (self->enemy && self->enemy->r.inuse) {
        self->pos1 = self->enemy->s.origin; // save for aiming the shot
        self->pos1.z += self->enemy->viewheight;
    } else
        self->pos1 = vec3_origin;
}

static void widow2_disrupt_reattack(edict_t *self)
{
    if (frandom() < (0.25f + (skill.integer * 0.15f)))
        self->monsterinfo.nextframe = FRAME_firea01;
}

static const mframe_t widow2_frames_attack_disrupt[] = {
    { ai_charge, 2 },
    { ai_charge, 2 },
    { ai_charge, 2, Widow2SaveDisruptLoc },
    { ai_charge, -20, WidowDisrupt },
    { ai_charge, 2 },
    { ai_charge, 2 },
    { ai_charge, 2, widow2_disrupt_reattack }
};
const mmove_t MMOVE_T(widow2_move_attack_disrupt) = { FRAME_firea01, FRAME_firea07, widow2_frames_attack_disrupt, widow2_run };

static void Widow2SaveBeamTarget(edict_t *self)
{
    if (self->enemy && self->enemy->r.inuse) {
        self->pos2 = self->pos1;
        self->pos1 = self->enemy->s.origin; // save for aiming the shot
    } else {
        self->pos1 = vec3_origin;
        self->pos2 = vec3_origin;
    }
}

#if 0
static void Widow2BeamTargetRemove(edict_t *self)
{
    self->pos1 = vec3_origin;
    self->pos2 = vec3_origin;
}

static void Widow2StartSweep(edict_t *self)
{
    Widow2SaveBeamTarget(self);
}
#endif

static void widow2_start_spawn(edict_t *self)
{
    widow_start_spawn(self);
    widow2_step(self);
}

static const mframe_t widow2_frames_spawn[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, widow2_start_spawn },
    { ai_charge, 0, Widow2Beam },
    { ai_charge, 0, Widow2Beam }, // 5
    { ai_charge, 0, Widow2Beam },
    { ai_charge, 0, Widow2Beam },
    { ai_charge, 0, Widow2Beam },
    { ai_charge, 0, Widow2Beam },
    { ai_charge, 0, widow2_ready_spawn }, // 10
    { ai_charge, 0, Widow2Beam },
    { ai_charge, 0, Widow2Beam },
    { ai_charge, 0, Widow2Beam },
    { ai_charge, 0, widow2_spawn_check },
    { ai_charge }, // 15
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, widow2_reattack_beam }
};
const mmove_t MMOVE_T(widow2_move_spawn) = { FRAME_spawn01, FRAME_spawn18, widow2_frames_spawn, NULL };

static bool widow2_tongue_attack_ok(vec3_t start, vec3_t end, float range)
{
    vec3_t dir, angles;

    // check for max distance
    dir = Vec3_Sub(start, end);
    if (Vec3_Length(dir) > range)
        return false;

    // check for min/max pitch
    angles = vectoangles(dir);
    if (angles.pitch < -180)
        angles.pitch += 360;
    if (fabsf(angles.pitch) > 30)
        return false;

    return true;
}

void THINK(widow2_tongue_think)(edict_t *self)
{
    g_edicts[self->r.ownernum].beam = NULL;
    G_FreeEdict(self);
}

static void Widow2Tongue(edict_t *self)
{
    vec3_t  f, r, u;
    vec3_t  start, end, dir;
    trace_t tr;

    AngleVectors(self->s.angles, &f, &r, &u);
    start = G_ProjectSource2(self->s.origin, offsets[self->s.frame - FRAME_tongs01], f, r, u);
    end = self->enemy->s.origin;
    if (!widow2_tongue_attack_ok(start, end, 256)) {
        end.z = self->enemy->s.origin.z + self->enemy->r.box.maxs.z - 8;
        if (!widow2_tongue_attack_ok(start, end, 256)) {
            end.z = self->enemy->s.origin.z + self->enemy->r.box.mins.z + 8;
            if (!widow2_tongue_attack_ok(start, end, 256))
                return;
        }
    }

    end = self->enemy->s.origin;

    tr = G_TraceLine(start, end, self->s.number, MASK_PROJECTILE);
    if (tr.entnum != self->enemy->s.number)
        return;

    G_StartSound(self, CHAN_WEAPON, sound_tentacles_retract, 1, ATTN_NORM);

    edict_t *te = self->beam;
    if (!te) {
        self->beam = te = G_Spawn();
        te->s.renderfx = RF_BEAM;
        te->s.modelindex = G_ModelIndex("models/monsters/parasite/segment/tris.md2");
        te->s.othernum = ENTITYNUM_NONE;
        te->s.alpha = self->s.alpha;
        te->s.scale = self->s.scale;
        te->r.ownernum = self->s.number;
        te->think = widow2_tongue_think;
    }

    te->s.old_origin = G_SnapVector(start);
    te->s.origin = G_SnapVector(end);
    te->nextthink = level.time + SEC(0.2f);
    trap_LinkEntity(te);

    dir = Vec3_Sub(start, end);
    T_Damage(self->enemy, self, self, dir, self->enemy->s.origin, 0, 2, 0, DAMAGE_NO_KNOCKBACK, MOD_UNKNOWN);
}

static void Widow2TonguePull(edict_t *self)
{
    vec3_t vec;
    vec3_t f, r, u;
    vec3_t start, end;

    if ((!self->enemy) || (!self->enemy->r.inuse)) {
        self->monsterinfo.run(self);
        return;
    }

    AngleVectors(self->s.angles, &f, &r, &u);
    start = G_ProjectSource2(self->s.origin, offsets[self->s.frame - FRAME_tongs01], f, r, u);
    end = self->enemy->s.origin;

    if (!widow2_tongue_attack_ok(start, end, 256))
        return;

    if (self->enemy->groundentity) {
        self->enemy->s.origin.z += 1;
        self->enemy->groundentity = NULL;
        // interesting, you don't have to relink the player
    }

    vec = Vec3_Direction(self->s.origin, self->enemy->s.origin);

    if (self->enemy->client) {
        self->enemy->velocity = Vec3_MA(self->enemy->velocity, 1000, vec);
    } else {
        self->enemy->ideal_yaw = vectoyaw(vec);
        M_ChangeYaw(self->enemy);
        self->enemy->velocity = Vec3_Scale(f, 1000);
    }
}

static void Widow2Crunch(edict_t *self)
{
    vec3_t aim;

    if ((!self->enemy) || (!self->enemy->r.inuse)) {
        self->monsterinfo.run(self);
        return;
    }

    Widow2TonguePull(self);

    // 70 + 32
    aim = Vec3(150, 0, 4);
    if (self->s.frame != FRAME_tongs07)
        fire_hit(self, aim, irandom2(20, 26), 0);
    else if (self->enemy->groundentity)
        fire_hit(self, aim, irandom2(20, 26), 500);
    else // not as much kick if they're in the air .. makes it harder to land on her head
        fire_hit(self, aim, irandom2(20, 26), 250);
}

static void Widow2Toss(edict_t *self)
{
    self->timestamp = level.time + SEC(3);
}

static const mframe_t widow2_frames_tongs[] = {
    { ai_charge, 0, Widow2Tongue },
    { ai_charge, 0, Widow2Tongue },
    { ai_charge, 0, Widow2Tongue },
    { ai_charge, 0, Widow2TonguePull },
    { ai_charge, 0, Widow2TonguePull }, // 5
    { ai_charge, 0, Widow2TonguePull },
    { ai_charge, 0, Widow2Crunch },
    { ai_charge, 0, Widow2Toss }
};
const mmove_t MMOVE_T(widow2_move_tongs) = { FRAME_tongs01, FRAME_tongs08, widow2_frames_tongs, widow2_run };

static const mframe_t widow2_frames_pain[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(widow2_move_pain) = { FRAME_pain01, FRAME_pain05, widow2_frames_pain, widow2_run };

static const mframe_t widow2_frames_death[] = {
    { ai_move },
    { ai_move },
    { ai_move, 0, WidowExplosion1 }, // 3 boom
    { ai_move },
    { ai_move }, // 5

    { ai_move, 0, WidowExplosion2 }, // 6 boom
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }, // 10

    { ai_move },
    { ai_move }, // 12
    { ai_move },
    { ai_move },
    { ai_move }, // 15

    { ai_move },
    { ai_move },
    { ai_move, 0, WidowExplosion3 }, // 18
    { ai_move },                     // 19
    { ai_move },                     // 20

    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, WidowExplosion4 }, // 25

    { ai_move }, // 26
    { ai_move },
    { ai_move },
    { ai_move, 0, WidowExplosion5 },
    { ai_move, 0, WidowExplosionLeg }, // 30

    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, WidowExplosion6 },
    { ai_move }, // 35

    { ai_move },
    { ai_move },
    { ai_move, 0, WidowExplosion7 },
    { ai_move },
    { ai_move }, // 40

    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, WidowExplode } // 44
};
const mmove_t MMOVE_T(widow2_move_death) = { FRAME_death01, FRAME_death44, widow2_frames_death, NULL };

static void widow2_start_searching(edict_t *self);
static void widow2_keep_searching(edict_t *self);
static void widow2_finaldeath(edict_t *self);

static const mframe_t widow2_frames_dead[] = {
    { ai_move, 0, widow2_start_searching },
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
    { ai_move, 0, widow2_keep_searching }
};
const mmove_t MMOVE_T(widow2_move_dead) = { FRAME_dthsrh01, FRAME_dthsrh15, widow2_frames_dead, NULL };

static const mframe_t widow2_frames_really_dead[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },

    { ai_move },
    { ai_move, 0, widow2_finaldeath }
};
const mmove_t MMOVE_T(widow2_move_really_dead) = { FRAME_dthsrh16, FRAME_dthsrh22, widow2_frames_really_dead, NULL };

static void widow2_start_searching(edict_t *self)
{
    self->count = 0;
}

static void widow2_keep_searching(edict_t *self)
{
    if (self->count <= 2) {
        M_SetAnimation(self, &widow2_move_dead);
        self->s.frame = FRAME_dthsrh01;
        self->count++;
        return;
    }

    M_SetAnimation(self, &widow2_move_really_dead);
}

static void widow2_finaldeath(edict_t *self)
{
    self->r.box = Box3_FromSize(70, 0, 80);
    self->movetype = MOVETYPE_TOSS;
    self->takedamage = true;
    self->nextthink = 0;
    trap_LinkEntity(self);
}

void MONSTERINFO_STAND(widow2_stand)(edict_t *self)
{
    M_SetAnimation(self, &widow2_move_stand);
}

void MONSTERINFO_RUN(widow2_run)(edict_t *self)
{
    self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;

    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
        M_SetAnimation(self, &widow2_move_stand);
    else
        M_SetAnimation(self, &widow2_move_run);
}

void MONSTERINFO_WALK(widow2_walk)(edict_t *self)
{
    M_SetAnimation(self, &widow2_move_walk);
}

void widow2_attack(edict_t *self);

void MONSTERINFO_MELEE(widow2_melee)(edict_t *self)
{
    if (self->timestamp >= level.time)
        widow2_attack(self);
    else
        M_SetAnimation(self, &widow2_move_tongs);
}

void MONSTERINFO_ATTACK(widow2_attack)(edict_t *self)
{
    float luck;
    bool  blocked = false;

    if (self->monsterinfo.aiflags & AI_BLOCKED) {
        blocked = true;
        self->monsterinfo.aiflags &= ~AI_BLOCKED;
    }

    if (!self->enemy)
        return;

    float real_enemy_range = realrange(self, self->enemy);

    // melee attack
    if (self->timestamp < level.time) {
        if (real_enemy_range < 300) {
            vec3_t f, r, u, spot;
            AngleVectors(self->s.angles, &f, &r, &u);
            spot = G_ProjectSource2(self->s.origin, offsets[0], f, r, u);
            if (widow2_tongue_attack_ok(spot, self->enemy->s.origin, 256)) {
                // melee attack ok

                // be nice in easy mode
                if (skill.integer != 0 || irandom1(4)) {
                    M_SetAnimation(self, &widow2_move_tongs);
                    return;
                }
            }
        }
    }

    if (self->bad_area) {
        if ((frandom() < 0.75f) || (level.time < self->monsterinfo.attack_finished))
            M_SetAnimation(self, &widow2_move_attack_pre_beam);
        else {
            M_SetAnimation(self, &widow2_move_attack_disrupt);
        }
        return;
    }

    WidowCalcSlots(self);

    // if we can't see the target, spawn stuff
    if ((self->monsterinfo.attack_state == AS_BLIND) && (M_SlotsLeft(self) >= 2)) {
        M_SetAnimation(self, &widow2_move_spawn);
        return;
    }

    // accept bias towards spawning
    if (blocked && (M_SlotsLeft(self) >= 2)) {
        M_SetAnimation(self, &widow2_move_spawn);
        return;
    }

    if (real_enemy_range < 600) {
        luck = frandom();
        if (M_SlotsLeft(self) >= 2) {
            if (luck <= 0.40f)
                M_SetAnimation(self, &widow2_move_attack_pre_beam);
            else if ((luck <= 0.7f) && !(level.time < self->monsterinfo.attack_finished)) {
                M_SetAnimation(self, &widow2_move_attack_disrupt);
            } else
                M_SetAnimation(self, &widow2_move_spawn);
        } else {
            if ((luck <= 0.50f) || (level.time < self->monsterinfo.attack_finished))
                M_SetAnimation(self, &widow2_move_attack_pre_beam);
            else {
                M_SetAnimation(self, &widow2_move_attack_disrupt);
            }
        }
    } else {
        luck = frandom();
        if (M_SlotsLeft(self) >= 2) {
            if (luck < 0.3f)
                M_SetAnimation(self, &widow2_move_attack_pre_beam);
            else if ((luck < 0.65f) || (level.time < self->monsterinfo.attack_finished))
                M_SetAnimation(self, &widow2_move_spawn);
            else {
                M_SetAnimation(self, &widow2_move_attack_disrupt);
            }
        } else {
            if ((luck < 0.45f) || (level.time < self->monsterinfo.attack_finished))
                M_SetAnimation(self, &widow2_move_attack_pre_beam);
            else {
                M_SetAnimation(self, &widow2_move_attack_disrupt);
            }
        }
    }
}

static void widow2_attack_beam(edict_t *self)
{
    M_SetAnimation(self, &widow2_move_attack_beam);
    widow2_step(self);
}

static void widow2_reattack_beam(edict_t *self)
{
    self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;

    if (brandom() && infront(self, self->enemy)) {
        if ((frandom() < 0.7f) || (M_SlotsLeft(self) < 2))
            M_SetAnimation(self, &widow2_move_attack_beam);
        else
            M_SetAnimation(self, &widow2_move_spawn);
    } else
        M_SetAnimation(self, &widow2_move_attack_post_beam);
}

void PAIN(widow2_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
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

    if (damage >= 15) {
        if (damage < 75) {
            if ((skill.integer < 3) && (frandom() < (0.6f - (0.2f * skill.integer)))) {
                self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
                M_SetAnimation(self, &widow2_move_pain);
            }
        } else {
            if ((skill.integer < 3) && (frandom() < (0.75f - (0.1f * skill.integer)))) {
                self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
                M_SetAnimation(self, &widow2_move_pain);
            }
        }
    }
}

void MONSTERINFO_SETSKIN(widow2_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

static void KillChildren(edict_t *self)
{
    edict_t *ent = NULL;

    while ((ent = G_Find(ent, FOFS(classname), "monster_stalker")) != NULL) {
        // FIXME - may need to stagger
        if ((ent->r.inuse) && (ent->health > 0))
            T_Damage(ent, self, self, vec3_origin, self->enemy->s.origin, 0, (ent->health + 1), 0, DAMAGE_NO_KNOCKBACK, MOD_UNKNOWN);
    }
}

void DIE(widow2_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    int n;
    int clipped;

    // check for gib
    if (self->deadflag && M_CheckGib(self, mod)) {
        clipped = min(damage, 100);

        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        for (n = 0; n < 2; n++)
            ThrowWidowGibLoc(self, "models/objects/gibs/bone/tris.md2", clipped, GIB_NONE, vec3_origin, false);
        for (n = 0; n < 3; n++)
            ThrowWidowGibLoc(self, "models/objects/gibs/sm_meat/tris.md2", clipped, GIB_NONE, vec3_origin, false);
        for (n = 0; n < 3; n++) {
            ThrowWidowGibSized(self, "models/monsters/blackwidow2/gib1/tris.md2", clipped, GIB_METALLIC, vec3_origin, 0, false);
            ThrowWidowGibSized(self, "models/monsters/blackwidow2/gib2/tris.md2", clipped, GIB_METALLIC, vec3_origin, G_SoundIndex("misc/fhit3.wav"), false);
        }
        for (n = 0; n < 2; n++) {
            ThrowWidowGibSized(self, "models/monsters/blackwidow2/gib3/tris.md2", clipped, GIB_METALLIC, vec3_origin, 0, false);
            ThrowWidowGibSized(self, "models/monsters/blackwidow/gib3/tris.md2", clipped, GIB_METALLIC, vec3_origin, 0, false);
        }
        ThrowGib(self, "models/objects/gibs/chest/tris.md2", damage, GIB_NONE);
        ThrowGib(self, "models/objects/gibs/head2/tris.md2", damage, GIB_HEAD);
        return;
    }

    if (self->deadflag)
        return;

    G_StartSound(self, CHAN_VOICE, sound_death, 1, ATTN_NONE);
    self->deadflag = true;
    self->takedamage = false;
    self->count = 0;
    KillChildren(self);
    self->monsterinfo.quad_time = 0;
    self->monsterinfo.double_time = 0;
    self->monsterinfo.invincible_time = 0;
    M_SetAnimation(self, &widow2_move_death);
}

bool MONSTERINFO_CHECKATTACK(Widow2_CheckAttack)(edict_t *self)
{
    if (!self->enemy)
        return false;

    WidowPowerups(self);

    if ((frandom() < 0.8f) && (M_SlotsLeft(self) >= 2) && (realrange(self, self->enemy) > 150)) {
        self->monsterinfo.aiflags |= AI_BLOCKED;
        self->monsterinfo.attack_state = AS_MISSILE;
        return true;
    }

    return M_CheckAttack_Base(self, 0.4f, 0.8f, 0.8f, 0.5f, 0, 0);
}

static void Widow2Precache(void)
{
    // cache in all of the stalker stuff, widow stuff, spawngro stuff, gibs
    G_SoundIndex("parasite/parpain1.wav");
    G_SoundIndex("parasite/parpain2.wav");
    G_SoundIndex("parasite/pardeth1.wav");
    G_SoundIndex("parasite/paratck1.wav");
    G_SoundIndex("parasite/parsght1.wav");
    G_SoundIndex("infantry/melee2.wav");
    G_SoundIndex("misc/fhit3.wav");

    G_SoundIndex("tank/tnkatck3.wav");
    G_SoundIndex("weapons/disrupt.wav");
    G_SoundIndex("weapons/disint2.wav");

    G_ModelIndex("models/monsters/stalker/tris.md2");
    G_ModelIndex("models/items/spawngro3/tris.md2");
    G_ModelIndex("models/objects/gibs/sm_metal/tris.md2");
    G_ModelIndex("models/objects/laser/tris.md2");
    G_ModelIndex("models/proj/disintegrator/tris.md2");

    G_ModelIndex("models/monsters/blackwidow/gib1/tris.md2");
    G_ModelIndex("models/monsters/blackwidow/gib2/tris.md2");
    G_ModelIndex("models/monsters/blackwidow/gib3/tris.md2");
    G_ModelIndex("models/monsters/blackwidow/gib4/tris.md2");
    G_ModelIndex("models/monsters/blackwidow2/gib1/tris.md2");
    G_ModelIndex("models/monsters/blackwidow2/gib2/tris.md2");
    G_ModelIndex("models/monsters/blackwidow2/gib3/tris.md2");
    G_ModelIndex("models/monsters/blackwidow2/gib4/tris.md2");
}

void PR_monster_widow2(void)
{
    sound_pain1 = G_SoundIndex("widow/bw2pain1.wav");
    sound_pain2 = G_SoundIndex("widow/bw2pain2.wav");
    sound_pain3 = G_SoundIndex("widow/bw2pain3.wav");
    sound_death = G_SoundIndex("widow/death.wav");
    sound_search1 = G_SoundIndex("bosshovr/bhvunqv1.wav");
    sound_tentacles_retract = G_SoundIndex("brain/brnatck3.wav");
}

/*QUAKED monster_widow2 (1 .5 0) (-70 -70 0) (70 70 144) Ambush Trigger_Spawn Sight
 */
void SP_monster_widow2(edict_t *self)
{
    self->movetype = MOVETYPE_STEP;
    self->r.solid = SOLID_BBOX;
    self->s.modelindex = G_ModelIndex("models/monsters/blackwidow2/tris.md2");
    self->r.box = Box3_FromSize(70, 0, 144);

    self->health = (2000 + 800 + 1000 * skill.integer) * st.health_multiplier;
    if (coop.integer)
        self->health += 500 * skill.integer;
    self->gib_health = -900;
    self->mass = 2500;

    if (skill.integer == 3) {
        if (!ED_WasKeySpecified("power_armor_type"))
            self->monsterinfo.power_armor_type = IT_ITEM_POWER_SHIELD;
        if (!ED_WasKeySpecified("power_armor_power"))
            self->monsterinfo.power_armor_power = 750;
    }

    self->yaw_speed = 30;

    self->flags |= FL_IMMUNE_LASER;
    self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;

    self->pain = widow2_pain;
    self->die = widow2_die;

    self->monsterinfo.melee = widow2_melee;
    self->monsterinfo.stand = widow2_stand;
    self->monsterinfo.walk = widow2_walk;
    self->monsterinfo.run = widow2_run;
    self->monsterinfo.attack = widow2_attack;
    self->monsterinfo.search = widow2_search;
    self->monsterinfo.checkattack = Widow2_CheckAttack;
    self->monsterinfo.setskin = widow2_setskin;
    trap_LinkEntity(self);

    M_SetAnimation(self, &widow2_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    Widow2Precache();
    WidowCalcSlots(self);
    walkmonster_start(self);
}

//
// Death sequence stuff
//

static vec3_t WidowVelocityForDamage(int damage)
{
    vec3_t v = Vec3_Scale(Vec3_CenterRandom(), damage);
    v.z += 200.0f;
    return v;
}

void TOUCH(widow_gib_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    self->r.solid = SOLID_NOT;
    self->touch = NULL;
    self->s.angles.pitch = 0;
    self->s.angles.roll = 0;
    self->avelocity = vec3_origin;

    if (self->style)
        G_StartSound(self, CHAN_VOICE, self->style, 1, ATTN_NORM);
}

static void ThrowWidowGib(edict_t *self, const char *gibname, int damage, gib_type_t type)
{
    ThrowWidowGibReal(self, gibname, damage, type, vec3_origin, false, 0, true);
}

static void ThrowWidowGibLoc(edict_t *self, const char *gibname, int damage, gib_type_t type, vec3_t startpos, bool fade)
{
    ThrowWidowGibReal(self, gibname, damage, type, startpos, false, 0, fade);
}

void ThrowWidowGibSized(edict_t *self, const char *gibname, int damage, gib_type_t type, vec3_t startpos, int hitsound, bool fade)
{
    ThrowWidowGibReal(self, gibname, damage, type, startpos, true, hitsound, fade);
}

static void ThrowWidowGibReal(edict_t *self, const char *gibname, int damage, gib_type_t type, vec3_t startpos, bool sized, int hitsound, bool fade)
{
    edict_t *gib;
    vec3_t   vd;
    float    vscale;

    if (!gibname)
        return;

    gib = G_Spawn();

    if (!Vec3_IsEmpty(startpos))
        gib->s.origin = startpos;
    else
        gib->s.origin = Box3_RandomPoint(self->r.absbox);

    gib->r.solid = SOLID_NOT;
    gib->s.effects |= EF_GIB;
    gib->flags |= FL_NO_KNOCKBACK;
    gib->takedamage = true;
    gib->die = gib_die;
    gib->s.renderfx |= RF_IR_VISIBLE;
    gib->s.renderfx &= ~RF_DOT_SHADOW;

    if (fade) {
        gib->think = G_FreeEdict;
        // sized gibs last longer
        if (sized)
            gib->nextthink = level.time + random_time_sec(20, 35);
        else
            gib->nextthink = level.time + random_time_sec(5, 15);
    } else {
        gib->think = G_FreeEdict;
        // sized gibs last longer
        if (sized)
            gib->nextthink = level.time + random_time_sec(60, 75);
        else
            gib->nextthink = level.time + random_time_sec(25, 35);
    }

    if (!(type & GIB_METALLIC)) {
        gib->movetype = MOVETYPE_TOSS;
        vscale = 0.5f;
    } else {
        gib->movetype = MOVETYPE_BOUNCE;
        vscale = 1.0f;
    }

    vd = WidowVelocityForDamage(damage);
    gib->velocity = Vec3_MA(self->velocity, vscale, vd);
    ClipGibVelocity(gib);

    gib->s.modelindex = G_ModelIndex(gibname);

    if (sized) {
        gib->style = hitsound;
        gib->r.solid = SOLID_BBOX;
        gib->avelocity = Vec3_Scale(Vec3_Random(), 400);
        if (gib->velocity.z < 0)
            gib->velocity.z = -gib->velocity.z;
        gib->velocity.x *= 2;
        gib->velocity.y *= 2;
        ClipGibVelocity(gib);
        vscale = frandom2(350, 450);
        gib->velocity.z = max(vscale, gib->velocity.z);
        gib->gravity = 0.25f;
        gib->touch = widow_gib_touch;
        gib->r.ownernum = self->s.number;
        if (gib->s.modelindex == G_ModelIndex("models/monsters/blackwidow2/gib2/tris.md2"))
            gib->r.box = Box3_FromSize(10, 0, 10);
        else
            gib->r.box = Box3_FromSize(5, 0, 5);
    } else {
        gib->velocity.x *= 2;
        gib->velocity.y *= 2;
        gib->avelocity = Vec3_Scale(Vec3_Random(), 600);
    }

    trap_LinkEntity(gib);
}

void ThrowSmallStuff(edict_t *self, vec3_t point)
{
    int n;

    for (n = 0; n < 2; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_NONE, point, false);
    ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 300, GIB_METALLIC, point, false);
    ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, point, false);
}

static void ThrowMoreStuff(edict_t *self, vec3_t point)
{
    int n;

    if (coop.integer) {
        ThrowSmallStuff(self, point);
        return;
    }

    for (n = 0; n < 1; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_NONE, point, false);
    for (n = 0; n < 2; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 300, GIB_METALLIC, point, false);
    for (n = 0; n < 3; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, point, false);
}

void THINK(WidowExplode)(edict_t *self)
{
    vec3_t org;
    int    n;

    self->think = WidowExplode;

    org = self->s.origin;
    org.z += irandom2(24, 40);
    if (self->count < 8)
        org.z += irandom2(24, 56);
    switch (self->count) {
    case 0:
        org.x -= 24;
        org.y -= 24;
        break;
    case 1:
        org.x += 24;
        org.y += 24;
        ThrowSmallStuff(self, org);
        break;
    case 2:
        org.x += 24;
        org.y -= 24;
        break;
    case 3:
        org.x -= 24;
        org.y += 24;
        ThrowMoreStuff(self, org);
        break;
    case 4:
        org.x -= 48;
        org.y -= 48;
        break;
    case 5:
        org.x += 48;
        org.y += 48;
        ThrowArm1(self);
        break;
    case 6:
        org.x -= 48;
        org.y += 48;
        ThrowArm2(self);
        break;
    case 7:
        org.x += 48;
        org.y -= 48;
        ThrowSmallStuff(self, org);
        break;
    case 8:
        org.x += 18;
        org.y += 18;
        org.z = self->s.origin.z + 48;
        ThrowMoreStuff(self, org);
        break;
    case 9:
        org.x -= 18;
        org.y += 18;
        org.z = self->s.origin.z + 48;
        break;
    case 10:
        org.x += 18;
        org.y -= 18;
        org.z = self->s.origin.z + 48;
        break;
    case 11:
        org.x -= 18;
        org.y -= 18;
        org.z = self->s.origin.z + 48;
        break;
    case 12:
        self->s.sound = 0;
        for (n = 0; n < 1; n++)
            ThrowWidowGib(self, "models/objects/gibs/sm_meat/tris.md2", 400, GIB_NONE);
        for (n = 0; n < 2; n++)
            ThrowWidowGib(self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC);
        for (n = 0; n < 2; n++)
            ThrowWidowGib(self, "models/objects/gibs/sm_metal/tris.md2", 400, GIB_METALLIC);
        self->deadflag = true;
        self->think = monster_think;
        self->nextthink = level.time + HZ(10);
        M_SetAnimation(self, &widow2_move_dead);
        return;
    }

    self->count++;
    if (self->count >= 9 && self->count <= 12)
        G_TempEntity(org, EV_EXPLOSION1_BIG, 0);
    else
        G_TempEntity(org, (self->count & 1) ? EV_EXPLOSION1 : EV_EXPLOSION1_NP, 0);

    self->nextthink = level.time + HZ(10);
}

static void WidowExplosion1(edict_t *self)
{
    int    n;
    vec3_t f, r, u, startpoint;
    vec3_t offset = { 23.74f, -37.67f, 76.96f };

    AngleVectors(self->s.angles, &f, &r, &u);
    startpoint = G_ProjectSource2(self->s.origin, offset, f, r, u);

    G_TempEntity(startpoint, EV_EXPLOSION1, 0);

    for (n = 0; n < 1; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_NONE, startpoint, false);
    for (n = 0; n < 1; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, startpoint, false);
    for (n = 0; n < 2; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 300, GIB_METALLIC, startpoint, false);
}

static void WidowExplosion2(edict_t *self)
{
    int    n;
    vec3_t f, r, u, startpoint;
    vec3_t offset = { -20.49f, 36.92f, 73.52f };

    AngleVectors(self->s.angles, &f, &r, &u);
    startpoint = G_ProjectSource2(self->s.origin, offset, f, r, u);

    G_TempEntity(startpoint, EV_EXPLOSION1, 0);

    for (n = 0; n < 1; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_NONE, startpoint, false);
    for (n = 0; n < 1; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, startpoint, false);
    for (n = 0; n < 2; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 300, GIB_METALLIC, startpoint, false);
}

static void WidowExplosion3(edict_t *self)
{
    int    n;
    vec3_t f, r, u, startpoint;
    vec3_t offset = { 2.11f, 0.05f, 92.20f };

    AngleVectors(self->s.angles, &f, &r, &u);
    startpoint = G_ProjectSource2(self->s.origin, offset, f, r, u);

    G_TempEntity(startpoint, EV_EXPLOSION1, 0);

    for (n = 0; n < 1; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_NONE, startpoint, false);
    for (n = 0; n < 1; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, startpoint, false);
    for (n = 0; n < 2; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 300, GIB_METALLIC, startpoint, false);
}

static void WidowExplosion4(edict_t *self)
{
    int    n;
    vec3_t f, r, u, startpoint;
    vec3_t offset = { -28.04f, -35.57f, -77.56f };

    AngleVectors(self->s.angles, &f, &r, &u);
    startpoint = G_ProjectSource2(self->s.origin, offset, f, r, u);

    G_TempEntity(startpoint, EV_EXPLOSION1, 0);

    for (n = 0; n < 1; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_NONE, startpoint, false);
    for (n = 0; n < 1; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, startpoint, false);
    for (n = 0; n < 2; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 300, GIB_METALLIC, startpoint, false);
}

static void WidowExplosion5(edict_t *self)
{
    int    n;
    vec3_t f, r, u, startpoint;
    vec3_t offset = { -20.11f, -1.11f, 40.76f };

    AngleVectors(self->s.angles, &f, &r, &u);
    startpoint = G_ProjectSource2(self->s.origin, offset, f, r, u);

    G_TempEntity(startpoint, EV_EXPLOSION1, 0);

    for (n = 0; n < 1; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_NONE, startpoint, false);
    for (n = 0; n < 1; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, startpoint, false);
    for (n = 0; n < 2; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 300, GIB_METALLIC, startpoint, false);
}

static void WidowExplosion6(edict_t *self)
{
    int    n;
    vec3_t f, r, u, startpoint;
    vec3_t offset = { -20.11f, -1.11f, 40.76f };

    AngleVectors(self->s.angles, &f, &r, &u);
    startpoint = G_ProjectSource2(self->s.origin, offset, f, r, u);

    G_TempEntity(startpoint, EV_EXPLOSION1, 0);

    for (n = 0; n < 1; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_NONE, startpoint, false);
    for (n = 0; n < 1; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, startpoint, false);
    for (n = 0; n < 2; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 300, GIB_METALLIC, startpoint, false);
}

static void WidowExplosion7(edict_t *self)
{
    int    n;
    vec3_t f, r, u, startpoint;
    vec3_t offset = { -20.11f, -1.11f, 40.76f };

    AngleVectors(self->s.angles, &f, &r, &u);
    startpoint = G_ProjectSource2(self->s.origin, offset, f, r, u);

    G_TempEntity(startpoint, EV_EXPLOSION1, 0);

    for (n = 0; n < 1; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_NONE, startpoint, false);
    for (n = 0; n < 1; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, startpoint, false);
    for (n = 0; n < 2; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 300, GIB_METALLIC, startpoint, false);
}

static void WidowExplosionLeg(edict_t *self)
{
    vec3_t f, r, u, startpoint;
    vec3_t offset1 = { -31.89f, -47.86f, 67.02f };
    vec3_t offset2 = { -44.9f, -82.14f, 54.72f };

    AngleVectors(self->s.angles, &f, &r, &u);
    startpoint = G_ProjectSource2(self->s.origin, offset1, f, r, u);

    G_TempEntity(startpoint, EV_EXPLOSION1_BIG, 0);

    ThrowWidowGibSized(self, "models/monsters/blackwidow2/gib2/tris.md2", 200, GIB_METALLIC, startpoint,
                       G_SoundIndex("misc/fhit3.wav"), false);
    ThrowWidowGibLoc(self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_NONE, startpoint, false);
    ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, startpoint, false);

    startpoint = G_ProjectSource2(self->s.origin, offset2, f, r, u);

    G_TempEntity(startpoint, EV_EXPLOSION1, 0);

    ThrowWidowGibSized(self, "models/monsters/blackwidow2/gib1/tris.md2", 300, GIB_METALLIC, startpoint,
                       G_SoundIndex("misc/fhit3.wav"), false);
    ThrowWidowGibLoc(self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_NONE, startpoint, false);
    ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, startpoint, false);
}

static void ThrowArm1(edict_t *self)
{
    int    n;
    vec3_t f, r, u, startpoint;
    vec3_t offset1 = { 65.76f, 17.52f, 7.56f };

    AngleVectors(self->s.angles, &f, &r, &u);
    startpoint = G_ProjectSource2(self->s.origin, offset1, f, r, u);

    G_TempEntity(startpoint, EV_EXPLOSION1_BIG, 0);

    for (n = 0; n < 2; n++)
        ThrowWidowGibLoc(self, "models/objects/gibs/sm_metal/tris.md2", 100, GIB_METALLIC, startpoint, false);
}

static void ThrowArm2(edict_t *self)
{
    vec3_t f, r, u, startpoint;
    vec3_t offset1 = { 65.76f, 17.52f, 7.56f };

    AngleVectors(self->s.angles, &f, &r, &u);
    startpoint = G_ProjectSource2(self->s.origin, offset1, f, r, u);

    ThrowWidowGibSized(self, "models/monsters/blackwidow2/gib4/tris.md2", 200, GIB_METALLIC, startpoint,
                       G_SoundIndex("misc/fhit3.wav"), false);
    ThrowWidowGibLoc(self, "models/objects/gibs/sm_meat/tris.md2", 300, GIB_NONE, startpoint, false);
}
