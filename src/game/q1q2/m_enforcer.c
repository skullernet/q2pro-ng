/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/
// m_army.c

#include "g_local.h"
#include "m_enforcer.h"

#define SOUND   sound[!!self->style]

enum { Plain, Proto, Strogg };

static struct {
    int death;
    int search;
    int pain[2];
    int sight[4];
} sound[2];

// Stand
static const mframe_t enforcer_frames_stand[] = {
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand }
};
const mmove_t MMOVE_T(enforcer_move_stand) = { FRAME_stand01, FRAME_stand07, enforcer_frames_stand, NULL };

void MONSTERINFO_STAND(enforcer_stand)(edict_t *self)
{
    M_SetAnimation(self, &enforcer_move_stand);
}

// Run
static const mframe_t enforcer_frames_run[] = {
    { ai_run, 18 },
    { ai_run, 14 },
    { ai_run, 7, monster_footstep },
    { ai_run, 12 },

    { ai_run, 14 },
    { ai_run, 14 },
    { ai_run, 7, monster_footstep },
    { ai_run, 11 }
};
const mmove_t MMOVE_T(enforcer_move_run) = { FRAME_run01, FRAME_run08, enforcer_frames_run, NULL };

void MONSTERINFO_RUN(enforcer_run)(edict_t *self)
{
    M_SetAnimation(self, &enforcer_move_run);
}

// Run
static const mframe_t enforcer_frames_walk[] = {
    { ai_walk, 2, monster_footstep },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 3 },

    { ai_walk, 1 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 1 },

    { ai_walk, 2, monster_footstep },
    { ai_walk, 4 },
    { ai_walk, 4 },
    { ai_walk, 1 },

    { ai_walk, 2 },
    { ai_walk, 3 },
    { ai_walk, 4 },
    { ai_walk, 2 }
};
const mmove_t MMOVE_T(enforcer_move_walk) = { FRAME_walk01, FRAME_walk16, enforcer_frames_walk, NULL };

void MONSTERINFO_WALK(enforcer_walk)(edict_t *self)
{
    M_SetAnimation(self, &enforcer_move_walk);
}

// Attack
void TOUCH(enfbolt_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    edict_t *owner = &g_edicts[self->r.ownernum];

    if (other == owner)
        return;

    if (tr->surface_flags & SURF_SKY) {
        G_FreeEdict(self);
        return;
    }

    if (other->takedamage) {
        T_Damage(other, self, owner, self->velocity, self->s.origin, tr->plane.dir, self->dmg, 1, DAMAGE_ENERGY, MOD_UNKNOWN);
        G_FreeEdict(self);
    } else {
        G_BecomeEvent(self, EV_ENFORCER_BOLT, tr->plane.dir);
    }
}

static void fire_enfbolt(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed)
{
    edict_t *bolt;

    bolt = G_SpawnMissile(self, start, dir, speed);
    bolt->flags |= FL_DODGE;
    bolt->s.effects |= EF_HYPERBLASTER;
    bolt->s.modelindex = G_ModelIndex("models/monsters/laserstrogg/tris.md2");
    bolt->touch = enfbolt_touch;
    bolt->nextthink = level.time + SEC(2);
    bolt->think = G_FreeEdict;
    bolt->dmg = damage;
    trap_LinkEntity(bolt);

    G_CheckMissileImpact(self, bolt);
}

static void FireEnforcerBolt(edict_t *self)
{
    vec3_t  forward, right;
    vec3_t  start;
    vec3_t  dir;
    vec3_t  vec;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_ENFORCER_BOLT], forward, right);

    vec = self->enemy->s.origin;
    vec.z += self->enemy->viewheight;
    dir = Vec3_Direction(vec, start);

    fire_enfbolt(self, start, dir, 15, 600);
    G_AddEvent(self, EV_MUZZLEFLASH2, MZ2_ENFORCER_BOLT);
}

static void FireEnforcerFlechette(edict_t *self, int type)
{
    vec3_t start, aim, forward, right;
    monster_muzzleflash_id_t flash_number;

    AngleVectors(self->s.angles, &forward, &right, NULL);

    if (self->style == Strogg)
        flash_number = MZ2_ENFORCER_FLECHETTE2_L + type;
    else
        flash_number = MZ2_ENFORCER_FLECHETTE1_L + type;

    start = M_ProjectFlashSource(self, monster_flash_offset[flash_number], forward, right);

    M_PredictAim(self, self->enemy, start, 800, false, frandom() * 0.3f, &aim, NULL);
    for (int i = 0; i < 3; i++)
        aim.xyz[i] += crandom_open() * 0.025f;

    monster_fire_flechette(self, start, aim, 4, 800, flash_number);
}

static void enforcer_fire1(edict_t *self)
{
    if (!self->enemy || !self->enemy->r.inuse)
        return;

    if (self->style)
        FireEnforcerFlechette(self, 0);
    else if (self->s.frame == FRAME_attack06)
        FireEnforcerBolt(self);
}

static void enforcer_fire2(edict_t *self)
{
    if (!self->enemy || !self->enemy->r.inuse)
        return;

    if (self->style)
        FireEnforcerFlechette(self, 1);
}

static const mframe_t enforcer_frames_attack2[] = {
    { ai_charge },
    { ai_charge, 0, enforcer_fire1 },
    { ai_charge, 0, enforcer_fire2 },
    { ai_charge, 0, enforcer_fire1 }
};
const mmove_t MMOVE_T(enforcer_move_attack2) = { FRAME_attack05, FRAME_attack08, enforcer_frames_attack2, enforcer_run };

void MONSTERINFO_ATTACK(enforcer_attack_again)(edict_t *self)
{
    //self->s.frame = FRAME_attack05;
    M_SetAnimation(self, &enforcer_move_attack2);
}

static const mframe_t enforcer_frames_attack1[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },

    { ai_charge },
    { ai_charge, 0, enforcer_fire2 },
    { ai_charge, 0, enforcer_fire1 },
    { ai_charge, 0, enforcer_fire2 }
};
const mmove_t MMOVE_T(enforcer_move_attack1) = { FRAME_attack01, FRAME_attack08, enforcer_frames_attack1, enforcer_attack_again };

void MONSTERINFO_ATTACK(enforcer_attack)(edict_t *self)
{
    M_SetAnimation(self, &enforcer_move_attack1);
}

// Pain (1)
static const mframe_t enforcer_frames_pain1[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(enforcer_move_pain1) = { FRAME_paina01, FRAME_paina04, enforcer_frames_pain1, enforcer_run };

// Pain (2)
static const mframe_t enforcer_frames_pain2[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },

    { ai_move }
};
const mmove_t MMOVE_T(enforcer_move_pain2) = { FRAME_painb01, FRAME_painb05, enforcer_frames_pain2, enforcer_run };

// Pain (3)
static const mframe_t enforcer_frames_pain3[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },

    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(enforcer_move_pain3) = { FRAME_painc01, FRAME_painc08, enforcer_frames_pain3, enforcer_run };

// Pain (4)
static const mframe_t enforcer_frames_pain4[] = {
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 2 },

    { ai_move, 1 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },

    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 1 },
    { ai_move, 1 },

    { ai_move, 1 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 1 },

    { ai_move, 1 },
    { ai_move, 0 },
    { ai_move, 0 }
};
const mmove_t MMOVE_T(enforcer_move_pain4) = { FRAME_paind01, FRAME_paind19, enforcer_frames_pain4, enforcer_run };

// Pain
void PAIN(enforcer_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    float r;

    if (self->pain_debounce_time > level.time)
        return;

    G_StartSound(self, CHAN_VOICE, random_element(SOUND.pain), 1, ATTN_NORM);

    r = frandom();
    if (r < 0.2f)
        self->pain_debounce_time = level.time + SEC(6);
    else
        self->pain_debounce_time = level.time + SEC(1);

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    if (r < 0.2f)
        M_SetAnimation(self, &enforcer_move_pain1);
    else if (r < 0.4f)
        M_SetAnimation(self, &enforcer_move_pain2);
    else if (r < 0.7f)
        M_SetAnimation(self, &enforcer_move_pain3);
    else
        M_SetAnimation(self, &enforcer_move_pain4);
}

void MONSTERINFO_SETSKIN(enforcer_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

static void enforcer_dead(edict_t *self)
{
    self->r.box.maxs.z = -8 * G_EntityScale(self);
    monster_dead(self);
}

static void enforcer_shrink(edict_t *self)
{
    self->r.box.maxs.z = 8 * G_EntityScale(self);
    self->r.svflags |= SVF_DEADMONSTER;
    trap_LinkEntity(self);
}

// Death (1)
static const mframe_t enforcer_frames_death1[] = {
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 14, enforcer_shrink },

    { ai_move, 2 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },

    { ai_move, 3 },
    { ai_move, 5 },
    { ai_move, 5 },
    { ai_move, 5 },

    { ai_move, 0 },
    { ai_move, 0 }
};
const mmove_t MMOVE_T(enforcer_move_death1) = { FRAME_death01, FRAME_death14, enforcer_frames_death1, enforcer_dead };

// Death (2)
static const mframe_t enforcer_frames_death2[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, enforcer_shrink },

    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },

    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(enforcer_move_death2) = { FRAME_fdeath01, FRAME_fdeath11, enforcer_frames_death2, enforcer_dead };

static const gib_def_t enforcer_gibs_plain[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/objects/gibs/head2/tris.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t enforcer_gibs_proto[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/monsters/enforcer_prototype/gibs/g_arm.md2", 2 },
    { "models/monsters/enforcer_prototype/gibs/g_leg.md2", 2 },
    { "models/monsters/enforcer_prototype/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t enforcer_gibs_strogg[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/monsters/enforcerstrogg/gibs/g_arm.md2", 2 },
    { "models/monsters/enforcerstrogg/gibs/g_leg.md2", 2 },
    { "models/monsters/enforcerstrogg/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

// Death
void DIE(enforcer_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    if (self->health <= self->gib_health) {
        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        if (self->style == Strogg)
            ThrowGibs(self, damage, enforcer_gibs_strogg);
        else if (self->style == Proto)
            ThrowGibs(self, damage, enforcer_gibs_proto);
        else
            ThrowGibs(self, damage, enforcer_gibs_plain);
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;

    G_StartSound(self, CHAN_VOICE, SOUND.death, 1, ATTN_NORM);

    self->deadflag = true;
    self->takedamage = true;

    if (self->style && brandom())
        Drop_Item(self, GetItemByIndex(IT_AMMO_FLECHETTES))->count = 25;

    if (brandom())
        M_SetAnimation(self, &enforcer_move_death1);
    else
        M_SetAnimation(self, &enforcer_move_death2);
}

// Sight
void MONSTERINFO_SIGHT(enforcer_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_VOICE, random_element(SOUND.sight), 1, ATTN_NORM);
}

// Search
void MONSTERINFO_SEARCH(enforcer_search)(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.search, 1, ATTN_NORM);
}

void PR_monster_enforcer(void)
{
    sound[0].death = G_SoundIndex("enforcer/death1.wav");
    sound[0].search = G_SoundIndex("enforcer/idle1.wav");
    sound[0].pain[0] = G_SoundIndex("enforcer/pain1.wav");
    sound[0].pain[1] = G_SoundIndex("enforcer/pain2.wav");
    sound[0].sight[0] = G_SoundIndex("enforcer/sight1.wav");
    sound[0].sight[1] = G_SoundIndex("enforcer/sight2.wav");
    sound[0].sight[2] = G_SoundIndex("enforcer/sight3.wav");
    sound[0].sight[3] = G_SoundIndex("enforcer/sight4.wav");

    G_SoundIndex("enforcer/enfstop.wav");
    G_SoundIndex("enforcer/enfire.wav");
}

void PR_monster_enforcer_strogg(void)
{
    sound[1].death = G_SoundIndex("enforcer/death1_s.wav");
    sound[1].search = G_SoundIndex("enforcer/idle1_s.wav");
    sound[1].pain[0] = G_SoundIndex("enforcer/pain1_s.wav");
    sound[1].pain[1] = G_SoundIndex("enforcer/pain2_s.wav");
    sound[1].sight[0] = G_SoundIndex("enforcer/sight1_s.wav");
    sound[1].sight[1] = G_SoundIndex("enforcer/sight2_s.wav");
    sound[1].sight[2] = G_SoundIndex("enforcer/sight3_s.wav");
    sound[1].sight[3] = G_SoundIndex("enforcer/sight4_s.wav");

    G_SoundIndex("weapons/spike2.wav");
    G_SoundIndex("weapons/nail1b.wav");
}

static void SP_monster_enforcer_x(edict_t *self)
{
    self->r.box = Box3_FromSize(16, -24, 40);
    self->r.solid = SOLID_BBOX;
    self->movetype = MOVETYPE_STEP;

    self->health *= st.health_multiplier;
    self->gib_health = -35;
    self->mass = 120;

    self->pain = enforcer_pain;
    self->die = enforcer_die;
    self->monsterinfo.stand = enforcer_stand;
    self->monsterinfo.walk = enforcer_walk;
    self->monsterinfo.run = enforcer_run;
    self->monsterinfo.attack = enforcer_attack;
    self->monsterinfo.sight = enforcer_sight;
    self->monsterinfo.search = enforcer_search;
    self->monsterinfo.setskin = enforcer_setskin;

    M_SetAnimation(self, &enforcer_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    walkmonster_start(self);
}

void SP_monster_enforcer(edict_t *self)
{
    self->style = Plain;
    self->health = 80;
    self->s.modelindex = G_ModelIndex("models/monsters/enforcer/tris.md2");
    SP_monster_enforcer_x(self);
}

void SP_monster_enforcer_prototype(edict_t *self)
{
    self->style = Proto;
    self->health = 100;
    self->s.modelindex = G_ModelIndex("models/monsters/enforcer_prototype/tris.md2");
    SP_monster_enforcer_x(self);
    G_PrecacheGibs(enforcer_gibs_proto);
}

void SP_monster_enforcer_strogg(edict_t *self)
{
    self->style = Strogg;
    self->health = 100;
    self->flags |= FL_DEEPONE;
    self->s.modelindex = G_ModelIndex("models/monsters/enforcerstrogg/tris.md2");
    SP_monster_enforcer_x(self);
    G_PrecacheGibs(enforcer_gibs_strogg);
}
