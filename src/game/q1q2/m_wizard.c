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
// m_wizard.c

#include "g_local.h"
#include "m_wizard.h"

#define SOUND   sound[!!self->style]

enum { Plain, Proto, Strogg, Wizarcuda };

static struct {
    int attack;
    int death;
    int idle1;
    int idle2;
    int pain;
    int sight;
    int beam;
} sound[2];

// Stand
static const mframe_t wizard_frames_stand[] = {
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
const mmove_t MMOVE_T(wizard_move_stand) = { FRAME_hover01, FRAME_hover15, wizard_frames_stand, NULL };

void MONSTERINFO_STAND(wizard_stand)(edict_t *self)
{
    M_SetAnimation(self, &wizard_move_stand);
}

// Walk
static const mframe_t wizard_frames_walk[] = {
	{ ai_walk, 16 },
	{ ai_walk, 16 },
	{ ai_walk, 16 },
	{ ai_walk, 16 },
	{ ai_walk, 16 },
	{ ai_walk, 16 },
	{ ai_walk, 16 },
	{ ai_walk, 16 },
	{ ai_walk, 16 },
	{ ai_walk, 16 },
	{ ai_walk, 16 },
	{ ai_walk, 16 },
	{ ai_walk, 16 },
	{ ai_walk, 16 },
	{ ai_walk, 16 }
};
const mmove_t MMOVE_T(wizard_move_walk) = { FRAME_fly01, FRAME_fly14, wizard_frames_walk, NULL };

void MONSTERINFO_WALK(wizard_walk)(edict_t *self)
{
    M_SetAnimation(self, &wizard_move_walk);
}

// Run
static const mframe_t wizard_frames_run[] = {
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 },
	{ ai_run, 16 }
};
const mmove_t MMOVE_T(wizard_move_run) = { FRAME_fly01, FRAME_fly14, wizard_frames_run, NULL };

void MONSTERINFO_RUN(wizard_run)(edict_t *self)
{
    M_SetAnimation(self, &wizard_move_run);
}

static void wizard_frame(edict_t *self)
{
    if (self->count >= 5)
        return;

    self->s.frame = (FRAME_magatt05 - self->count);
    self->count++;

    if (self->count >= 5)
        wizard_run(self);
}

// decino: Quake plays this animation backwards, so we'll have to do some hacking
static const mframe_t wizard_frames_finish[] = {
    { ai_charge, 0, wizard_frame },
    { ai_charge, 0, wizard_frame },
    { ai_charge, 0, wizard_frame },
    { ai_charge, 0, wizard_frame },
    { ai_charge, 0, wizard_frame },
    { ai_charge, 0, wizard_frame }
};
const mmove_t MMOVE_T(wizard_move_finish) = { FRAME_magatt01, FRAME_magatt06, wizard_frames_finish, wizard_run };

static void wizard_finish_attack(edict_t *self)
{
    M_SetAnimation(self, &wizard_move_finish);
    self->count = 0;
}

void TOUCH(spit_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
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
        G_BecomeEvent(self, EV_WIZARD_SPIT, tr->plane.dir);
    }
}

static void fire_spit(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed)
{
    edict_t *spit;

    spit = G_SpawnMissile(self, start, dir, speed);
    spit->flags |= FL_DODGE;
    spit->s.effects |= (EF_BLASTER | EF_TRACKER);
    spit->s.modelindex = G_ModelIndex("models/monsters/spitstrogg/tris.md2");
    spit->touch = spit_touch;
    spit->nextthink = level.time + SEC(10);
    spit->think = G_FreeEdict;
    spit->dmg = damage;
    trap_LinkEntity(spit);

    G_CheckMissileImpact(self, spit);
}

static void WizardSpitLeft(edict_t *self)
{
    vec3_t  forward, right;
    vec3_t  start, vec, dir;
    vec3_t  ofs = { 14, -14, 25 };

    if (!self->enemy || !self->enemy->r.inuse)
        return;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, ofs, forward, right);

    vec = self->enemy->s.origin;
    vec.z += self->enemy->viewheight;

    dir = Vec3_Direction(vec, start);
    fire_spit(self, start, dir, 9, 600);

    if (self->style == Strogg)
        monster_fire_blaster(self, start, dir, 1, 1000, MZ2_HOVER_BLASTER_1, EF_HYPERBLASTER);
}

static void WizardSpitRight(edict_t *self)
{
    vec3_t  forward, right;
    vec3_t  start, vec, dir;
    vec3_t  ofs = { 14, 14, 35 };

    if (!self->enemy || !self->enemy->r.inuse)
        return;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, ofs, forward, right);

    vec = self->enemy->s.origin;
    vec.z += self->enemy->viewheight;

    dir = Vec3_Direction(vec, start);
    fire_spit(self, start, dir, 9, 600);

    if (self->style == Strogg)
        monster_fire_blaster(self, start, dir, 1, 1000, MZ2_HOVER_BLASTER_1, EF_HYPERBLASTER);
}

static void wizard_prespit(edict_t *self)
{
    G_StartSound(self, CHAN_WEAPON, SOUND.attack, 1, ATTN_NORM);
}

// Attack
static const mframe_t wizard_frames_attack[] = {
	{ ai_charge, 0, wizard_prespit },
	{ ai_charge, 0, WizardSpitLeft },
	{ ai_charge },
	{ ai_charge },
	{ ai_charge, 0, WizardSpitRight },
	{ ai_charge }
};
const mmove_t MMOVE_T(wizard_move_attack) = { FRAME_magatt01, FRAME_magatt06, wizard_frames_attack, wizard_finish_attack };

static void wizarcuda_checker(edict_t *self)
{
    self->fly_sound_debounce_time = level.time + SEC(2.0f);
}

static void wizarcuda_beamfire(edict_t *self)
{
    vec3_t  start;
    vec3_t  dir;
    vec3_t  forward;
    vec3_t  right;

    if (!self->enemy || !self->enemy->r.inuse)
        return;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_WIZARD_BEAM], forward, right);

    M_PredictAim(self, self->enemy, start, 0, false, 0.1f, &dir, NULL);

    monster_fire_heatbeam(self, start, dir, start, 1, 0, MZ2_WIZARD_BEAM);

    if (self->fly_sound_debounce_time > level.time)
        self->monsterinfo.nextframe = FRAME_magatt05;
    else
        self->monsterinfo.weapon_sound = 0;
}

static void wizarcuda_beamfire_sound(edict_t *self)
{
    self->monsterinfo.weapon_sound = SOUND.beam;
}

static const mframe_t wizarcuda_frames_attack[] = {
    { ai_charge, 0, NULL },
    { ai_charge, 0, NULL },
    { ai_charge, 0, wizarcuda_beamfire_sound },
    { ai_charge, 0, wizarcuda_checker },
    { ai_charge, 0, wizarcuda_beamfire },
    { ai_charge, 0, NULL }
};
const mmove_t MMOVE_T(wizarcuda_move_attack) = { FRAME_magatt01, FRAME_magatt06, wizarcuda_frames_attack, wizard_finish_attack };

void MONSTERINFO_ATTACK(wizard_attack)(edict_t *self)
{
    if (self->style == Wizarcuda)
        M_SetAnimation(self, &wizarcuda_move_attack);
    else
        M_SetAnimation(self, &wizard_move_attack);
}

// Pain
static const mframe_t wizard_frames_pain[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(wizard_move_pain) = { FRAME_pain01, FRAME_pain04, wizard_frames_pain, wizard_run};

void PAIN(wizard_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (level.time < self->pain_debounce_time)
        return;

    self->monsterinfo.weapon_sound = 0;
    G_StartSound(self, CHAN_VOICE, SOUND.pain, 1, ATTN_NORM);

    if (M_ShouldReactToPain(self, mod))
        M_SetAnimation(self, &wizard_move_pain);

    self->pain_debounce_time = level.time + SEC(3);
}

static void wizard_fling(edict_t *self)
{
    self->velocity.x = -200 + 400 * crandom();
    self->velocity.y = -200 + 400 * crandom();
    self->velocity.z = 100 + 100 * crandom();

    self->r.box.maxs.z = -8 * G_EntityScale(self);
    self->movetype = MOVETYPE_TOSS;
    self->r.svflags |= SVF_DEADMONSTER;
}

// Death
static const mframe_t wizard_frames_death[] = {
    { ai_move, 0, wizard_fling },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(wizard_move_death) = { FRAME_death01, FRAME_death08, wizard_frames_death, monster_dead };

static const gib_def_t wizard_gibs_plain[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/objects/gibs/head2/tris.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t wizard_gibs_strogg[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/monsters/wizardstrogg/gibs/g_arm.md2", 2 },
    { "models/monsters/wizardstrogg/gibs/g_leg.md2", 1 },
    { "models/monsters/wizardstrogg/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t wizard_gibs_cuda[] = {
    { "models/monsters/scraggacuda/gibs/g_arm.md2", 2, GIB_ACID },
    { "models/monsters/scraggacuda/gibs/g_leg.md2", 1, GIB_ACID },
    { "models/monsters/scraggacuda/gibs/g_head.md2", 1, GIB_ACID | GIB_HEAD },
    { 0 }
};

void DIE(wizard_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    self->monsterinfo.weapon_sound = 0;

    if (self->health <= self->gib_health) {
        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        switch (self->style) {
        case Wizarcuda:
            ThrowGibs(self, damage, wizard_gibs_cuda);
            break;
        case Strogg:
            ThrowGibs(self, damage, wizard_gibs_strogg);
            break;
        default:
            ThrowGibs(self, damage, wizard_gibs_plain);
            break;
        }
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;

    G_StartSound(self, CHAN_VOICE, SOUND.death, 1, ATTN_NORM);

    self->deadflag = true;
    self->takedamage = true;
    M_SetAnimation(self, &wizard_move_death);
}

void MONSTERINFO_SIGHT(wizard_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_VOICE, SOUND.sight, 1, ATTN_NORM);
}

void MONSTERINFO_SEARCH(wizard_search)(edict_t *self)
{
    if (frandom() > 0.9f)
        G_StartSound(self, CHAN_VOICE, SOUND.idle1, 1, ATTN_NORM);
    else
        G_StartSound(self, CHAN_VOICE, SOUND.idle2, 1, ATTN_NORM);
}

void MONSTERINFO_SETSKIN(wizard_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

void PR_monster_wizard(void)
{
    sound[0].attack = G_SoundIndex("wizard/wattack.wav");
    sound[0].death = G_SoundIndex("wizard/wdeath.wav");
    sound[0].idle1 = G_SoundIndex("wizard/widle1.wav");
    sound[0].idle2 = G_SoundIndex("wizard/widle2.wav");
    sound[0].pain = G_SoundIndex("wizard/wpain.wav");
    sound[0].sight = G_SoundIndex("wizard/wsight.wav");
}

void PR_monster_wizard_strogg(void)
{
    sound[1].attack = G_SoundIndex("wizard/wattack_s.wav");
    sound[1].death = G_SoundIndex("wizard/wdeath_s.wav");
    sound[1].idle1 = G_SoundIndex("wizard/widle1_s.wav");
    sound[1].idle2 = G_SoundIndex("wizard/widle2_s.wav");
    sound[1].pain = G_SoundIndex("wizard/wpain_s.wav");
    sound[1].sight = G_SoundIndex("wizard/wsight_s.wav");
    sound[1].beam = G_SoundIndex("army/bfgbeam.wav");
}

static void SP_monster_wizard_x(edict_t *self)
{
    G_SoundIndex("wizard/hit.wav");

    self->movetype = MOVETYPE_STEP;
    self->r.box = Box3_FromSize(16, -24, 40);
    self->r.solid = SOLID_BBOX;

    self->health *= st.health_multiplier;
    self->gib_health = -40;
    self->mass = 200;

    self->pain = wizard_pain;
    self->die = wizard_die;
    self->monsterinfo.stand = wizard_stand;
    self->monsterinfo.walk = wizard_walk;
    self->monsterinfo.run = wizard_run;
    self->monsterinfo.attack = wizard_attack;
    self->monsterinfo.sight = wizard_sight;
    self->monsterinfo.search = wizard_search;
    self->monsterinfo.setskin = wizard_setskin;

    trap_LinkEntity(self);

    M_SetAnimation(self, &wizard_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    flymonster_start(self);
}

void SP_monster_wizard(edict_t *self)
{
    self->style = Plain;
    self->health = 80;
    self->s.modelindex = G_ModelIndex("models/monsters/wizard/tris.md2");
    SP_monster_wizard_x(self);
}

void SP_monster_wizard_prototype(edict_t *self)
{
    self->style = Proto;
    self->health = 80;
    self->s.modelindex = G_ModelIndex("models/monsters/wizard_prototype/tris.md2");
    SP_monster_wizard_x(self);
}

void SP_monster_wizard_strogg(edict_t *self)
{
    self->style = Strogg;
    self->health = 120;
    self->s.modelindex = G_ModelIndex("models/monsters/wizardstrogg/tris.md2");
    SP_monster_wizard_x(self);
    G_PrecacheGibs(wizard_gibs_strogg);
}

void SP_monster_wizarcuda_strogg(edict_t *self)
{
    self->style = Wizarcuda;
    self->health = 120;
    self->flags |= FL_ACIDIC;
    self->s.modelindex = G_ModelIndex("models/monsters/scraggacuda/tris.md2");
    SP_monster_wizard_x(self);
    G_PrecacheGibs(wizard_gibs_cuda);
}
