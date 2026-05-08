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
// m_shalrath.c

#include "g_local.h"
#include "m_shalrath.h"

#define SOUND   sound[!!self->style]

static struct {
    int death;
    int search;
    int pain;
    int attack;
    int fire;
    int sight;
} sound[2];

// Stand
static const mframe_t shalrath_frames_stand[] = {
    { ai_stand }
};
const mmove_t MMOVE_T(shalrath_move_stand) = { FRAME_attack01, FRAME_attack01, shalrath_frames_stand, NULL };

void MONSTERINFO_STAND(shalrath_stand)(edict_t *self)
{
    M_SetAnimation(self, &shalrath_move_stand);
}

// Walk
static const mframe_t shalrath_frames_walk[] = {
    { ai_run, 6 },
    { ai_run, 4 },
    { ai_run, 0 },
    { ai_run, 0 },
    { ai_run, 0 },
    { ai_run, 0 },
    { ai_run, 5 },
    { ai_run, 6 },
    { ai_run, 5 },
    { ai_run, 0 },
    { ai_run, 4 },
    { ai_run, 5 }
};
const mmove_t MMOVE_T(shalrath_move_walk) = { FRAME_walk01, FRAME_walk12, shalrath_frames_walk, NULL };

void MONSTERINFO_WALK(shalrath_walk)(edict_t *self)
{
    M_SetAnimation(self, &shalrath_move_walk);
}

// Run
static const mframe_t shalrath_frames_run[] = {
    { ai_run, 6 },
    { ai_run, 4 },
    { ai_run, 0 },
    { ai_run, 0 },
    { ai_run, 0 },
    { ai_run, 0 },
    { ai_run, 5 },
    { ai_run, 6 },
    { ai_run, 5 },
    { ai_run, 0 },
    { ai_run, 4 },
    { ai_run, 5 }
};
const mmove_t MMOVE_T(shalrath_move_run) = { FRAME_walk01, FRAME_walk12, shalrath_frames_run, NULL };

void MONSTERINFO_RUN(shalrath_run)(edict_t *self)
{
    M_SetAnimation(self, &shalrath_move_run);
}

static void shalrath_roar(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.attack, 1, ATTN_NORM);
}

void TOUCH(shalrath_pod_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    edict_t *owner = &g_edicts[self->r.ownernum];

    if (other == owner)
        return;

    if (!strncmp(other->classname, CONST_STR_LEN("monster_zombie"))) // decino: According to shalrath.qc
        T_Damage(other, self, owner, vec3_origin, other->s.origin, 0, 110, 110, DAMAGE_NONE, MOD_UNKNOWN);

    T_RadiusDamage(self, owner, self->dmg, NULL, self->dmg + 40, DAMAGE_NONE, MOD_EXPLOSIVE);

    G_BecomeEvent(self, EV_ROCKET_EXPLOSION, 0);
}

void THINK(shalrath_pod_home)(edict_t *self)
{
    vec3_t  end;
    vec3_t  dir;

    if (!self->enemy || self->enemy->health < 1 || level.time > self->timestamp) {
        G_BecomeEvent(self, EV_ROCKET_EXPLOSION, 0);
        return;
    }

    end = self->enemy->s.origin;
    end.z += self->enemy->viewheight;

    dir = Vec3_Direction(end, self->s.origin);
    self->velocity = Vec3_Scale(dir, (skill.integer >= 3) ? 350 : 250);

    G_AddEvent(self, EV_TUNNEL_SPARKS, MakeLittleLong(0, 15, 255, 0));

    self->nextthink = level.time + HZ(5);
    self->think = shalrath_pod_home;
}

void fire_shalrath_pod(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed)
{
    edict_t *pod;

    pod = G_SpawnMissile(self, start, dir, speed);
    pod->avelocity = Vec3_Fill(300);
    pod->s.modelindex = G_ModelIndex("models/monsters/podstrogg/tris.md2");
    pod->s.effects |= EF_IONRIPPER;
    pod->touch = shalrath_pod_touch;
    pod->nextthink = level.time + HZ(10);
    pod->think = shalrath_pod_home;
    pod->dmg = damage;
    pod->enemy = self->enemy;
    pod->timestamp = level.time + SEC(15);
    trap_LinkEntity(pod);

    G_CheckMissileImpact(self, pod);
}

static void FireShalrathPod(edict_t *self)
{
    vec3_t  forward, right;
    vec3_t  start, dir;
    vec3_t  offset = { 16, 0, 16 };

    if (!self->enemy || !self->enemy->r.inuse)
        return;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = M_ProjectFlashSource(self, offset, forward, right);

    dir = Vec3_Direction(self->enemy->s.origin, start);
    fire_shalrath_pod(self, start, dir, 40, 400);

    G_StartSound(self, CHAN_WEAPON, SOUND.fire, 1, ATTN_NORM);
}

// Attack
static const mframe_t shalrath_frames_attack[] = {
    { ai_charge, 0, shalrath_roar },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, FireShalrathPod },
    { ai_charge },
    { ai_charge }
};
const mmove_t MMOVE_T(shalrath_move_attack) = { FRAME_attack01, FRAME_attack11, shalrath_frames_attack, shalrath_run };

void MONSTERINFO_ATTACK(shalrath_attack)(edict_t *self)
{
    M_SetAnimation(self, &shalrath_move_attack);
}

// Pain
static const mframe_t shalrath_frames_pain[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(shalrath_move_pain) = { FRAME_pain01, FRAME_pain05, shalrath_frames_pain, shalrath_run };

void PAIN(shalrath_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (level.time < self->pain_debounce_time)
        return;
    G_StartSound(self, CHAN_VOICE, SOUND.pain, 1, ATTN_NORM);

    if (M_ShouldReactToPain(self, mod))
        M_SetAnimation(self, &shalrath_move_pain);

    self->pain_debounce_time = level.time + SEC(3);
}

void MONSTERINFO_SETSKIN(shalrath_setskin)(edict_t *self)
{
    if (self->health < (self->max_health / 2))
        self->s.skinnum = 1;
    else
        self->s.skinnum = 0;
}

static void shalrath_dead(edict_t *self)
{
    self->r.box.maxs.z = -8 * G_EntityScale(self);
    monster_dead(self);
}

// Death
static const mframe_t shalrath_frames_death[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(shalrath_move_death) = { FRAME_death01, FRAME_death07, shalrath_frames_death, shalrath_dead };

static const gib_def_t shalrath_gibs[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/objects/gibs/head2/tris.md2", 1, GIB_HEAD },
    { 0 }
};

void DIE(shalrath_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    if (self->health <= self->gib_health) {
        G_StartSound(self, CHAN_VOICE, G_SoundIndex("misc/udeath.wav"), 1, ATTN_NORM);
        ThrowGibs(self, damage, shalrath_gibs);
        self->deadflag = true;
        return;
    }

    if (self->deadflag)
        return;

    G_StartSound(self, CHAN_VOICE, SOUND.death, 1, ATTN_NORM);

    self->deadflag = true;
    self->takedamage = true;
    M_SetAnimation(self, &shalrath_move_death);
}

// Sight
void MONSTERINFO_SIGHT(shalrath_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_VOICE, SOUND.sight, 1, ATTN_NORM);
}

// Search
void MONSTERINFO_SEARCH(shalrath_search)(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.search, 1, ATTN_NORM);
}

void PR_monster_shalrath(void)
{
    sound[0].death = G_SoundIndex("shalrath/death.wav");
    sound[0].search = G_SoundIndex("shalrath/idle.wav");
    sound[0].pain = G_SoundIndex("shalrath/pain.wav");
    sound[0].attack = G_SoundIndex("shalrath/attack.wav");
    sound[0].fire = G_SoundIndex("shalrath/attack2.wav");
    sound[0].sight = G_SoundIndex("shalrath/sight.wav");
}

void PR_monster_shalrath_prototype(void)
{
    sound[1].death = G_SoundIndex("shalrath/death_s.wav");
    sound[1].search = G_SoundIndex("shalrath/idle_s.wav");
    sound[1].pain = G_SoundIndex("shalrath/pain_s.wav");
    sound[1].attack = G_SoundIndex("shalrath/attack_s.wav");
    sound[1].fire = G_SoundIndex("shalrath/attack2_s.wav");
    sound[1].sight = G_SoundIndex("shalrath/sight_s.wav");
}

static void SP_monster_shalrath_x(edict_t *self)
{
    self->r.box = Box3_FromSize(32, -24, 48);
    self->r.solid = SOLID_BBOX;
    self->movetype = MOVETYPE_STEP;

    self->health = 400;
    self->gib_health = -90;
    self->mass = 400;

    self->pain = shalrath_pain;
    self->die = shalrath_die;
    self->monsterinfo.stand = shalrath_stand;
    self->monsterinfo.walk = shalrath_walk;
    self->monsterinfo.run = shalrath_run;
    self->monsterinfo.attack = shalrath_attack;
    self->monsterinfo.sight = shalrath_sight;
    self->monsterinfo.search = shalrath_search;

    trap_LinkEntity(self);

    M_SetAnimation(self, &shalrath_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    walkmonster_start(self);
}

void SP_monster_shalrath(edict_t *self)
{
    self->style = 0;
    self->s.modelindex = G_ModelIndex("models/monsters/shalrath/tris.md2");
    SP_monster_shalrath_x(self);
}

void SP_monster_shalrath_prototype(edict_t *self)
{
    self->style = 1;
    self->s.modelindex = G_ModelIndex("models/monsters/shalrath_prototype/tris.md2");
    SP_monster_shalrath_x(self);
}
