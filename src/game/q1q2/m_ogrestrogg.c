// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

ogrestrogg

==============================================================================
*/

#include "g_local.h"
#include "m_ogrestrogg.h"

static int sound_pain1;
static int sound_pain2;
static int sound_die;
static int sound_cleaver_swing;
static int sound_cleaver_hit;
static int sound_cleaver_miss;
static int sound_idle;
static int sound_search;
static int sound_sight;

void MONSTERINFO_IDLE(ogrestrogg_idle)(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, sound_idle, 1, ATTN_IDLE);
}

void MONSTERINFO_SIGHT(ogrestrogg_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_VOICE, sound_sight, 1, ATTN_NORM);
}

void MONSTERINFO_SEARCH(ogrestrogg_search)(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, sound_search, 1, ATTN_NORM);
}

static void ogrestrogg_cleaver_swing(edict_t *self)
{
    G_StartSound(self, CHAN_WEAPON, sound_cleaver_swing, 1, ATTN_NORM);
}

static const mframe_t ogrestrogg_frames_stand[] = {
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand }
};
const mmove_t MMOVE_T(ogrestrogg_move_stand) = { FRAME_stand1, FRAME_stand7, ogrestrogg_frames_stand, NULL };

void MONSTERINFO_STAND(ogrestrogg_stand)(edict_t *self)
{
    M_SetAnimation(self, &ogrestrogg_move_stand);
}

static const mframe_t ogrestrogg_frames_walk[] = {
    { ai_walk, 15 },
    { ai_walk, 7 },
    { ai_walk, 6 },
    { ai_walk, 5 },
    { ai_walk, 2, monster_footstep },
    { ai_walk },
    { ai_walk, 2 },
    { ai_walk, 8 },
    { ai_walk, 12 },
    { ai_walk, 8 },
    { ai_walk, 5 },
    { ai_walk, 5 },
    { ai_walk, 2, monster_footstep },
    { ai_walk, 2 },
    { ai_walk, 1 },
    { ai_walk, 8 }
};
const mmove_t MMOVE_T(ogrestrogg_move_walk) = { FRAME_walk1, FRAME_walk16, ogrestrogg_frames_walk, NULL };

void MONSTERINFO_WALK(ogrestrogg_walk)(edict_t *self)
{
    M_SetAnimation(self, &ogrestrogg_move_walk);
}

static const mframe_t ogrestrogg_frames_run[] = {
    { ai_run, 23 },
    { ai_run, 14 },
    { ai_run, 14, monster_footstep },
    { ai_run, 21 },
    { ai_run, 12 },
    { ai_run, 13, monster_footstep }
};
const mmove_t MMOVE_T(ogrestrogg_move_run) = { FRAME_run1, FRAME_run6, ogrestrogg_frames_run, NULL };

void MONSTERINFO_RUN(ogrestrogg_run)(edict_t *self)
{
    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
        M_SetAnimation(self, &ogrestrogg_move_stand);
    else
        M_SetAnimation(self, &ogrestrogg_move_run);
}

static void ogrestroggMelee(edict_t *self)
{
    vec3_t aim = { MELEE_DISTANCE, self->r.box.mins.x, -4 };
    if (fire_hit(self, aim, irandom2(20, 25), 300))
        G_StartSound(self, CHAN_AUTO, sound_cleaver_hit, 1, ATTN_NORM);
    else {
        G_StartSound(self, CHAN_AUTO, sound_cleaver_miss, 1, ATTN_NORM);
        self->monsterinfo.melee_debounce_time = level.time + SEC(1.5f);
    }
}

static const mframe_t ogrestrogg_frames_attack_melee[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, ogrestrogg_cleaver_swing },
    { ai_charge },
    { ai_charge, 0, ogrestroggMelee },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, ogrestrogg_cleaver_swing },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, ogrestroggMelee },
    { ai_charge },
    { ai_charge }
};
const mmove_t MMOVE_T(ogrestrogg_move_attack_melee) = { FRAME_melee3, FRAME_melee16, ogrestrogg_frames_attack_melee, ogrestrogg_run };

void MONSTERINFO_MELEE(ogrestrogg_melee)(edict_t *self)
{
    M_SetAnimation(self, &ogrestrogg_move_attack_melee);
}

// Grenade
static void OgreGrenade(edict_t *self)
{
    vec3_t start, forward, right;
    float range;
    
    if (!self->enemy || !self->enemy->r.inuse)
        return;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_OGRE_GRENADE_S], forward, right);

    range = Vec3_Distance(self->s.origin, self->enemy->s.origin);
    range = max(range, 300.0f);

    monster_fire_grenade(self, start, forward, 50, 2.0f * range, MZ2_OGRE_GRENADE_S, -50, 0);
}

static const mframe_t ogrestrogg_frames_attack_gun[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, OgreGrenade },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, monster_footstep },
    { ai_charge }
};
const mmove_t MMOVE_T(ogrestrogg_move_attack_gun) = { FRAME_attack1, FRAME_attack9, ogrestrogg_frames_attack_gun, ogrestrogg_run };

void MONSTERINFO_ATTACK(ogrestrogg_attack)(edict_t *self)
{
    M_SetAnimation(self, &ogrestrogg_move_attack_gun);
}

static void ogrestrogg_pain_sound(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        G_StartSound(self, CHAN_VOICE, sound_pain2, 1, ATTN_NORM);
    else
        G_StartSound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM);
}

static const mframe_t ogrestrogg_frames_pain[] = {
    { ai_move },
    { ai_move, 0, ogrestrogg_pain_sound },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(ogrestrogg_move_pain) = { FRAME_pain2, FRAME_pain5, ogrestrogg_frames_pain, ogrestrogg_run };

static const mframe_t ogrestrogg_frames_pain_air[] = {
    { ai_move },
    { ai_move, 0, ogrestrogg_pain_sound },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(ogrestrogg_move_pain_air) = { FRAME_painup2, FRAME_painup6, ogrestrogg_frames_pain_air, ogrestrogg_run };

void PAIN(ogrestrogg_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (level.time < self->pain_debounce_time) {
        if ((self->velocity.z > 100) && (self->monsterinfo.active_move == &ogrestrogg_move_pain))
            M_SetAnimation(self, &ogrestrogg_move_pain_air);
        return;
    }

    self->pain_debounce_time = level.time + SEC(3);

    if (!M_ShouldReactToPain(self, mod))
        return;

    if (self->velocity.z > 100)
        M_SetAnimation(self, &ogrestrogg_move_pain_air);
    else
        M_SetAnimation(self, &ogrestrogg_move_pain);
}

void MONSTERINFO_SETSKIN(ogrestrogg_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum |= 1;
    else
        self->s.skinnum &= ~1;
}

static void ogrestrogg_dead(edict_t *self)
{
    self->r.box = Box3_FromSize(16, -24, -8);
    monster_dead(self);
}

static void ogrestrogg_shrink(edict_t *self)
{
    self->r.box.maxs.z = 0;
    self->r.svflags |= SVF_DEADMONSTER;
    trap_LinkEntity(self);
}

static void ogrestrogg_death_sound(edict_t *self)
{
    G_StartSound(self, CHAN_BODY, sound_die, 1, ATTN_NORM);
}

static const mframe_t ogrestrogg_frames_death[] = {
    { ai_move },
    { ai_move, 0, ogrestrogg_death_sound },
    { ai_move, 0, ogrestrogg_shrink },
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
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(ogrestrogg_move_death) = { FRAME_death2, FRAME_death22, ogrestrogg_frames_death, ogrestrogg_dead };

static const gib_def_t ogrestrogg_gibs[] = {
    { "models/objects/gibs/bone/tris.md2", 2 },
    { "models/objects/gibs/sm_meat/tris.md2", 2 },
    { "models/monsters/gladiatr/gibs/thigh.md2", 2, GIB_SKINNED },
    { "models/monsters/gladiatr/gibs/chest.md2", 1, GIB_SKINNED },
    { "models/monsters/ogrestrogg/gibs/g_arm.md2", 2, GIB_SKINNED },
    { "models/monsters/ogrestrogg/gibs/g_leg.md2", 2, GIB_SKINNED },
    { "models/monsters/ogrestrogg/gibs/g_head.md2", 1, GIB_SKINNED | GIB_HEAD },
    { 0 }
};

void DIE(ogrestrogg_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    if (M_CheckGib(self, mod)) {
        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        self->s.skinnum /= 2;
        ThrowGibs(self, damage, ogrestrogg_gibs);
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;

    self->deadflag = true;
    self->takedamage = true;

    G_StartSound(self, CHAN_VOICE, sound_die, 1, ATTN_NORM);

    if (brandom())
        Drop_Item(self, GetItemByIndex(IT_AMMO_GRENADES))->count = 2;

    M_SetAnimation(self, &ogrestrogg_move_death);
}

bool MONSTERINFO_BLOCKED(ogrestrogg_blocked)(edict_t *self, float dist)
{
    return blocked_checkplat(self, dist);
}

void PR_monster_ogre_strogg(void)
{
    sound_pain1 = G_SoundIndex("ogre/ogpain1_s.wav");
    sound_pain2 = G_SoundIndex("ogre/ogpain1_s.wav");
    sound_die = G_SoundIndex("ogre/ogdth_s.wav");
    sound_cleaver_swing = G_SoundIndex("gladiator/melee1.wav");
    sound_cleaver_hit = G_SoundIndex("gladiator/melee2.wav");
    sound_cleaver_miss = G_SoundIndex("gladiator/melee3.wav");
    sound_idle = G_SoundIndex("ogre/ogidle2_s.wav");
    sound_search = G_SoundIndex("gladiator/gldsrch1.wav");
    sound_sight = G_SoundIndex("ogre/ogwake_s.wav");
}

void SP_monster_ogre_strogg(edict_t *self)
{
    self->s.modelindex = G_ModelIndex("models/monsters/ogrestrogg/tris.md2");

    G_PrecacheGibs(ogrestrogg_gibs);

    self->r.solid = SOLID_BBOX;
    self->r.box = Box3_FromSize(32, -24, 42);
    self->movetype = MOVETYPE_STEP;

    self->health = 450 * st.health_multiplier;
    self->mass = 400;
    self->gib_health = -175;

    self->pain = ogrestrogg_pain;
    self->die = ogrestrogg_die;
    self->monsterinfo.stand = ogrestrogg_stand;
    self->monsterinfo.walk = ogrestrogg_walk;
    self->monsterinfo.run = ogrestrogg_run;
    self->monsterinfo.dodge = NULL;
    self->monsterinfo.attack = ogrestrogg_attack;
    self->monsterinfo.melee = ogrestrogg_melee;
    self->monsterinfo.sight = ogrestrogg_sight;
    self->monsterinfo.idle = ogrestrogg_idle;
    self->monsterinfo.search = ogrestrogg_search;
    self->monsterinfo.blocked = ogrestrogg_blocked;
    self->monsterinfo.setskin = ogrestrogg_setskin;

    trap_LinkEntity(self);

    M_SetAnimation(self, &ogrestrogg_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    walkmonster_start(self);
}
