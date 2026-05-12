// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_turret.c

#include "g_local.h"

#define SPAWNFLAG_TURRET_BREACH_FIRE    65536

void MOVEINFO_BLOCKED(turret_blocked)(edict_t *self, edict_t *other)
{
    edict_t *attacker;

    if (other->takedamage) {
        if (self->teammaster->r.ownernum != ENTITYNUM_NONE)
            attacker = &g_edicts[self->teammaster->r.ownernum];
        else
            attacker = self->teammaster;
        T_Damage(other, self, attacker, vec3_origin, other->s.origin, 0, self->teammaster->dmg, 10, DAMAGE_NONE, MOD_CRUSH);
    }
}

/*QUAKED turret_breach (0 0 0) ?
This portion of the turret can change both pitch and yaw.
The model  should be made with a flat pitch.
It (and the associated base) need to be oriented towards 0.
Use "angle" to set the starting angle.

"speed"     default 50
"dmg"       default 10
"angle"     point this forward
"target"    point this at an info_notnull at the muzzle tip
"minpitch"  min acceptable pitch angle : default -30
"maxpitch"  max acceptable pitch angle : default 30
"minyaw"    min acceptable yaw angle   : default 0
"maxyaw"    max acceptable yaw angle   : default 360
*/

static void turret_breach_fire(edict_t *self)
{
    vec3_t f, r, u;
    vec3_t start;
    int    damage;
    int    speed;

    AngleVectors(self->s.angles, &f, &r, &u);
    start = Vec3_MA(self->s.origin, self->move_origin.x, f);
    start = Vec3_MA(start, self->move_origin.y, r);
    start = Vec3_MA(start, self->move_origin.z, u);

    if (self->count)
        damage = self->count;
    else
        damage = irandom2(100, 150);
    speed = 550 + 50 * skill.integer;
    edict_t *owner = &g_edicts[self->teammaster->r.ownernum];
    edict_t *rocket = fire_rocket(owner->activator ? owner->activator : owner, start, f, damage, speed, 150, damage);
    rocket->s.scale = self->teammaster->dmg_radius;

    G_PositionedSound(start, self, CHAN_WEAPON, G_SoundIndex("weapons/rocklf1a.wav"), 1, ATTN_NORM);
}

void THINK(turret_breach_think)(edict_t *self)
{
    edict_t *ent;
    vec3_t   dest, delta;

    // clamp angles to mins & maxs
    dest = self->move_angles;

    dest.pitch = Q_clipf(AngleMod(dest.pitch), self->pos1.pitch, self->pos2.pitch);

    if ((dest.yaw < self->pos1.yaw) || (dest.yaw > self->pos2.yaw)) {
        float dmin, dmax;

        dmin = AngleMod(self->pos1.yaw - dest.yaw);
        dmax = AngleMod(self->pos2.yaw - dest.yaw);
        if (fabsf(dmin) < fabsf(dmax))
            dest.yaw = self->pos1.yaw;
        else
            dest.yaw = self->pos2.yaw;
    }

    delta = Vec3_AngleMod(Vec3_Sub(dest, self->s.angles));
    delta = Vec3_Scale(delta, 1.0f / FRAME_TIME_SEC);

    delta.pitch = Q_clipf(delta.pitch, -self->speed, self->speed);
    delta.yaw   = Q_clipf(delta.yaw,   -self->speed, self->speed);
    delta.roll  = 0;

    for (ent = self->teammaster; ent; ent = ent->teamchain) {
        if (ent->noise_index) {
            if (delta.pitch || delta.yaw)
                ent->s.sound = G_EncodeSound(CHAN_AUTO, ent->noise_index, 1, ATTN_NORM);
            else
                ent->s.sound = 0;
        }
    }

    self->avelocity = delta;

    self->nextthink = level.time + FRAME_TIME;

    for (ent = self->teammaster; ent; ent = ent->teamchain)
        ent->avelocity.yaw = self->avelocity.yaw;

    // if we have a driver, adjust his velocities
    if (self->r.ownernum != ENTITYNUM_NONE) {
        edict_t *owner = &g_edicts[self->r.ownernum];
        float    angle;
        float    target_z;
        float    diff;
        vec3_t   target;
        vec3_t   dir;

        // angular is easy, just copy ours
        owner->avelocity.pitch = self->avelocity.pitch;
        owner->avelocity.yaw = self->avelocity.yaw;

        // x & y
        angle = DEG2RAD(self->s.angles.yaw + owner->move_origin.y);
        target.x = self->s.origin.x + cosf(angle) * owner->move_origin.x;
        target.y = self->s.origin.y + sinf(angle) * owner->move_origin.x;
        target.z = owner->s.origin.z;

        dir = Vec3_Sub(target, owner->s.origin);
        owner->velocity.x = dir.x * 1.0f / FRAME_TIME_SEC;
        owner->velocity.y = dir.y * 1.0f / FRAME_TIME_SEC;

        // z
        angle = DEG2RAD(self->s.angles.pitch);
        target_z = self->s.origin.z + owner->move_origin.x * tanf(angle) + owner->move_origin.z;

        diff = target_z - owner->s.origin.z;
        owner->velocity.z = diff * 1.0f / FRAME_TIME_SEC;

        if (self->spawnflags & SPAWNFLAG_TURRET_BREACH_FIRE) {
            turret_breach_fire(self);
            self->spawnflags &= ~SPAWNFLAG_TURRET_BREACH_FIRE;
        }
    }
}

void THINK(turret_breach_finish_init)(edict_t *self)
{
    // get and save info for muzzle location
    if (!self->target) {
        G_Printf("%s: needs a target\n", etos(self));
    } else {
        self->target_ent = G_PickTarget(self->target);
        if (self->target_ent) {
            self->move_origin = Vec3_Sub(self->target_ent->s.origin, self->s.origin);
            G_FreeEdict(self->target_ent);
        } else
            G_Printf("%s: could not find target entity \"%s\"\n", etos(self), self->target);
    }

    self->teammaster->dmg = self->dmg;
    self->teammaster->dmg_radius = self->dmg_radius; // scale
    self->think = turret_breach_think;
    self->think(self);
}

void SP_turret_breach(edict_t *self)
{
    self->r.solid = SOLID_BSP;
    self->movetype = MOVETYPE_PUSH;

    if (st.noise)
        self->noise_index = G_SoundIndex(st.noise);

    trap_SetBrushModel(self, self->model);

    if (!self->speed)
        self->speed = 50;
    if (!self->dmg)
        self->dmg = 10;

    if (!st.minpitch)
        st.minpitch = -30;
    if (!st.maxpitch)
        st.maxpitch = 30;
    if (!st.maxyaw)
        st.maxyaw = 360;

    self->pos1.pitch = st.minpitch;
    self->pos1.yaw = st.minyaw;
    self->pos2.pitch = st.maxpitch;
    self->pos2.yaw = st.maxyaw;

    // scale used for rocket scale
    self->dmg_radius = self->s.scale;
    self->s.scale = 0;

    self->ideal_yaw = self->s.angles.yaw;
    self->move_angles.yaw = self->ideal_yaw;

    self->moveinfo.blocked = turret_blocked;

    self->think = turret_breach_finish_init;
    self->nextthink = level.time + FRAME_TIME;
    trap_LinkEntity(self);
}

/*QUAKED turret_base (0 0 0) ?
This portion of the turret changes yaw only.
MUST be teamed with a turret_breach.
*/

void SP_turret_base(edict_t *self)
{
    self->r.solid = SOLID_BSP;
    self->movetype = MOVETYPE_PUSH;

    if (st.noise)
        self->noise_index = G_SoundIndex(st.noise);

    trap_SetBrushModel(self, self->model);
    self->moveinfo.blocked = turret_blocked;
    trap_LinkEntity(self);
}

/*QUAKED turret_driver (1 .5 0) (-16 -16 -24) (16 16 32)
Must NOT be on the team with the rest of the turret parts.
Instead it must target the turret_breach.
*/

void infantry_die(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod);
void infantry_stand(edict_t *self);
void infantry_pain(edict_t *self, edict_t *other, float kick, int damage, mod_t mod);
void infantry_setskin(edict_t *self);

void DIE(turret_driver_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    if (!self->deadflag) {
        edict_t *ent;

        // level the gun
        self->target_ent->move_angles.pitch = 0;

        // remove the driver from the end of them team chain
        for (ent = self->target_ent->teammaster; ent->teamchain != self; ent = ent->teamchain)
            ;
        ent->teamchain = NULL;
        self->teammaster = NULL;
        self->flags &= ~FL_TEAMSLAVE;

        self->target_ent->r.ownernum = ENTITYNUM_NONE;
        self->target_ent->teammaster->r.ownernum = ENTITYNUM_NONE;

        self->target_ent->moveinfo.blocked = NULL;

        // clear pitch
        self->s.angles.pitch = 0;
        self->movetype = MOVETYPE_STEP;

        self->think = monster_think;
        self->classname = "monster_infantry"; // [Paril-KEX] fix revive
    }

    infantry_die(self, inflictor, attacker, damage, point, mod);

    G_FixStuckObject(self, self->s.origin);
    AngleVectors(self->s.angles, &self->velocity, NULL, NULL);
    self->velocity = Vec3_Scale(self->velocity, -50);
    self->velocity.z += 110;
}

void THINK(turret_driver_think)(edict_t *self)
{
    vec3_t target;
    vec3_t dir;

    self->nextthink = level.time + FRAME_TIME;

    if (self->enemy && (!self->enemy->r.inuse || self->enemy->health <= 0))
        self->enemy = NULL;

    if (!self->enemy) {
        if (!FindTarget(self))
            return;
        self->monsterinfo.trail_time = level.time;
        self->monsterinfo.aiflags &= ~AI_LOST_SIGHT;
    } else if (visible(self, self->enemy)) {
        if (self->monsterinfo.aiflags & AI_LOST_SIGHT) {
            self->monsterinfo.trail_time = level.time;
            self->monsterinfo.aiflags &= ~AI_LOST_SIGHT;
        }
    } else {
        self->monsterinfo.aiflags |= AI_LOST_SIGHT;
        return;
    }

    // let the turret know where we want it to aim
    target = self->enemy->s.origin;
    target.z += self->enemy->viewheight;
    dir = Vec3_Sub(target, self->target_ent->s.origin);
    self->target_ent->move_angles = vectoangles(dir);

    // decide if we should shoot
    if (level.time < self->monsterinfo.attack_finished)
        return;

    gtime_t reaction_time = SEC(3 - skill.integer);
    if ((level.time - self->monsterinfo.trail_time) < reaction_time)
        return;

    self->monsterinfo.attack_finished = level.time + reaction_time + SEC(1);
    // FIXME how do we really want to pass this along?
    self->target_ent->spawnflags |= SPAWNFLAG_TURRET_BREACH_FIRE;
}

void THINK(turret_driver_link)(edict_t *self)
{
    vec3_t   vec;
    edict_t *ent;

    self->think = turret_driver_think;
    self->nextthink = level.time + FRAME_TIME;

    self->target_ent = G_PickTarget(self->target);
    if (!self->target_ent) {
        G_FreeEdict(self);
        return;
    }
    self->target_ent->r.ownernum = self->s.number;
    self->target_ent->teammaster->r.ownernum = self->s.number;
    self->s.angles = self->target_ent->s.angles;

    vec = Vec3_Sub(self->target_ent->s.origin, self->s.origin);
    self->move_origin.x = Vec2_Length(Vec2_FromVec3(vec));

    vec = Vec3_Negate(vec);
    vec = vectoangles(vec);
    self->move_origin.y = vec.yaw;

    self->move_origin.z = self->s.origin.z - self->target_ent->s.origin.z;

    // add the driver to the end of them team chain
    for (ent = self->target_ent->teammaster; ent->teamchain; ent = ent->teamchain)
        ;
    ent->teamchain = self;
    self->teammaster = self->target_ent->teammaster;
    self->flags |= FL_TEAMSLAVE;
}

void SP_turret_driver(edict_t *self)
{
    if (!M_AllowSpawn(self)) {
        G_FreeEdict(self);
        return;
    }

    self->movetype = MOVETYPE_PUSH;
    self->r.solid = SOLID_BBOX;
    self->s.modelindex = G_ModelIndex("models/monsters/infantry/tris.md2");
    self->r.box = Box3_FromSize(16, -24, 32);

    self->health = self->max_health = 100;
    self->gib_health = -40;
    self->mass = 200;
    self->viewheight = 24;

    self->pain = infantry_pain;
    self->die = turret_driver_die;
    self->monsterinfo.stand = infantry_stand;

    self->flags |= FL_NO_KNOCKBACK;

    level.total_monsters++;

    self->r.svflags |= SVF_MONSTER;
    self->takedamage = true;
    self->use = monster_use;
    self->clipmask = MASK_MONSTERSOLID;
    self->s.old_origin = self->s.origin;
    self->monsterinfo.aiflags |= AI_STAND_GROUND;
    self->monsterinfo.setskin = infantry_setskin;

    if (st.item) {
        self->item = FindItemByClassname(st.item);
        if (!self->item)
            G_Printf("%s: bad item: %s\n", etos(self), st.item);
    }

    self->think = turret_driver_link;
    self->nextthink = level.time + FRAME_TIME;

    trap_LinkEntity(self);
}

//============
// ROGUE

// invisible turret drivers so we can have unmanned turrets.
// originally designed to shoot at func_trains and such, so they
// fire at the center of the bounding box, rather than the entity's
// origin.

#define SPAWNFLAG_TURRET_BRAIN_IGNORE_SIGHT 1

void THINK(turret_brain_think)(edict_t *self)
{
    vec3_t  target;
    vec3_t  dir;
    trace_t trace;

    self->nextthink = level.time + FRAME_TIME;

    if (self->enemy) {
        if (!self->enemy->r.inuse)
            self->enemy = NULL;
        else if (self->enemy->takedamage && self->enemy->health <= 0)
            self->enemy = NULL;
    }

    if (!self->enemy) {
        if (!FindTarget(self))
            return;
        self->monsterinfo.trail_time = level.time;
        self->monsterinfo.aiflags &= ~AI_LOST_SIGHT;
    }

    target = G_EntityCenter(self->enemy);

    if (!(self->spawnflags & SPAWNFLAG_TURRET_BRAIN_IGNORE_SIGHT)) {
        trace = G_TraceLine(self->target_ent->s.origin, target, self->target_ent->s.number, MASK_SHOT);
        if (trace.fraction == 1 || trace.entnum == self->enemy->s.number) {
            if (self->monsterinfo.aiflags & AI_LOST_SIGHT) {
                self->monsterinfo.trail_time = level.time;
                self->monsterinfo.aiflags &= ~AI_LOST_SIGHT;
            }
        } else {
            self->monsterinfo.aiflags |= AI_LOST_SIGHT;
            return;
        }
    }

    // let the turret know where we want it to aim
    dir = Vec3_Sub(target, self->target_ent->s.origin);
    self->target_ent->move_angles = vectoangles(dir);

    // decide if we should shoot
    if (level.time < self->monsterinfo.attack_finished)
        return;

    gtime_t reaction_time = self->delay ? SEC(self->delay) : SEC(3 - skill.integer);
    if ((level.time - self->monsterinfo.trail_time) < reaction_time)
        return;

    self->monsterinfo.attack_finished = level.time + reaction_time + SEC(1);
    // FIXME how do we really want to pass this along?
    self->target_ent->spawnflags |= SPAWNFLAG_TURRET_BREACH_FIRE;
}

void THINK(turret_brain_link)(edict_t *self)
{
    vec3_t   vec;
    edict_t *ent;

    if (self->killtarget)
        self->enemy = G_PickTarget(self->killtarget);

    self->think = turret_brain_think;
    self->nextthink = level.time + FRAME_TIME;

    self->target_ent = G_PickTarget(self->target);
    if (!self->target_ent) {
        G_FreeEdict(self);
        return;
    }
    self->target_ent->r.ownernum = self->s.number;
    self->target_ent->teammaster->r.ownernum = self->s.number;
    self->s.angles = self->target_ent->s.angles;

    vec = Vec3_Sub(self->target_ent->s.origin, self->s.origin);
    self->move_origin.x = Vec2_Length(Vec2_FromVec3(vec));

    vec = Vec3_Negate(vec);
    vec = vectoangles(vec);
    self->move_origin.y = vec.yaw;

    self->move_origin.z = self->s.origin.z - self->target_ent->s.origin.z;

    // add the driver to the end of them team chain
    for (ent = self->target_ent->teammaster; ent->teamchain; ent = ent->teamchain)
        ent->activator = self->activator; // pass along activator to breach, etc

    ent->teamchain = self;
    self->teammaster = self->target_ent->teammaster;
    self->flags |= FL_TEAMSLAVE;
}

void USE(turret_brain_deactivate)(edict_t *self, edict_t *other, edict_t *activator)
{
    self->think = NULL;
    self->nextthink = 0;
}

void USE(turret_brain_activate)(edict_t *self, edict_t *other, edict_t *activator)
{
    if (!self->enemy)
        self->enemy = activator;

    // wait at least 3 seconds to fire.
    self->monsterinfo.attack_finished = level.time + SEC(self->wait ? self->wait : 3);
    self->use = turret_brain_deactivate;

    // Paril NOTE: rhangar1 has a turret_invisible_brain that breaks the
    // hangar ceiling; once the final rocket explodes the barrier,
    // it attempts to print "Barrier neutralized." to the rocket owner
    // who happens to be this brain rather than the player that activated
    // the turret. this resolves this by passing it along to fire_rocket.
    self->activator = activator;

    self->think = turret_brain_link;
    self->nextthink = level.time + FRAME_TIME;
}

/*QUAKED turret_invisible_brain (1 .5 0) (-16 -16 -16) (16 16 16)
Invisible brain to drive the turret.

Does not search for targets. If targeted, can only be turned on once
and then off once. After that they are completely disabled.

"delay" the delay between firing (default ramps for skill level)
"Target" the turret breach
"Killtarget" the item you want it to attack.
Target the brain if you want it activated later, instead of immediately. It will wait 3 seconds
before firing to acquire the target.
*/
void SP_turret_invisible_brain(edict_t *self)
{
    if (!self->killtarget) {
        G_Printf("%s with no killtarget!\n", etos(self));
        G_FreeEdict(self);
        return;
    }
    if (!self->target) {
        G_Printf("%s with no target!\n", etos(self));
        G_FreeEdict(self);
        return;
    }

    if (self->targetname) {
        self->use = turret_brain_activate;
    } else {
        self->think = turret_brain_link;
        self->nextthink = level.time + FRAME_TIME;
    }

    self->movetype = MOVETYPE_PUSH;
    trap_LinkEntity(self);
}

// ROGUE
//============
