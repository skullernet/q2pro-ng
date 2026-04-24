// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

MEDIC

==============================================================================
*/

#include "g_local.h"
#include "m_medic.h"

enum { Medic, Commander, Shamedic };

#define MEDIC_MIN_DISTANCE          32
#define MEDIC_MAX_HEAL_DISTANCE     400
#define MEDIC_TRY_TIME              SEC(10)

// FIXME -
//
// owner moved to monsterinfo.healer instead
//
// For some reason, the healed monsters are rarely ending up in the floor
//
// 5/15/1998 I think I fixed these, keep an eye on them

#define SOUND   sound[self->style & 1]

static struct {
    int idle1;
    int pain1;
    int pain2;
    int die;
    int sight;
    int search;
    int hook_launch;
    int hook_hit;
    int hook_heal;
    int hook_retract;
    int spawn;
} sound[2];

#define DEFAULT_REINFORCEMENTS      "monster_soldier_light 1;monster_soldier 2;monster_soldier_ss 2;monster_infantry 3;monster_gunner 4;monster_medic 5;monster_gladiator 6"
#define DEFAULT_MONSTER_SLOTS_BASE  3

static const vec3_t reinforcement_position[MAX_REINFORCEMENTS] = {
   { 80, 0, 0 },
   { 40, 60, 0 },
   { 40, -60, 0 },
   { 0, 80, 0 },
   { 0, -80, 0 }
};

// pick an array of reinforcements to use
int M_PickReinforcements(edict_t *self, int max_slots)
{
    for (int i = 0; i < MAX_REINFORCEMENTS; i++)
        self->monsterinfo.chosen_reinforcements[i] = 255;

    if (!max_slots)
        max_slots = MAX_REINFORCEMENTS;

    // decide how many things we want to spawn;
    // this is on a logarithmic scale
    // so we don't spawn too much too often.
    int num_slots = Q_log2(irandom2(2, BIT(max_slots) + 1));

    // we only have this many slots left to use
    int remaining = self->monsterinfo.monster_slots - self->monsterinfo.monster_used;
    const reinforcement_list_t *r = &self->monsterinfo.reinforcements;

    int num_chosen = 0;
    while (num_chosen < num_slots) {
        byte available[MAX_REINFORCEMENTS_TOTAL];
        int num_avail = 0;

        // ran out of slots!
        if (remaining <= 0)
            break;

        // get everything we could choose
        for (int i = 0; i < r->num_reinforcements; i++)
            if (r->reinforcements[i].strength <= remaining)
                available[num_avail++] = i;

        // can't pick any
        if (!num_avail)
            break;

        // select monster, TODO fairly
        int chosen = available[irandom1(num_avail)];
        self->monsterinfo.chosen_reinforcements[num_chosen++] = chosen;

        remaining -= r->reinforcements[chosen].strength;
    }

    return num_chosen;
}

void M_SetupReinforcements(const char *reinforcements, reinforcement_list_t *list)
{
    list->reinforcements = NULL;
    list->num_reinforcements = 0;

    if (!*reinforcements)
        return;

    // count up the semicolons
    int count = 1;

    for (int i = 0; reinforcements[i]; i++)
        if (reinforcements[i] == ';')
            count++;

    if (count > MAX_REINFORCEMENTS_TOTAL)
        G_Error("Too many reinforcements");

    // allocate
    list->reinforcements = G_Malloc(sizeof(list->reinforcements[0]) * count);

    // parse
    ED_InitSpawnVars();

    char copy[MAX_STRING_CHARS];
    Q_strlcpy(copy, reinforcements, sizeof(copy));

    const char *s = copy;
    while (1) {
        char *p = strchr(s, ';');
        if (p)
            *p = 0;

        const char *token = COM_Parse(&s);
        if (*token) {
            Q_assert(list->num_reinforcements < count);
            reinforcement_t *r = &list->reinforcements[list->num_reinforcements];

            r->classname = G_CopyString(token);
            r->strength = Q_atoi(COM_Parse(&s));

            edict_t *newEnt = G_Spawn();
            newEnt->classname = r->classname;
            newEnt->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

            ED_CallSpawn(newEnt);
            if (newEnt->r.inuse) {
                r->box = newEnt->r.box;
                r->radius = Box3_Radius(r->box);
                G_FreeEdict(newEnt);
                list->num_reinforcements++;
            }
        }

        if (!p)
            break;
        s = p + 1;
    }
}

void fixHealerEnemy(edict_t *self)
{
    if (self->oldenemy && self->oldenemy->r.inuse && self->oldenemy->health > 0) {
        self->enemy = self->oldenemy;
        HuntTarget(self, false);
    } else {
        self->enemy = self->goalentity = NULL;
        self->oldenemy = NULL;
        if (!FindTarget(self)) {
            // no valid enemy, so stop acting
            self->monsterinfo.pausetime = HOLD_FOREVER;
            return;
        }
    }
}

void cleanupHeal(edict_t *self)
{
    // clean up target, if we have one and it's legit
    if (self->enemy && self->enemy->r.inuse)
        cleanupHealTarget(self->enemy);

    fixHealerEnemy(self);
}

static bool isMedic(edict_t *ent)
{
    if (!ent || !ent->r.inuse)
        return false;
    if (!strncmp(ent->classname, "monster_medic", 13))
        return true;
    if (!strcmp(ent->classname, "monster_shamedic"))
        return true;
    return false;
}

void abortHeal(edict_t *self, bool gib, bool mark)
{
    int              hurt;

    if (self->enemy && self->enemy->r.inuse && !self->enemy->client && (self->monsterinfo.aiflags & AI_MEDIC)) {
        cleanupHealTarget(self->enemy);

        // gib em!
        if (mark) {
            // if the first badMedic slot is filled by a medic, skip it and use the second one
            if (isMedic(self->enemy->monsterinfo.badMedic1))
                self->enemy->monsterinfo.badMedic2 = self;
            else
                self->enemy->monsterinfo.badMedic1 = self;
        }

        if (gib) {
            // [Paril-KEX] health added in case of weird edge case
            // with fixbot "healing" the corpses
            if (self->enemy->gib_health)
                hurt = -self->enemy->gib_health + max(0, self->enemy->health);
            else
                hurt = 500;

            T_Damage(self->enemy, self, self, vec3_origin, self->enemy->s.origin,
                     DIRTOBYTE_UP, hurt, 0, DAMAGE_NONE, MOD_UNKNOWN);
        }

        cleanupHeal(self);
    }

    self->monsterinfo.aiflags &= ~AI_MEDIC;
    self->monsterinfo.medicTries = 0;
}

bool finishHeal(edict_t *self)
{
    edict_t *healee = self->enemy;

    healee->spawnflags = SPAWNFLAG_NONE;
    healee->monsterinfo.aiflags &= AI_RESPAWN_MASK;
    healee->target = NULL;
    healee->targetname = NULL;
    healee->combattarget = NULL;
    healee->deathtarget = NULL;
    healee->healthtarget = NULL;
    healee->itemtarget = NULL;
    healee->monsterinfo.healer = self;

    vec3_t maxs = healee->r.box.maxs;
    maxs.z += 48; // compensate for change when they die

    trace_t tr = G_Trace(healee->s.origin, healee->s.origin, healee->r.box,
                         healee->s.number, MASK_MONSTERSOLID);

    if (tr.startsolid || tr.allsolid) {
        abortHeal(self, true, false);
        return false;
    }

    if (tr.entnum != ENTITYNUM_WORLD) {
        abortHeal(self, true, false);
        return false;
    }

    healee->monsterinfo.aiflags |= AI_IGNORE_SHOTS | AI_DO_NOT_COUNT;

    // backup & restore health stuff, because of multipliers
    int old_max_health = healee->max_health;
    item_id_t old_power_armor_type = healee->monsterinfo.initial_power_armor_type;
    int old_power_armor_power = healee->monsterinfo.max_power_armor_power;
    int old_base_health = healee->monsterinfo.base_health;
    int old_health_scaling = healee->monsterinfo.health_scaling;
    reinforcement_list_t reinforcements = healee->monsterinfo.reinforcements;
    int slots_from_commander = healee->monsterinfo.slots_from_commander;
    int monster_slots = healee->monsterinfo.monster_slots;
    int monster_used = healee->monsterinfo.monster_used;
    int old_gib_health = healee->gib_health;

    ED_InitSpawnVars();
    ED_SetKeySpecified("reinforcements");
    st.reinforcements = "";

    ED_CallSpawn(healee);

    healee->monsterinfo.slots_from_commander = slots_from_commander;
    healee->monsterinfo.reinforcements = reinforcements;
    healee->monsterinfo.monster_slots = monster_slots;
    healee->monsterinfo.monster_used = monster_used;

    healee->gib_health = old_gib_health / 2;
    healee->health = healee->max_health = old_max_health;
    healee->monsterinfo.power_armor_power = healee->monsterinfo.max_power_armor_power = old_power_armor_power;
    healee->monsterinfo.power_armor_type = healee->monsterinfo.initial_power_armor_type = old_power_armor_type;
    healee->monsterinfo.base_health = old_base_health;
    healee->monsterinfo.health_scaling = old_health_scaling;

    if (healee->monsterinfo.setskin)
        healee->monsterinfo.setskin(healee);

    if (healee->think) {
        healee->nextthink = level.time;
        healee->think(healee);
    }

    healee->monsterinfo.aiflags &= ~AI_RESURRECTING;
    healee->monsterinfo.aiflags |= AI_IGNORE_SHOTS | AI_DO_NOT_COUNT;
    // turn off flies
    healee->s.effects &= ~EF_FLIES;
    healee->monsterinfo.healer = NULL;

    // switch our enemy
    fixHealerEnemy(self);

    // switch revivee's enemy
    healee->oldenemy = NULL;
    healee->enemy = self->enemy;

    if (healee->enemy)
        FoundTarget(healee);
    else {
        healee->enemy = NULL;
        if (!FindTarget(healee)) {
            // no valid enemy, so stop acting
            healee->monsterinfo.pausetime = HOLD_FOREVER;
            healee->monsterinfo.stand(healee);
        }
    }

    cleanupHeal(self);
    return true;
}

#if 0
static bool canReach(edict_t *self, edict_t *other)
{
    vec3_t  spot1;
    vec3_t  spot2;
    trace_t trace;

    spot1 = self->s.origin;
    spot1.z += self->viewheight;
    spot2 = other->s.origin;
    spot2.z += other->viewheight;
    trace = G_TraceLine(spot1, spot2, self->s.number, MASK_PROJECTILE | MASK_WATER);
    return trace.fraction == 1.0f || trace.ent == other;
}
#endif

edict_t *healFindMonster(edict_t *self, float radius)
{
    edict_t *ent = NULL;
    edict_t *best = NULL;

    while ((ent = findradius(ent, self->s.origin, radius)) != NULL) {
        if (ent == self)
            continue;
        if (!(ent->r.svflags & SVF_MONSTER))
            continue;
        if (ent->monsterinfo.aiflags & AI_GOOD_GUY)
            continue;
        // check to make sure we haven't bailed on this guy already
        if ((ent->monsterinfo.badMedic1 == self) || (ent->monsterinfo.badMedic2 == self))
            continue;
        if (ent->monsterinfo.healer)
            // FIXME - this is correcting a bug that is somewhere else
            // if the healer is a monster, and it's in medic mode .. continue .. otherwise
            //   we will override the healer, if it passes all the other tests
            if ((ent->monsterinfo.healer->r.inuse) && (ent->monsterinfo.healer->health > 0) &&
                (ent->monsterinfo.healer->r.svflags & SVF_MONSTER) && (ent->monsterinfo.healer->monsterinfo.aiflags & AI_MEDIC))
                continue;
        if (ent->health > 0)
            continue;
        if ((ent->nextthink) && (ent->think != monster_dead_think))
            continue;
        if (!visible(self, ent))
            continue;
        if (!strncmp(ent->classname, "player", 6)) // stop it from trying to heal player_noise entities
            continue;
        // FIXME - there's got to be a better way ..
        // make sure we don't spawn people right on top of us
        if (realrange(self, ent) <= MEDIC_MIN_DISTANCE)
            continue;
        if (!best) {
            best = ent;
            continue;
        }
        if (ent->max_health <= best->max_health)
            continue;
        best = ent;
    }

    return best;
}

static edict_t *medic_FindDeadMonster(edict_t *self)
{
    float    radius;

    if (self->monsterinfo.react_to_damage_time > level.time)
        return NULL;

    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
        radius = MEDIC_MAX_HEAL_DISTANCE;
    else
        radius = 1024;

    edict_t *best = healFindMonster(self, radius);

    if (best)
        self->timestamp = level.time + MEDIC_TRY_TIME;

    return best;
}

void MONSTERINFO_IDLE(medic_idle)(edict_t *self)
{
    edict_t *ent;

    G_StartSound(self, CHAN_VOICE, SOUND.idle1, 1, ATTN_IDLE);

    if (!self->oldenemy) {
        ent = medic_FindDeadMonster(self);
        if (ent) {
            self->oldenemy = self->enemy;
            self->enemy = ent;
            self->enemy->monsterinfo.healer = self;
            self->monsterinfo.aiflags |= AI_MEDIC;
            FoundTarget(self);
        }
    }
}

void MONSTERINFO_SEARCH(medic_search)(edict_t *self)
{
    edict_t *ent;

    G_StartSound(self, CHAN_VOICE, SOUND.search, 1, ATTN_IDLE);

    if (!self->oldenemy) {
        ent = medic_FindDeadMonster(self);
        if (ent) {
            self->oldenemy = self->enemy;
            self->enemy = ent;
            self->enemy->monsterinfo.healer = self;
            self->monsterinfo.aiflags |= AI_MEDIC;
            FoundTarget(self);
        }
    }
}

void MONSTERINFO_SIGHT(medic_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_VOICE, SOUND.sight, 1, ATTN_NORM);
}

static const mframe_t medic_frames_stand[] = {
    { ai_stand, 0, medic_idle },
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
};
const mmove_t MMOVE_T(medic_move_stand) = { FRAME_wait1, FRAME_wait90, medic_frames_stand, NULL };

void MONSTERINFO_STAND(medic_stand)(edict_t *self)
{
    M_SetAnimation(self, &medic_move_stand);
}

static const mframe_t medic_frames_walk[] = {
    { ai_walk, 6.2f },
    { ai_walk, 18.1f, monster_footstep },
    { ai_walk, 1 },
    { ai_walk, 9 },
    { ai_walk, 10 },
    { ai_walk, 9 },
    { ai_walk, 11 },
    { ai_walk, 11.6f, monster_footstep },
    { ai_walk, 2 },
    { ai_walk, 9.9f },
    { ai_walk, 14 },
    { ai_walk, 9.3f }
};
const mmove_t MMOVE_T(medic_move_walk) = { FRAME_walk1, FRAME_walk12, medic_frames_walk, NULL };

void MONSTERINFO_WALK(medic_walk)(edict_t *self)
{
    M_SetAnimation(self, &medic_move_walk);
}

static const mframe_t medic_frames_run[] = {
    { ai_run, 18 },
    { ai_run, 22.5f, monster_footstep },
    { ai_run, 25.4f, monster_done_dodge },
    { ai_run, 23.4f, monster_footstep },
    { ai_run, 24 },
    { ai_run, 35.6f }
};
const mmove_t MMOVE_T(medic_move_run) = { FRAME_run1, FRAME_run6, medic_frames_run, NULL };

void MONSTERINFO_RUN(medic_run)(edict_t *self)
{
    monster_done_dodge(self);

    if (!(self->monsterinfo.aiflags & AI_MEDIC)) {
        edict_t *ent;

        ent = medic_FindDeadMonster(self);
        if (ent) {
            self->oldenemy = self->enemy;
            self->enemy = ent;
            self->enemy->monsterinfo.healer = self;
            self->monsterinfo.aiflags |= AI_MEDIC;
            FoundTarget(self);
            return;
        }
    }

    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
        M_SetAnimation(self, &medic_move_stand);
    else
        M_SetAnimation(self, &medic_move_run);
}

static const mframe_t medic_frames_pain1[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(medic_move_pain1) = { FRAME_paina2, FRAME_paina6, medic_frames_pain1, medic_run };

static const mframe_t medic_frames_pain2[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, monster_footstep },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, monster_footstep }
};
const mmove_t MMOVE_T(medic_move_pain2) = { FRAME_painb2, FRAME_painb13, medic_frames_pain2, medic_run };

void PAIN(medic_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    monster_done_dodge(self);

    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + SEC(3);

    float r = frandom();

    if (self->style == Commander) {
        if (damage < 35) {
            G_StartSound(self, CHAN_VOICE, SOUND.pain1, 1, ATTN_NORM);
            if (mod != MOD_CHAINFIST)
                return;
        }
        G_StartSound(self, CHAN_VOICE, SOUND.pain2, 1, ATTN_NORM);
    } else if (r < 0.5f)
        G_StartSound(self, CHAN_VOICE, SOUND.pain1, 1, ATTN_NORM);
    else
        G_StartSound(self, CHAN_VOICE, SOUND.pain2, 1, ATTN_NORM);

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    // if we're healing someone, we ignore pain
    if (mod != MOD_CHAINFIST && (self->monsterinfo.aiflags & AI_MEDIC))
        return;

    if (self->style == Commander) {
        self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
        self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;

        if (r < min(damage * 0.005f, 0.5f)) // no more than 50% chance of big pain
            M_SetAnimation(self, &medic_move_pain2);
        else
            M_SetAnimation(self, &medic_move_pain1);
    } else if (r < 0.5f)
        M_SetAnimation(self, &medic_move_pain1);
    else
        M_SetAnimation(self, &medic_move_pain2);

    // PMM - clear duck flag
    if (self->monsterinfo.aiflags & AI_DUCKED)
        monster_duck_up(self);

    abortHeal(self, false, false);
}

void MONSTERINFO_SETSKIN(medic_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum |= 1;
    else
        self->s.skinnum &= ~1;
}

static void medic_fire_blaster(edict_t *self)
{
    vec3_t    start;
    vec3_t    forward, right;
    vec3_t    end;
    vec3_t    dir;
    effects_t effect;
    int       damage = 2;
    monster_muzzleflash_id_t mz;

    // paranoia checking
    if (!(self->enemy && self->enemy->r.inuse))
        return;

    if ((self->s.frame == FRAME_attack9) || (self->s.frame == FRAME_attack12)) {
        effect = EF_BLASTER;
        damage = 6;
        mz = (self->style == Commander) ? MZ2_MEDIC_BLASTER_2 : MZ2_MEDIC_BLASTER_1;
    } else {
        effect = (self->s.frame % 4) ? EF_NONE : EF_HYPERBLASTER;
        mz = ((self->style == Commander) ? MZ2_MEDIC_HYPERBLASTER2_1 : MZ2_MEDIC_HYPERBLASTER1_1) + (self->s.frame - FRAME_attack19);
    }

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[mz], forward, right);

    end = self->enemy->s.origin;
    end.z += self->enemy->viewheight;
    dir = Vec3_Direction(end, start);

    if (!strcmp(self->enemy->classname, "tesla_mine"))
        damage = 3;

    // medic commander shoots blaster2
    if (self->style == Commander)
        monster_fire_blaster2(self, start, dir, damage, 1000, mz, effect);
    else
        monster_fire_blaster(self, start, dir, damage, 1000, mz, effect);
}

static void ShamedicCastLightningWithOffset(edict_t *self, vec3_t offset, edict_t **beam)
{
    if (!self->enemy || !self->enemy->r.inuse)
        return;

    vec3_t forward, right, start, end, dir;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, offset, forward, right);

    M_PredictAim(self, self->enemy, start, 0, false, 0.1f, &dir, NULL);

    end = Vec3_MA(start, 8192, dir);
    trace_t tr = G_TraceLine(start, end, self->s.number, MASK_PROJECTILE | CONTENTS_SLIME | CONTENTS_LAVA);

    if (!*beam)
        *beam = G_SpawnLightning(self);

    (*beam)->s.old_origin = G_SnapVector(start);
    (*beam)->s.origin = G_SnapVectorTowards(tr.endpos, start);
    trap_LinkEntity(*beam);

    fire_bullet(self, start, dir, irandom2(1, 4), 15, 0, 0, MOD_TESLA);
}

static void ShamedicDoubleLightningAttack(edict_t *self)
{
    ShamedicCastLightningWithOffset(self, Vec3(64.0f, 22.5f, 74.0f), &self->beam);
    ShamedicCastLightningWithOffset(self, Vec3(64.0f, -26.5f, 70.0f), &self->beam2);
}

static void ShamedicCastLeftLightning(edict_t *self)
{
    ShamedicCastLightningWithOffset(self, Vec3(64.0f, 9.5f, 84.0f), &self->beam);
}

static void medic_dead(edict_t *self)
{
    if (self->style == Shamedic)
        self->r.box = Box3_FromSize(32, 0, 24);
    else
        self->r.box = Box3_FromSize(16, -24, -8);
    monster_dead(self);
}

static void medic_shrink(edict_t *self)
{
    if (self->style != Shamedic)
        self->r.box.maxs.z = -2;
    self->r.svflags |= SVF_DEADMONSTER;
    trap_LinkEntity(self);
}

static const mframe_t medic_frames_death[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, -18, monster_footstep },
    { ai_move, -10, medic_shrink },
    { ai_move, -6 },
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
    { ai_move, 0, monster_footstep },
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
const mmove_t MMOVE_T(medic_move_death) = { FRAME_death2, FRAME_death30, medic_frames_death, medic_dead };

static const gib_def_t medic_gibs[] = {
    { "models/objects/gibs/bone/tris.md2", 2 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/objects/gibs/sm_metal/tris.md2", 1, GIB_METALLIC },
    { "models/monsters/medic/gibs/chest.md2", 1, GIB_SKINNED },
    { "models/monsters/medic/gibs/leg.md2", 2, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/medic/gibs/hook.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/medic/gibs/gun.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/medic/gibs/head.md2", 1, GIB_SKINNED | GIB_HEAD },
    { 0 }
};

static const gib_def_t shamedic_gibs[] = {
    { "models/objects/gibs/bone/tris.md2", 2 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/objects/gibs/sm_metal/tris.md2", 1, GIB_METALLIC },
    { "models/monsters/medic/gibs/chest.md2", 1, GIB_SKINNED },
    { "models/monsters/shamblerthunder/gibs/g_leg.md2", 2, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/medic/gibs/hook.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/shamblerthunder/gibs/g_arm.md2", 1, GIB_SKINNED | GIB_UPRIGHT },
    { "models/monsters/shamblerthunder/gibs/g_head.md2", 1, GIB_SKINNED | GIB_HEAD },
    { 0 }
};

void DIE(medic_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    // if we had a pending patient, he was already freed up in Killed
    M_FreeBeams(self);

    // check for gib
    if (M_CheckGib(self, mod)) {
        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        self->s.skinnum /= 2;
        if (self->style == Shamedic)
            ThrowGibs(self, damage, shamedic_gibs);
        else
            ThrowGibs(self, damage, medic_gibs);
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;

    // regular death
    G_StartSound(self, CHAN_VOICE, SOUND.die, 1, ATTN_NORM);

    self->deadflag = true;
    self->takedamage = true;

    M_SetAnimation(self, &medic_move_death);
}

static const mframe_t medic_frames_duck[] = {
    { ai_move, -1 },
    { ai_move, -1, monster_duck_down },
    { ai_move, -1, monster_duck_hold },
    { ai_move, -1 },
    { ai_move, -1 },
    { ai_move, -1 }, // PMM - duck up used to be here
    { ai_move, -1 },
    { ai_move, -1 },
    { ai_move, -1 },
    { ai_move, -1 },
    { ai_move, -1 },
    { ai_move, -1 },
    { ai_move, -1, monster_duck_up }
};
const mmove_t MMOVE_T(medic_move_duck) = { FRAME_duck2, FRAME_duck14, medic_frames_duck, medic_run };

// PMM -- moved dodge code to after attack code so I can reference attack frames

static const mframe_t medic_frames_attackHyperBlaster[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, medic_fire_blaster },
    { ai_charge, 0, medic_fire_blaster },
    { ai_charge, 0, medic_fire_blaster },
    { ai_charge, 0, medic_fire_blaster },
    { ai_charge, 0, medic_fire_blaster },
    { ai_charge, 0, medic_fire_blaster },
    { ai_charge, 0, medic_fire_blaster },
    { ai_charge, 0, medic_fire_blaster },
    { ai_charge, 0, medic_fire_blaster },
    { ai_charge, 0, medic_fire_blaster },
    { ai_charge, 0, medic_fire_blaster },
    { ai_charge, 0, medic_fire_blaster },
    { ai_charge },
    { ai_charge },
    // [Paril-KEX] end on 36 as intended
    { ai_charge, 2 }, // 33
    { ai_charge, 3, monster_footstep },
};
const mmove_t MMOVE_T(medic_move_attackHyperBlaster) = { FRAME_attack15, FRAME_attack34, medic_frames_attackHyperBlaster, medic_run };

static const mframe_t medic_frames_attackDoubleLightning[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, ShamedicDoubleLightningAttack },
    { ai_charge, 0, ShamedicDoubleLightningAttack },
    { ai_charge, 0, ShamedicDoubleLightningAttack },
    { ai_charge, 0, ShamedicDoubleLightningAttack },
    { ai_charge, 0, ShamedicDoubleLightningAttack },
    { ai_charge, 0, ShamedicDoubleLightningAttack },
    { ai_charge, 0, ShamedicDoubleLightningAttack },
    { ai_charge, 0, ShamedicDoubleLightningAttack },
    { ai_charge, 0, ShamedicDoubleLightningAttack },
    { ai_charge, 0, ShamedicDoubleLightningAttack },
    { ai_charge, 0, ShamedicDoubleLightningAttack },
    { ai_charge, 0, ShamedicDoubleLightningAttack },
    { ai_charge },
    { ai_charge },
    // [Paril-KEX] end on 36 as intended
    { ai_charge, 2, M_FreeBeams }, // 33
    { ai_charge, 3, monster_footstep },
};
const mmove_t MMOVE_T(medic_move_attackDoubleLightning) = { FRAME_attack15, FRAME_attack34, medic_frames_attackDoubleLightning, medic_run };

static void medic_quick_attack(edict_t *self)
{
    if (brandom()) {
        if (self->style == Shamedic)
            M_SetAnimationEx(self, &medic_move_attackDoubleLightning, false);
        else
            M_SetAnimationEx(self, &medic_move_attackHyperBlaster, false);
        self->monsterinfo.nextframe = FRAME_attack16;
    }
}

static void medic_continue(edict_t *self)
{
    M_FreeBeams(self);

    if (visible(self, self->enemy) && frandom() <= 0.95f) {
        if (self->style == Shamedic)
            M_SetAnimationEx(self, &medic_move_attackDoubleLightning, false);
        else
            M_SetAnimationEx(self, &medic_move_attackHyperBlaster, false);
    }
}

static const mframe_t medic_frames_attackBlaster[] = {
    { ai_charge, 5 },
    { ai_charge, 3 },
    { ai_charge, 2 },
    { ai_charge, 0, medic_quick_attack },
    { ai_charge, 0, monster_footstep },
    { ai_charge },
    { ai_charge, 0, medic_fire_blaster },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, medic_fire_blaster },
    { ai_charge },
    { ai_charge, 0, medic_continue } // Change to medic_continue... Else, go to frame 32
};
const mmove_t MMOVE_T(medic_move_attackBlaster) = { FRAME_attack3, FRAME_attack14, medic_frames_attackBlaster, medic_run };

static const mframe_t medic_frames_attackLightning[] = {
    { ai_charge, 5 },
    { ai_charge, 3 },
    { ai_charge, 2 },
    { ai_charge, 0, medic_quick_attack },
    { ai_charge, 0, monster_footstep },
    { ai_charge },
    { ai_charge, 0, ShamedicCastLeftLightning },
    { ai_charge, 0, ShamedicCastLeftLightning },
    { ai_charge, 0, ShamedicCastLeftLightning },
    { ai_charge, 0, ShamedicCastLeftLightning },
    { ai_charge },
    { ai_charge, 0, medic_continue } // Change to medic_continue... Else, go to frame 32
};
const mmove_t MMOVE_T(medic_move_attackLightning) = { FRAME_attack3, FRAME_attack14, medic_frames_attackLightning, medic_run };

static void medic_hook_launch(edict_t *self)
{
    G_StartSound(self, CHAN_WEAPON, SOUND.hook_launch, 1, ATTN_NORM);
}

static vec3_t medic_cable_offsets[] = {
    { 45.0f, -9.2f, 15.5f },
    { 48.4f, -9.7f, 15.2f },
    { 47.8f, -9.8f, 15.8f },
    { 47.3f, -9.3f, 14.3f },
    { 45.4f, -10.1f, 13.1f },
    { 41.9f, -12.7f, 12.0f },
    { 37.8f, -15.8f, 11.2f },
    { 34.3f, -18.4f, 10.7f },
    { 32.7f, -19.7f, 10.4f },
    { 32.7f, -19.7f, 10.4f }
};

static void medic_cable_attack(edict_t *self)
{
    vec3_t  start, end, offset, f, r;
    trace_t tr;

    if ((!self->enemy) || (!self->enemy->r.inuse) || (self->enemy->s.effects & EF_GIB)) {
        abortHeal(self, false, false);
        return;
    }

    // we switched back to a player; let the animation finish
    if (self->enemy->client)
        return;

    // see if our enemy has changed to a client, or our target has more than 0 health,
    // abort it .. we got switched to someone else due to damage
    if (self->enemy->health > 0) {
        abortHeal(self, false, false);
        return;
    }

    if (self->style == Shamedic)
        offset = Vec3(80.0f, -10.0f, 60.0f); // FIXME
    else
        offset = medic_cable_offsets[self->s.frame - FRAME_attack42];

    AngleVectors(self->s.angles, &f, &r, NULL);
    start = M_ProjectFlashSource(self, offset, f, r);

    // check for max distance
    // not needed, done in checkattack
    // check for min distance
    if (Vec3_Distance(start, self->enemy->s.origin) < MEDIC_MIN_DISTANCE) {
        abortHeal(self, true, false);
        self->monsterinfo.nextframe = FRAME_attack52;
        return;
    }

    tr = G_TraceLine(start, self->enemy->s.origin, self->s.number, MASK_SOLID);
    if (tr.fraction != 1.0f && tr.entnum != self->enemy->s.number) {
        if (tr.entnum == ENTITYNUM_WORLD) {
            // give up on second try
            if (self->monsterinfo.medicTries > 1) {
                abortHeal(self, false, true);
                self->monsterinfo.nextframe = FRAME_attack52;
                return;
            }
            self->monsterinfo.medicTries++;
            cleanupHeal(self);
            return;
        }
        abortHeal(self, false, false);
        self->monsterinfo.nextframe = FRAME_attack52;
        return;
    }

    if (self->s.frame == FRAME_attack43) {
        G_StartSound(self->enemy, CHAN_AUTO, SOUND.hook_hit, 1, ATTN_NORM);

        self->enemy->monsterinfo.aiflags |= AI_RESURRECTING;
        self->enemy->takedamage = false;
        M_SetEffects(self->enemy);
    } else if (self->s.frame == FRAME_attack50) {
        if (!finishHeal(self))
            self->monsterinfo.nextframe = FRAME_attack52;
        return;
    } else {
        if (self->s.frame == FRAME_attack44)
            G_StartSound(self, CHAN_WEAPON, SOUND.hook_heal, 1, ATTN_NORM);
    }

    // adjust start for beam origin being in middle of a segment
    start = Vec3_MA(start, 8, f);

    // adjust end z for end spot since the monster is currently dead
    end = self->enemy->s.origin;
    end.z = (self->enemy->r.absbox.mins.z + self->enemy->r.absbox.maxs.z) / 2;

    edict_t *te = self->beam;
    if (!te) {
        self->beam = te = G_Spawn();
        te->s.renderfx = RF_BEAM;
        te->s.modelindex = G_ModelIndex("models/monsters/parasite/segment/tris.md2");
        te->s.othernum = ENTITYNUM_NONE;
        te->s.alpha = self->s.alpha;
        te->s.scale = self->s.scale;
        te->r.ownernum = self->s.number;
    }

    te->s.old_origin = G_SnapVector(start);
    te->s.origin = G_SnapVector(end);
    trap_LinkEntity(te);
}

static void medic_hook_retract(edict_t *self)
{
    G_StartSound(self, CHAN_WEAPON, SOUND.hook_retract, 1, ATTN_NORM);

    M_FreeBeams(self);

    self->monsterinfo.aiflags &= ~AI_MEDIC;

    fixHealerEnemy(self);
}

static const mframe_t medic_frames_attackCable[] = {
    // ROGUE - negated 36-40 so he scoots back from his target a little
    // ROGUE - switched 33-36 to ai_charge
    // ROGUE - changed frame 52 to 60 to compensate for changes in 36-40
    // [Paril-KEX] started on 36 as they intended
    { ai_charge, -4.7f }, // 37
    { ai_charge, -5.0f },
    { ai_charge, -6.0f },
    { ai_charge, -4.0f }, // 40
    { ai_charge, 0, monster_footstep },
    { ai_move, 0, medic_hook_launch },  // 42
    { ai_move, 0, medic_cable_attack }, // 43
    { ai_move, 0, medic_cable_attack },
    { ai_move, 0, medic_cable_attack },
    { ai_move, 0, medic_cable_attack },
    { ai_move, 0, medic_cable_attack },
    { ai_move, 0, medic_cable_attack },
    { ai_move, 0, medic_cable_attack },
    { ai_move, 0, medic_cable_attack },
    { ai_move, 0, medic_cable_attack }, // 51
    { ai_move, 0, medic_hook_retract }, // 52
    { ai_move, -1.5f },
    { ai_move, -1.2f, monster_footstep },
    { ai_move, -3.0f }
};
const mmove_t MMOVE_T(medic_move_attackCable) = { FRAME_attack37, FRAME_attack55, medic_frames_attackCable, medic_run };

static void medic_start_spawn(edict_t *self)
{
    G_StartSound(self, CHAN_WEAPON, SOUND.spawn, 1, ATTN_NORM);
    self->monsterinfo.nextframe = FRAME_attack48;
}

static void medic_determine_spawn(edict_t *self)
{
    vec3_t f, r, offset, startpoint, spawnpoint;

    AngleVectors(self->s.angles, &f, &r, NULL);

    int num_summoned = M_PickReinforcements(self, 0);

    float scale = self->s.scale;
    if (!scale)
        scale = 1;

    for (int spin = 0; spin < 2; spin++)
        for (int count = 0; count < num_summoned; count++) {
            offset = Vec3_Scale(reinforcement_position[count], scale);

            // see if we have any success by spinning around
            if (spin) {
                offset.x = -offset.x;
                offset.y = -offset.y;
            }

            startpoint = M_ProjectFlashSource(self, offset, f, r);

            // a little off the ground
            startpoint.z += 10 * scale;

            const reinforcement_t *reinforcement = &self->monsterinfo.reinforcements.reinforcements[self->monsterinfo.chosen_reinforcements[count]];

            if (!FindSpawnPoint(startpoint, reinforcement->box, &spawnpoint, 32, true))
                continue;
            if (!CheckGroundSpawnPoint(spawnpoint, reinforcement->box, 256, -1))
                continue;

            if (spin) {
                self->monsterinfo.aiflags |= AI_MANUAL_STEERING;
                self->ideal_yaw = anglemod(self->s.angles.yaw + 180);
            }

            // we found a spot, we're done here
            return;
        }

    // did not succeed
    self->monsterinfo.nextframe = FRAME_attack53;
}

static void medic_spawngrows(edict_t *self)
{
    vec3_t f, r, offset, startpoint, spawnpoint;
    int    num_success = 0;
    float  current_yaw;

    // if we've been directed to turn around
    if (self->monsterinfo.aiflags & AI_MANUAL_STEERING) {
        current_yaw = anglemod(self->s.angles.yaw);
        if (fabsf(current_yaw - self->ideal_yaw) > 0.1f) {
            self->monsterinfo.aiflags |= AI_HOLD_FRAME;
            return;
        }

        // done turning around
        self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
        self->monsterinfo.aiflags &= ~AI_MANUAL_STEERING;
    }

    AngleVectors(self->s.angles, &f, &r, NULL);

    float scale = self->s.scale;
    if (!scale)
        scale = 1;

    for (int i = 0; i < MAX_REINFORCEMENTS; i++) {
        if (self->monsterinfo.chosen_reinforcements[i] == 255)
            break;

        offset = Vec3_Scale(reinforcement_position[i], scale);

        startpoint = M_ProjectFlashSource(self, offset, f, r);

        // a little off the ground
        startpoint.z += 10 * scale;

        const reinforcement_t *reinforcement = &self->monsterinfo.reinforcements.reinforcements[self->monsterinfo.chosen_reinforcements[i]];

        if (!FindSpawnPoint(startpoint, reinforcement->box, &spawnpoint, 32, true))
            continue;
        if (!CheckGroundSpawnPoint(spawnpoint, reinforcement->box, 256, -1))
            continue;

        spawnpoint = Vec3_Add(spawnpoint, Box3_Center(reinforcement->box));
        SpawnGrow_Spawn(spawnpoint, reinforcement->radius, reinforcement->radius * 2);

        num_success++;
    }

    if (num_success == 0)
        self->monsterinfo.nextframe = FRAME_attack53;
}

static void medic_finish_spawn(edict_t *self)
{
    edict_t *ent;
    vec3_t   f, r, offset, startpoint, spawnpoint;
    edict_t *designated_enemy;

    AngleVectors(self->s.angles, &f, &r, NULL);

    float scale = self->s.scale;
    if (!scale)
        scale = 1;

    for (int i = 0; i < MAX_REINFORCEMENTS; i++) {
        if (self->monsterinfo.chosen_reinforcements[i] == 255)
            break;

        offset = Vec3_Scale(reinforcement_position[i], scale);

        startpoint = M_ProjectFlashSource(self, offset, f, r);

        // a little off the ground
        startpoint.z += 10 * scale;

        const reinforcement_t *reinforcement = &self->monsterinfo.reinforcements.reinforcements[self->monsterinfo.chosen_reinforcements[i]];

        if (!FindSpawnPoint(startpoint, reinforcement->box, &spawnpoint, 32, true))
            continue;
        if (!CheckSpawnPoint(spawnpoint, reinforcement->box))
            continue;

        ent = CreateGroundMonster(spawnpoint, self->s.angles, reinforcement->box, reinforcement->classname, 256);
        if (!ent)
            continue;

        if (ent->think) {
            ent->nextthink = level.time;
            ent->think(ent);
        }

        ent->monsterinfo.aiflags |= AI_IGNORE_SHOTS | AI_DO_NOT_COUNT | AI_SPAWNED_COMMANDER | AI_SPAWNED_NEEDS_GIB;
        ent->monsterinfo.commander = self;
        ent->monsterinfo.slots_from_commander = reinforcement->strength;
        self->monsterinfo.monster_used += reinforcement->strength;

        if (self->monsterinfo.aiflags & AI_MEDIC)
            designated_enemy = self->oldenemy;
        else
            designated_enemy = self->enemy;

        if (coop.integer) {
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
        } else {
            ent->enemy = NULL;
            ent->monsterinfo.stand(ent);
        }
    }
}

static const mframe_t medic_frames_callReinforcements[] = {
    // ROGUE - 33-36 now ai_charge
    { ai_charge, 2 }, // 33
    { ai_charge, 3 },
    { ai_charge, 5 },
    { ai_charge, 4.4f }, // 36
    { ai_charge, 4.7f },
    { ai_charge, 5 },
    { ai_charge, 6 },
    { ai_charge, 4 }, // 40
    { ai_charge, 0, monster_footstep },
    { ai_move, 0, medic_start_spawn }, // 42
    { ai_move },                       // 43 -- 43 through 47 are skipped
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, medic_determine_spawn }, // 48
    { ai_charge, 0, medic_spawngrows },    // 49
    { ai_move },                           // 50
    { ai_move },                           // 51
    { ai_move, -15, medic_finish_spawn },  // 52
    { ai_move, -1.5f },
    { ai_move, -1.2f },
    { ai_move, -3, monster_footstep }
};
const mmove_t MMOVE_T(medic_move_callReinforcements) = { FRAME_attack33, FRAME_attack55, medic_frames_callReinforcements, medic_run };

void MONSTERINFO_ATTACK(medic_attack)(edict_t *self)
{
    monster_done_dodge(self);

    float enemy_range = range_to(self, self->enemy);

    // signal from checkattack to spawn
    if (self->monsterinfo.aiflags & AI_BLOCKED) {
        M_SetAnimation(self, &medic_move_callReinforcements);
        self->monsterinfo.aiflags &= ~AI_BLOCKED;
    }

    float r = frandom();
    if (self->monsterinfo.aiflags & AI_MEDIC) {
        if ((self->style == Commander) && (r > 0.8f) && M_SlotsLeft(self))
            M_SetAnimation(self, &medic_move_callReinforcements);
        else
            M_SetAnimation(self, &medic_move_attackCable);
    } else {
        if (self->monsterinfo.attack_state == AS_BLIND) {
            M_SetAnimation(self, &medic_move_callReinforcements);
            return;
        }
        if ((self->style == Commander) && (r > 0.2f) && (enemy_range > RANGE_MELEE) && M_SlotsLeft(self))
            M_SetAnimation(self, &medic_move_callReinforcements);
        else if (self->style == Shamedic)
            M_SetAnimation(self, &medic_move_attackLightning);
        else
            M_SetAnimation(self, &medic_move_attackBlaster);
    }
}

bool MONSTERINFO_CHECKATTACK(medic_checkattack)(edict_t *self)
{
    if (self->monsterinfo.aiflags & AI_MEDIC) {
        // if our target went away
        if ((!self->enemy) || (!self->enemy->r.inuse)) {
            abortHeal(self, false, false);
            self->monsterinfo.nextframe = FRAME_attack52;
            return false;
        }

        // if we ran out of time, give up
        if (self->timestamp < level.time) {
            abortHeal(self, false, true);
            self->monsterinfo.nextframe = FRAME_attack52;
            self->timestamp = 0;
            return false;
        }

        if (realrange(self, self->enemy) < MEDIC_MAX_HEAL_DISTANCE + 10) {
            medic_attack(self);
            return true;
        }

        self->monsterinfo.attack_state = AS_STRAIGHT;
        return false;
    }

    if (self->enemy->client && !visible(self, self->enemy) && M_SlotsLeft(self)) {
        self->monsterinfo.attack_state = AS_BLIND;
        return true;
    }

    // give a LARGE bias to spawning things when we have room
    // use AI_BLOCKED as a signal to attack to spawn
    if (self->monsterinfo.monster_slots && (frandom() < 0.8f) && (M_SlotsLeft(self) > self->monsterinfo.monster_slots * 0.8f) && (realrange(self, self->enemy) > 150)) {
        self->monsterinfo.aiflags |= AI_BLOCKED;
        self->monsterinfo.attack_state = AS_MISSILE;
        return true;
    }

    // ROGUE
    // since his idle animation looks kinda bad in combat, always attack
    // when he's on a combat point
    if (self->monsterinfo.aiflags & AI_STAND_GROUND) {
        self->monsterinfo.attack_state = AS_MISSILE;
        return true;
    }

    return M_CheckAttack(self);
}

static bool medic_is_shooting(edict_t *self)
{
    return ((self->monsterinfo.active_move == &medic_move_attackHyperBlaster) ||
            (self->monsterinfo.active_move == &medic_move_attackDoubleLightning) ||
            (self->monsterinfo.active_move == &medic_move_attackCable) ||
            (self->monsterinfo.active_move == &medic_move_attackBlaster) ||
            (self->monsterinfo.active_move == &medic_move_attackLightning) ||
            (self->monsterinfo.active_move == &medic_move_callReinforcements));
}

bool MONSTERINFO_DUCK(medic_duck)(edict_t *self, gtime_t eta)
{
    //  don't dodge if you're healing
    if (self->monsterinfo.aiflags & AI_MEDIC)
        return false;

    if (medic_is_shooting(self)) {
        // he ignores skill
        self->monsterinfo.unduck(self);
        return false;
    }

    M_SetAnimation(self, &medic_move_duck);

    return true;
}

bool MONSTERINFO_SIDESTEP(medic_sidestep)(edict_t *self)
{
    if (medic_is_shooting(self)) {
        // if we're shooting, don't dodge
        return false;
    }

    if (self->monsterinfo.active_move != &medic_move_run)
        M_SetAnimation(self, &medic_move_run);

    return true;
}

//===========
// PGM
bool MONSTERINFO_BLOCKED(medic_blocked)(edict_t *self, float dist)
{
    if (blocked_checkplat(self, dist))
        return true;

    return false;
}
// PGM
//===========

void PR_monster_medic(void)
{
    sound[0].idle1 = G_SoundIndex("medic/idle.wav");
    sound[0].pain1 = G_SoundIndex("medic/medpain1.wav");
    sound[0].pain2 = G_SoundIndex("medic/medpain2.wav");
    sound[0].die = G_SoundIndex("medic/meddeth1.wav");
    sound[0].sight = G_SoundIndex("medic/medsght1.wav");
    sound[0].search = G_SoundIndex("medic/medsrch1.wav");
    sound[0].hook_launch = G_SoundIndex("medic/medatck2.wav");
    sound[0].hook_hit = G_SoundIndex("medic/medatck3.wav");
    sound[0].hook_heal = G_SoundIndex("medic/medatck4.wav");
    sound[0].hook_retract = G_SoundIndex("medic/medatck5.wav");
}

void PR_monster_medic_commander(void)
{
    sound[1].idle1 = G_SoundIndex("medic_commander/medidle.wav");
    sound[1].pain1 = G_SoundIndex("medic_commander/medpain1.wav");
    sound[1].pain2 = G_SoundIndex("medic_commander/medpain2.wav");
    sound[1].die = G_SoundIndex("medic_commander/meddeth.wav");
    sound[1].sight = G_SoundIndex("medic_commander/medsght.wav");
    sound[1].search = G_SoundIndex("medic_commander/medsrch.wav");
    sound[1].hook_launch = G_SoundIndex("medic_commander/medatck2c.wav");
    sound[1].hook_hit = G_SoundIndex("medic_commander/medatck3a.wav");
    sound[1].hook_heal = G_SoundIndex("medic_commander/medatck4a.wav");
    sound[1].hook_retract = G_SoundIndex("medic_commander/medatck5a.wav");
    sound[1].spawn = G_SoundIndex("medic_commander/monsterspawn1.wav");
}

static void SP_monster_medic_x(edict_t *self)
{
    self->movetype = MOVETYPE_STEP;
    self->r.solid = SOLID_BBOX;

    self->pain = medic_pain;
    self->die = medic_die;

    self->monsterinfo.stand = medic_stand;
    self->monsterinfo.walk = medic_walk;
    self->monsterinfo.run = medic_run;
    // pmm
    self->monsterinfo.dodge = M_MonsterDodge;
    self->monsterinfo.duck = medic_duck;
    self->monsterinfo.unduck = monster_duck_up;
    self->monsterinfo.sidestep = medic_sidestep;
    self->monsterinfo.blocked = medic_blocked;
    // pmm
    self->monsterinfo.attack = medic_attack;
    self->monsterinfo.melee = NULL;
    self->monsterinfo.sight = medic_sight;
    self->monsterinfo.idle = medic_idle;
    self->monsterinfo.search = medic_search;
    self->monsterinfo.checkattack = medic_checkattack;
    self->monsterinfo.setskin = medic_setskin;

    trap_LinkEntity(self);

    M_SetAnimation(self, &medic_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    walkmonster_start(self);

    // PMM
    self->monsterinfo.aiflags |= AI_IGNORE_SHOTS;
    // pmm
}

/*QUAKED monster_medic (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
model="models/monsters/medic/tris.md2"
*/
void SP_monster_medic(edict_t *self)
{
    self->style = Medic;
    self->health = 300 * st.health_multiplier;
    self->gib_health = -130;
    self->mass = 400;
    self->s.skinnum = 0;
    self->s.modelindex = G_ModelIndex("models/monsters/medic/tris.md2");
    self->r.box = Box3_FromSize(24, -24, 32);

    G_SoundIndex("medic/medatck1.wav");
    G_PrecacheGibs(medic_gibs);

    SP_monster_medic_x(self);
}

/*QUAKED monster_medic_commander (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
 */
void SP_monster_medic_commander(edict_t *self)
{
    self->style = Commander;
    self->health = 600 * st.health_multiplier;
    self->gib_health = -130;
    self->mass = 600;
    self->s.skinnum = 2;
    self->yaw_speed = 40; // default is 20
    self->s.modelindex = G_ModelIndex("models/monsters/medic/tris.md2");
    self->r.box = Box3_FromSize(24, -24, 32);

    // commander sounds
    G_SoundIndex("tank/tnkatck3.wav");
    G_ModelIndex("models/items/spawngro3/tris.md2");
    G_PrecacheGibs(medic_gibs);

    const char *reinforcements = DEFAULT_REINFORCEMENTS;

    if (!ED_WasKeySpecified("monster_slots"))
        self->monsterinfo.monster_slots = DEFAULT_MONSTER_SLOTS_BASE;
    if (ED_WasKeySpecified("reinforcements"))
        reinforcements = st.reinforcements;

    if (self->monsterinfo.monster_slots && reinforcements && *reinforcements) {
        if (skill.integer)
            self->monsterinfo.monster_slots += floorf(self->monsterinfo.monster_slots * (skill.value / 2));

        M_SetupReinforcements(reinforcements, &self->monsterinfo.reinforcements);
    }

    SP_monster_medic_x(self);
}

/*QUAKED monster_shamedic (1 .5 0) (-16 -16 -24) (16 16 32) Ambush Trigger_Spawn Sight
model="models/monsters/shamblerthunder/tris.md2"
*/
void SP_monster_shamedic(edict_t *self)
{
    self->style = Shamedic;
    self->health = 550 * st.health_multiplier;
    self->gib_health = -130;
    self->mass = 400;
    self->s.skinnum = 0;
    self->s.modelindex = G_ModelIndex("models/monsters/shamblerthunder/tris.md2");
    self->r.box = Box3_FromSize(32, 0, 96);

    G_SoundIndex("medic/medatck1.wav");
    G_ModelIndex("models/proj/lightning/tris.md2");
    G_PrecacheGibs(shamedic_gibs);

    SP_monster_medic_x(self);
}
