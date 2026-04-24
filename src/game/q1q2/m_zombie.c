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
// m_zombie.c

#include "g_local.h"
#include "m_zombie.h"

#define SOUND    sound[!!self->style]

enum { Plain, Proto, Strogg };

static struct {
    int sight;
    int search;
    int fling;
    int pain1;
    int pain2;
    int fall;
    int miss;
    int hit;
    int gib;
} sound[2];

// Stand
static const mframe_t zombie_frames_stand[] = {
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
const mmove_t MMOVE_T(zombie_move_stand) = { FRAME_stand01, FRAME_stand15, zombie_frames_stand, NULL };

void MONSTERINFO_STAND(zombie_stand)(edict_t *self)
{
    M_SetAnimation(self, &zombie_move_stand);
}

static void zombie_reset_state(edict_t *self)
{
    monster_footstep(self);
    self->count = 0;
}

// Run
static const mframe_t zombie_frames_run[] = {
    { ai_run, 1, zombie_reset_state },
    { ai_run, 1 },
    { ai_run, 0 },
    { ai_run, 1 },
    { ai_run, 2 },
    { ai_run, 3 },
    { ai_run, 4 },
    { ai_run, 4 },
    { ai_run, 2 },
    { ai_run, 0, monster_footstep },
    { ai_run, 0 },
    { ai_run, 0 },
    { ai_run, 2 },
    { ai_run, 4 },
    { ai_run, 6 },
    { ai_run, 7 },
    { ai_run, 3 },
    { ai_run, 8 }
};
const mmove_t MMOVE_T(zombie_move_run) = { FRAME_run01, FRAME_run18, zombie_frames_run, NULL };

void MONSTERINFO_RUN(zombie_run)(edict_t *self)
{
    M_SetAnimation(self, &zombie_move_run);
}

// walk
static const mframe_t zombie_frames_walk[] = {
    { ai_walk, 0 },
    { ai_walk, 2 },
    { ai_walk, 3 },
    { ai_walk, 2 },
    { ai_walk, 1 },
    { ai_walk, 0 },
    { ai_walk, 0 },
    { ai_walk, 0 },
    { ai_walk, 0 },
    { ai_walk, 0 },
    { ai_walk, 2 },
    { ai_walk, 2 },
    { ai_walk, 1 },
    { ai_walk, 0 },
    { ai_walk, 0 },
    { ai_walk, 0, monster_footstep },
    { ai_walk, 0 },
    { ai_walk, 0 },
    { ai_walk, 0 }
};
const mmove_t MMOVE_T(zombie_move_walk) = { FRAME_walk01, FRAME_walk19, zombie_frames_walk, NULL };

void MONSTERINFO_WALK(zombie_walk)(edict_t *self)
{
    M_SetAnimation(self, &zombie_move_walk);
}

// Sight
void MONSTERINFO_SIGHT(zombie_sight)(edict_t *self, edict_t *other)
{
    G_StartSound(self, CHAN_VOICE, SOUND.sight, 1, ATTN_NORM);
}

void TOUCH(zombie_gib_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    edict_t *owner = &g_edicts[self->r.ownernum];

    if (other == owner)
        return;

    if (tr->surface_flags & SURF_SKY) {
        G_FreeEdict(self);
        return;
    }

    if (other->takedamage) {
        T_Damage(other, self, owner, self->velocity, self->s.origin, tr->plane.dir, self->dmg, self->dmg, DAMAGE_NONE, MOD_UNKNOWN);
        G_BecomeEvent(self, EV_SOUND, G_EncodeSound(CHAN_WEAPON, SOUND.hit, 1, ATTN_NORM));
        return;
    }

    G_StartSound(self, CHAN_WEAPON, SOUND.miss, 1, ATTN_NORM);

    self->avelocity = vec3_origin;
    self->velocity = vec3_origin;
    self->touch = NULL;
    self->think = G_FreeEdict;
    self->nextthink = level.time + random_time_sec(10, 20);
}

static void fire_zombie_gib(edict_t *self, vec3_t start, vec3_t aimdir, int damage, int speed, float right_adjust, float up_adjust)
{
    edict_t *gib;
    vec3_t   dir;
    vec3_t   right, up;

    dir = vectoangles(aimdir);
    AngleVectors(dir, NULL, &right, &up);

    gib = G_Spawn();
    gib->s.origin = start;
    gib->velocity = Vec3_Scale(aimdir, speed);
    gib->velocity = Vec3_MA(gib->velocity, up_adjust * (level.gravity / 800.0f), up);
    gib->velocity = Vec3_MA(gib->velocity, right_adjust, right);
    gib->avelocity = Vec3_Fill(300);
    gib->movetype = MOVETYPE_BOUNCE;
    gib->clipmask = MASK_PROJECTILE;
    gib->r.solid = SOLID_BBOX;
    gib->r.svflags |= SVF_PROJECTILE;
    gib->s.effects |= EF_GIB;
    gib->s.renderfx |= RF_NOSHADOW;
    gib->s.modelindex = G_ModelIndex("models/objects/gibs/sm_meat/tris.md2");
    gib->r.ownernum = self->s.number;
    gib->touch = zombie_gib_touch;
    gib->dmg = damage;
    gib->style = self->style;
    trap_LinkEntity(gib);
}

static void FireZombieGib(edict_t *self)
{
    vec3_t forward, right;
    vec3_t start;
    vec3_t dir;
    vec3_t vec;
    vec3_t offset = { 16, 0, 8 };
    float right_adj, up_adj;

    if (!self->enemy || !self->enemy->r.inuse)
        return;

    AngleVectors(self->s.angles, &forward, &right, NULL);
    start = G_ProjectSource(self->s.origin, offset, forward, right);

    vec = self->enemy->s.origin;
    vec.z += self->enemy->viewheight;

    dir = Vec3_Direction(vec, start);

    right_adj = crandom_open() * 10.0f;

    if (self->style == Strogg) {
        if (M_CalculatePitchToFire(self, vec, start, &dir, 600, 2.5f, false, false))
            up_adj = frandom() * 10.0f;
        else
            up_adj = 200.0f + crandom_open() * 10.0f;
        fire_grenade(self, start, dir, 50, 600, SEC(2.5f), 90, right_adj, up_adj);
    } else {
        up_adj = 100.0f + crandom_open() * 10.0f;
        fire_zombie_gib(self, start, dir, 10, 1200, right_adj, up_adj);
    }

    G_StartSound(self, CHAN_WEAPON, SOUND.fling, 1, ATTN_NORM);
}

// Attack (1)
static const mframe_t zombie_frames_attack1[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, FireZombieGib }
};
const mmove_t MMOVE_T(zombie_move_attack1) = { FRAME_atta01, FRAME_atta13, zombie_frames_attack1, zombie_run };

// Attack (2)
static const mframe_t zombie_frames_attack2[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, FireZombieGib }
};
const mmove_t MMOVE_T(zombie_move_attack2) = { FRAME_attb01, FRAME_attb14, zombie_frames_attack2, zombie_run };

// Attack (3)
static const mframe_t zombie_frames_attack3[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, 0, FireZombieGib }
};
const mmove_t MMOVE_T(zombie_move_attack3) = { FRAME_attc01, FRAME_attc12, zombie_frames_attack3, zombie_run };

// Attack
void MONSTERINFO_ATTACK(zombie_attack)(edict_t *self)
{
    int r = irandom1(3);
    if (r == 0)
        M_SetAnimation(self, &zombie_move_attack1);
    else if (r == 1)
        M_SetAnimation(self, &zombie_move_attack2);
    else
        M_SetAnimation(self, &zombie_move_attack3);
}

static const mframe_t zombie_frames_get_up[] = {
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
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(zombie_move_get_up) = { FRAME_paine12, FRAME_paine30, zombie_frames_get_up, zombie_run };

static void zombie_pain1(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.pain1, 1, ATTN_NORM);
}

static void zombie_pain2(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.pain2, 1, ATTN_NORM);
}

static void zombie_hit_floor(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, SOUND.fall, 1, ATTN_NORM);
}

void zombie_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod);

static void zombie_down(edict_t *self)
{
    if (self->takedamage) {
        self->takedamage = false;
        self->health = 60;
        self->r.box.maxs.z = 0;
        self->timestamp = level.time + random_time_sec(5, 10);
        self->monsterinfo.aiflags |= AI_HOLD_FRAME;
        trap_LinkEntity(self);
        return;
    }

    if (self->timestamp > level.time)
        return;

    self->takedamage = true;
    self->health = 60;
    self->r.box.maxs.z = 40;
    self->flags |= FL_PARTIALGROUND;
    trap_LinkEntity(self);

    zombie_sight(self, self->enemy);

    if (!M_walkmove(self, 0, 0)) {
        if (self->sounds >= 5) {
            zombie_die(self, world, self, 1, vec3_origin, MOD_UNKNOWN);
            return;
        }
        self->sounds++;
        zombie_down(self);
        return;
    }

    self->sounds = 0;
    self->monsterinfo.aiflags &= ~AI_HOLD_FRAME;
    M_SetAnimation(self, &zombie_move_get_up);
}

// Pain (1)
static const mframe_t zombie_frames_pain1[] = {
    { ai_move, 0, zombie_pain1 },
    { ai_move, 3 },
    { ai_move, 1 },
    { ai_move, 1 },
    { ai_move, 3 },
    { ai_move, 1 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 }
};
const mmove_t MMOVE_T(zombie_move_pain1) = { FRAME_paina01, FRAME_paina12, zombie_frames_pain1, zombie_run };

// Pain (2)
static const mframe_t zombie_frames_pain2[] = {
    { ai_move, 0, zombie_pain2 },
    { ai_move, 2 },
    { ai_move, 8 },
    { ai_move, 6 },
    { ai_move, 2 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0, zombie_hit_floor },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 1 },
    { ai_move, 0 },
    { ai_move, 0 }
};
const mmove_t MMOVE_T(zombie_move_pain2) = { FRAME_painb01, FRAME_painb28, zombie_frames_pain2, zombie_run };

// Pain (3)
static const mframe_t zombie_frames_pain3[] = {
    { ai_move, 0, zombie_pain2 },
    { ai_move, 0 },
    { ai_move, 3 },
    { ai_move, 1 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 1 },
    { ai_move, 1 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 }
};
const mmove_t MMOVE_T(zombie_move_pain3) = { FRAME_painc01, FRAME_painc18, zombie_frames_pain3, zombie_run };

// Pain (4)
static const mframe_t zombie_frames_pain4[] = {
    { ai_move, 0, zombie_pain1 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 1 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 },
    { ai_move, 0 }
};
const mmove_t MMOVE_T(zombie_move_pain4) = { FRAME_paind01, FRAME_paind13, zombie_frames_pain4, zombie_run };

// Pain (5)
static const mframe_t zombie_frames_fall_start[] = {
    { ai_move, 0, zombie_pain1 },
    { ai_move, -8 },
    { ai_move, -5 },
    { ai_move, -3 },
    { ai_move, -1 },
    { ai_move, -2 },
    { ai_move, -1 },
    { ai_move, -1 },
    { ai_move, -2 },
    { ai_move, 0, zombie_hit_floor },
    { ai_move, 0, zombie_down },
    { ai_move }
};
const mmove_t MMOVE_T(zombie_move_fall_start) = { FRAME_paine01, FRAME_paine12, zombie_frames_fall_start, NULL };

// Pain
void PAIN(zombie_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    self->health = 60;

    if (damage < 9)
        return;

    if (self->count == 2)
        return;

    if (damage >= 25) {
        self->count = 2;
        M_SetAnimation(self, &zombie_move_fall_start);
        return;
    }

    if (self->pain_debounce_time > level.time) {
        self->count = 2;
        M_SetAnimation(self, &zombie_move_fall_start);
        return;
    }

    if (self->count) {
        self->pain_debounce_time = level.time + SEC(3);
        return;
    }

    self->count = 1;

    if (!M_ShouldReactToPain(self, mod))
        return; // no pain anims in nightmare

    int r = irandom1(4);
    if (r == 0)
        M_SetAnimation(self, &zombie_move_pain1);
    else if (r == 1)
        M_SetAnimation(self, &zombie_move_pain2);
    else if (r == 2)
        M_SetAnimation(self, &zombie_move_pain3);
    else
        M_SetAnimation(self, &zombie_move_pain4);
}

static const gib_def_t zombie_gibs[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/objects/gibs/head2/tris.md2", 1, GIB_HEAD },
    { 0 }
};

static const gib_def_t zombie_gibs_strogg[] = {
    { "models/objects/gibs/bone/tris.md2", 1 },
    { "models/objects/gibs/sm_meat/tris.md2", 1 },
    { "models/monsters/zombiestrogg/gibs/g_arm.md2", 2 },
    { "models/monsters/zombiestrogg/gibs/g_leg.md2", 2 },
    { "models/monsters/zombiestrogg/gibs/g_head.md2", 1, GIB_HEAD },
    { 0 }
};

// Death
void DIE(zombie_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    if (self->deadflag)
        return;

    G_StartSound(self, CHAN_VOICE, SOUND.gib, 1, ATTN_NORM);

    if (self->style == Strogg) {
        G_AddEvent(self, EV_ROCKET_EXPLOSION, 0);
        T_RadiusClassDamage(self, attacker, 500, self->classname, 100, MOD_EXPLOSIVE);

        if (brandom())
            Drop_Item(self, GetItemByIndex(IT_AMMO_GRENADES))->count = 2;

        ThrowGibs(self, damage, zombie_gibs_strogg);
    } else {
        ThrowGibs(self, damage, zombie_gibs);
    }

    self->deadflag = true;
}

// Search
void MONSTERINFO_SEARCH(zombie_search)(edict_t *self)
{
    if (frandom() < 0.2f)
        G_StartSound(self, CHAN_VOICE, SOUND.sight, 1, ATTN_NORM);
    else
        G_StartSound(self, CHAN_VOICE, SOUND.search, 1, ATTN_NORM);
}

void PR_monster_zombie(void)
{
    sound[0].sight = G_SoundIndex("zombie/z_idle.wav");
    sound[0].search = G_SoundIndex("zombie/idle_w2.wav");
    sound[0].fling = G_SoundIndex("zombie/z_shot1.wav");
    sound[0].pain1 = G_SoundIndex("zombie/z_pain.wav");
    sound[0].pain2 = G_SoundIndex("zombie/z_pain1.wav");
    sound[0].fall = G_SoundIndex("zombie/z_fall.wav");
    sound[0].miss = G_SoundIndex("zombie/z_miss.wav");
    sound[0].hit = G_SoundIndex("zombie/z_hit.wav");
    sound[0].gib = G_SoundIndex("zombie/z_gib.wav");
}

void PR_monster_zombie_strogg(void)
{
    sound[1].sight = G_SoundIndex("zombie/z_idle_s.wav");
    sound[1].search = G_SoundIndex("zombie/idle_w2_s.wav");
    sound[1].fling = G_SoundIndex("zombie/z_shot1.wav");
    sound[1].pain1 = G_SoundIndex("zombie/z_pain_s.wav");
    sound[1].pain2 = G_SoundIndex("zombie/z_pain1_s.wav");
    sound[1].fall = G_SoundIndex("zombie/z_fall.wav");
    sound[1].miss = G_SoundIndex("zombie/z_miss.wav");
    sound[1].hit = G_SoundIndex("zombie/z_hit.wav");
    sound[1].gib = G_SoundIndex("zombie/z_gib_s.wav");
}

static void SP_monster_zombie_x(edict_t *self)
{
    self->r.box = Box3_FromSize(16, -24, 40);
    self->r.solid = SOLID_BBOX;
    self->movetype = MOVETYPE_STEP;

    self->health = 60 * st.health_multiplier;
    self->gib_health = -5;
    self->mass = 60;
    self->flags |= FL_DEEPONE;

    self->pain = zombie_pain;
    self->die = zombie_die;

    self->monsterinfo.stand = zombie_stand;
    self->monsterinfo.walk = zombie_walk;
    self->monsterinfo.run = zombie_run;
    self->monsterinfo.attack = zombie_attack;
    self->monsterinfo.sight = zombie_sight;
    self->monsterinfo.search = zombie_search;

    trap_LinkEntity(self);

    M_SetAnimation(self, &zombie_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;

    walkmonster_start(self);
}

void SP_monster_zombie(edict_t *self)
{
    self->style = Plain;
    self->s.modelindex = G_ModelIndex("models/monsters/zombie/tris.md2");
    SP_monster_zombie_x(self);
}

void SP_monster_zombie_prototype(edict_t *self)
{
    self->style = Proto;
    self->s.modelindex = G_ModelIndex("models/monsters/zombie_prototype/tris.md2");
    SP_monster_zombie_x(self);
}

void SP_monster_zombie_strogg(edict_t *self)
{
    self->style = Strogg;
    self->s.modelindex = G_ModelIndex("models/monsters/zombiestrogg/tris.md2");
    SP_monster_zombie_x(self);
    G_PrecacheGibs(zombie_gibs_strogg);
}
