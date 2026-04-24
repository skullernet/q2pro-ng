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
// m_ogre.c

#include "g_local.h"
#include "m_ogre.h"

#define SOUND sound[!!self->style]

enum { Plain, Proto, Strogg };

static struct {
    int death;
    int drag;
    int melee;
    int sight;
    int search;
    int idle;
    int idle2;
    int pain;
} sound[2];

static void ogre_idle(edict_t *self)
{
    if (frandom() < 0.2f)
        G_StartSound(self, CHAN_VOICE, SOUND.idle, 1, ATTN_NORM);
}

static void ogre_idle2(edict_t *self)
{
    if (frandom() < 0.2f)
        G_StartSound(self, CHAN_VOICE, SOUND.idle2, 1, ATTN_NORM);
}

static void ogre_drag(edict_t *self)
{
    if (frandom() < 0.2f)
        G_StartSound(self, CHAN_VOICE, SOUND.drag, 1, ATTN_NORM);
}

// Stand
static const mframe_t ogre_frames_stand[] = {
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
const mmove_t MMOVE_T(ogre_move_stand) = { FRAME_stand01, FRAME_stand09, ogre_frames_stand, NULL };

void MONSTERINFO_STAND(ogre_stand)(edict_t *self)
{
    M_SetAnimation(self, &ogre_move_stand);
}

// Run
static const mframe_t ogre_frames_run[] = {
    { ai_run, 9, ogre_idle2 },
    { ai_run, 12, monster_footstep },
    { ai_run, 8 },
    { ai_run, 22 },
    { ai_run, 16 },
    { ai_run, 4, monster_footstep },
    { ai_run, 13 },
    { ai_run, 24 }
};
const mmove_t MMOVE_T(ogre_move_run) = { FRAME_run01, FRAME_run08, ogre_frames_run, NULL };

void MONSTERINFO_RUN(ogre_run)(edict_t *self)
{
    M_SetAnimation(self, &ogre_move_run);
}

// Run
static const mframe_t ogre_frames_walk[] = {
    { ai_walk, 3 },
    { ai_walk, 2 },
    { ai_walk, 2, ogre_idle },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 5, ogre_drag },
    { ai_walk, 3 },
    { ai_walk, 2 },
    { ai_walk, 3, monster_footstep },
    { ai_walk, 1 },
    { ai_walk, 2 },
    { ai_walk, 3 },
    { ai_walk, 3 },
    { ai_walk, 3 },
    { ai_walk, 3 },
    { ai_walk, 4, monster_footstep }
};
const mmove_t MMOVE_T(ogre_move_walk) = { FRAME_walk01, FRAME_walk16, ogre_frames_walk, NULL };

void MONSTERINFO_WALK(ogre_walk)(edict_t *self)
{
    M_SetAnimation(self, &ogre_move_walk);
}

static void OgreChainsaw(edict_t *self)
{
    vec3_t aim = { MELEE_DISTANCE, 0, 24 };
    int damage = (frandom() + frandom() + frandom()) * 4;
    fire_hit(self, aim, damage, 40);
}

// Smash
static const mframe_t ogre_frames_smash[] = {
    { ai_charge, 0 },
    { ai_charge, 0 },
    { ai_charge, 1 },
    { ai_charge, 4 },
    { ai_charge, 14, OgreChainsaw },
    { ai_charge, 14, OgreChainsaw },
    { ai_charge, 20, OgreChainsaw },
    { ai_charge, 23, OgreChainsaw },
    { ai_charge, 10, OgreChainsaw },
    { ai_charge, 12, OgreChainsaw },
    { ai_charge, 1 },
    { ai_charge, 4 },
    { ai_charge, 12 },
    { ai_charge, 0 }
};
const mmove_t MMOVE_T(ogre_move_smash) = { FRAME_smash01, FRAME_smash14, ogre_frames_smash, ogre_run };

// Swing
static const mframe_t ogre_frames_swing[] = {
    { ai_charge, 11 },
    { ai_charge, 1 },
    { ai_charge, 4 },
    { ai_charge, 19, OgreChainsaw },
    { ai_charge, 13, OgreChainsaw },
    { ai_charge, 10, OgreChainsaw },
    { ai_charge, 10, OgreChainsaw },
    { ai_charge, 10, OgreChainsaw },
    { ai_charge, 10, OgreChainsaw },
    { ai_charge, 10, OgreChainsaw },
    { ai_charge, 3 },
    { ai_charge, 8 },
    { ai_charge, 9 },
    { ai_charge, 0 }
};
const mmove_t MMOVE_T(ogre_move_swing) = { FRAME_swing01, FRAME_swing14, ogre_frames_swing, ogre_run };

// Melee
void MONSTERINFO_MELEE(ogre_melee)(edict_t *self)
{
    if (brandom())
        M_SetAnimation(self, &ogre_move_smash);
    else
        M_SetAnimation(self, &ogre_move_swing);
    G_StartSound(self, CHAN_WEAPON, SOUND.melee, 1, ATTN_NORM);
}

// Grenade
static void OgreGrenade(edict_t *self)
{
    vec3_t forward, right;
    vec3_t start, dir, vec;
    float right_adj, up_adj;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_OGRE_GRENADE], forward, right);

    vec = self->enemy->s.origin;
    vec.z += self->enemy->viewheight;

    dir = Vec3_Direction(vec, start);

    right_adj = crandom_open() * 10.0f;

    if (M_CalculatePitchToFire(self, vec, start, &dir, 600, 2.5f, false, false))
        up_adj = frandom() * 10.0f;
    else
        up_adj = 200.0f + crandom_open() * 10.0f;
    monster_fire_grenade(self, start, dir, 50, 600, MZ2_OGRE_GRENADE, right_adj, up_adj);
}

static void OgreShotgun(edict_t *self)
{
    vec3_t forward, right, up;
    vec3_t start, end, dir, vec, aim;
    float r, u;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, monster_flash_offset[MZ2_OGRE_SHOTGUN], forward, right);

    vec = self->enemy->s.origin;
    vec.z += self->enemy->viewheight;

    aim = Vec3_Direction(vec, start);

    dir = vectoangles(aim);
    AngleVectors(dir, &forward, &right, &up);

    r = crandom() * 1000;
    u = crandom() * 500;

    end = Vec3_MA(start, 8192, forward);
    end = Vec3_MA(end, r, right);
    end = Vec3_MA(end, u, up);

    aim = Vec3_Direction(end, start);
    monster_fire_shotgun(self, start, aim, 2, 1, 1500, 750, 9, MZ2_OGRE_SHOTGUN);
}

static void OgreFire(edict_t *self)
{
    if (!self->enemy || !self->enemy->r.inuse)
        return;

    if (self->style == Strogg)
        OgreShotgun(self);
    else
        OgreGrenade(self);
}

static const mframe_t ogre_frames_attack[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, OgreFire },
    { ai_charge },
    { ai_charge }
};
const mmove_t MMOVE_T(ogre_move_attack) = { FRAME_shoot01, FRAME_shoot06, ogre_frames_attack, ogre_run };

void MONSTERINFO_ATTACK(ogre_attack)(edict_t *self)
{
    M_SetAnimation(self, &ogre_move_attack);
}

static const mframe_t ogre_frames_pain1[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(ogre_move_pain1) = { FRAME_pain01, FRAME_pain05, ogre_frames_pain1, ogre_run };

static const mframe_t ogre_frames_pain2[] = {
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(ogre_move_pain2) = { FRAME_painb01, FRAME_painb03, ogre_frames_pain2, ogre_run };

static const mframe_t ogre_frames_pain3[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(ogre_move_pain3) = { FRAME_painc01, FRAME_painc06, ogre_frames_pain3, ogre_run };

static const mframe_t ogre_frames_pain4[] = {
    { ai_move },
    { ai_move, 10 },
    { ai_move, 9 },
    { ai_move, 4 },
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
const mmove_t MMOVE_T(ogre_move_pain4) = { FRAME_paind01, FRAME_paind16, ogre_frames_pain4, ogre_run };

static const mframe_t ogre_frames_pain5[] = {
    { ai_move },
    { ai_move, 10 },
    { ai_move, 9 },
    { ai_move, 4 },
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
const mmove_t MMOVE_T(ogre_move_pain5) = { FRAME_paine01, FRAME_paine15, ogre_frames_pain5, ogre_run };

void PAIN(ogre_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    float r;

    if (self->pain_debounce_time > level.time)
        return;

    r = frandom();
    if (r < 0.75f)
        self->pain_debounce_time = level.time + SEC(1);
    else
        self->pain_debounce_time = level.time + SEC(2);

    G_StartSound(self, CHAN_VOICE, SOUND.pain, 1, ATTN_NORM);

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    if (r < 0.25f)
        M_SetAnimation(self, &ogre_move_pain1);
    else if (r < 0.5f)
        M_SetAnimation(self, &ogre_move_pain2);
    else if (r < 0.75f)
        M_SetAnimation(self, &ogre_move_pain3);
    else if (r < 0.88f)
        M_SetAnimation(self, &ogre_move_pain4);
    else
        M_SetAnimation(self, &ogre_move_pain5);
}

void MONSTERINFO_SETSKIN(ogre_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum |= 1;
    else
        self->s.skinnum &= ~1;
}

static void ogre_dead(edict_t *self)
{
    self->r.box.maxs.z = -8;
    monster_dead(self);
}

static void ogre_death_sound(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.death, 1, ATTN_NORM);
}

static const mframe_t ogre_frames_death1[] = {
    { ai_move },
    { ai_move, 0, ogre_death_sound },
    { ai_move, 0, ogre_dead },
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
const mmove_t MMOVE_T(ogre_move_death1) = { FRAME_death01, FRAME_death14, ogre_frames_death1, ogre_dead };

static const mframe_t ogre_frames_death2[] = {
    { ai_move },
    { ai_move, 5, ogre_death_sound },
    { ai_move, 0, ogre_dead },
    { ai_move, 1 },
    { ai_move, 3 },
    { ai_move, 7 },
    { ai_move, 25 },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(ogre_move_death2) = { FRAME_bdeath01, FRAME_bdeath10, ogre_frames_death2, ogre_dead };

static const gib_def_t ogre_gibs_plain[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/objects/gibs/head2/tris.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t ogre_gibs_strogg[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/monsters/ogre2strogg/gibs/g_arm.md2", 1 },
    { "models/monsters/ogre2strogg/gibs/g_leg.md2", 2 },
    { "models/monsters/ogre2strogg/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

void DIE(ogre_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    if (self->health <= self->gib_health) {
        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        if (self->style == Strogg)
            ThrowGibs(self, damage, ogre_gibs_strogg);
        else
            ThrowGibs(self, damage, ogre_gibs_plain);
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;

    self->deadflag = true;
    self->takedamage = true;

    if (self->style == Strogg && brandom())
        Drop_Item(self, GetItemByIndex(IT_AMMO_SHELLS))->count = 4;

    if (brandom())
        M_SetAnimation(self, &ogre_move_death1);
    else
        M_SetAnimation(self, &ogre_move_death2);
}

void MONSTERINFO_SIGHT(ogre_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_VOICE, SOUND.sight, 1, ATTN_NORM);
}

void MONSTERINFO_SEARCH(ogre_search)(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.search, 1, ATTN_NORM);
}

void PR_monster_ogre(void)
{
    sound[0].death = G_SoundIndex("ogre/ogdth.wav");
    sound[0].melee = G_SoundIndex("ogre/ogsawatk.wav");
    sound[0].sight = G_SoundIndex("ogre/ogwake.wav");
    sound[0].search = G_SoundIndex("ogre/ogidle2.wav");
    sound[0].idle2 = G_SoundIndex("ogre/ogidle2.wav");
    sound[0].idle = G_SoundIndex("ogre/ogidle.wav");
    sound[0].pain = G_SoundIndex("ogre/ogpain1.wav");
    sound[0].drag = G_SoundIndex("ogre/ogdrag.wav");
}

void PR_monster_ogre2_strogg(void)
{
    sound[1].death = G_SoundIndex("ogre/ogdth_s.wav");
    sound[1].melee = G_SoundIndex("ogre/ogsawatk.wav");
    sound[1].sight = G_SoundIndex("ogre/ogwake_s.wav");
    sound[1].search = G_SoundIndex("ogre/ogidle2_s.wav");
    sound[1].idle2 = G_SoundIndex("ogre/ogidle2_s.wav");
    sound[1].idle = G_SoundIndex("ogre/ogidle_s.wav");
    sound[1].pain = G_SoundIndex("ogre/ogpain1_s.wav");
    sound[1].drag = G_SoundIndex("ogre/ogdrag.wav");
}

static void SP_monster_ogre_x(edict_t *self)
{
    self->r.box = Box3_FromSize(32, -24, 64);
    self->r.solid = SOLID_BBOX;
    self->movetype = MOVETYPE_STEP;

    self->health = 200 * st.health_multiplier;
    self->gib_health = -80;
    self->mass = 200;

    self->pain = ogre_pain;
    self->die = ogre_die;

    self->monsterinfo.stand = ogre_stand;
    self->monsterinfo.walk = ogre_walk;
    self->monsterinfo.run = ogre_run;
    self->monsterinfo.attack = ogre_attack;
    self->monsterinfo.melee = ogre_melee;
    self->monsterinfo.sight = ogre_sight;
    self->monsterinfo.search = ogre_search;
    self->monsterinfo.setskin = ogre_setskin;

    trap_LinkEntity(self);

    M_SetAnimation(self, &ogre_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;
    self->monsterinfo.aiflags |= AI_STINKY;

    walkmonster_start(self);
}

void SP_monster_ogre(edict_t *self)
{
    self->style = Plain;
    self->s.modelindex = G_ModelIndex("models/monsters/ogre/tris.md2");
    SP_monster_ogre_x(self);
}

void SP_monster_ogre_prototype(edict_t *self)
{
    self->style = Proto;
    if (brandom()) {
        self->s.skinnum = 0;
        self->s.modelindex = G_ModelIndex("models/monsters/ogre_prototype/tris.md2");
    } else {
        self->s.skinnum = 2;
        self->s.modelindex = G_ModelIndex("models/monsters/ogre_prototype/tris2.md2");
    }
    SP_monster_ogre_x(self);
}

void SP_monster_ogre2_strogg(edict_t *self)
{
    self->style = Strogg;
    self->s.modelindex = G_ModelIndex("models/monsters/ogre2strogg/tris.md2");
    SP_monster_ogre_x(self);
    G_PrecacheGibs(ogre_gibs_strogg);
}
