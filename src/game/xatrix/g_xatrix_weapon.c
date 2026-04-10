// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"

void fire_blueblaster(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, effects_t effect)
{
    edict_t *bolt;

    bolt = G_SpawnMissile(self, start, dir, speed);
    bolt->flags |= FL_DODGE;
    bolt->s.effects |= effect;
    bolt->s.modelindex = G_ModelIndex("models/objects/laser/tris.md2");
    bolt->s.skinnum = 1;
    bolt->s.sound = G_SoundIndex("misc/lasfly.wav");
    bolt->touch = blaster_touch;
    bolt->nextthink = level.time + SEC(2);
    bolt->think = G_FreeEdict;
    bolt->dmg = damage;
    bolt->classname = "bolt";
    bolt->style = MOD_BLUEBLASTER;
    trap_LinkEntity(bolt);

    G_CheckMissileImpact(self, bolt);
}

/*
fire_ionripper
*/

void THINK(ionripper_sparks)(edict_t *self)
{
    G_BecomeEvent(self, EV_WELDING_SPARKS, 0);
}

void TOUCH(ionripper_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    edict_t *owner = &g_edicts[self->r.ownernum];

    if (other == owner)
        return;

    if (tr->surface_flags & SURF_SKY) {
        G_FreeEdict(self);
        return;
    }

    if (owner->client)
        PlayerNoise(owner, self->s.origin, PNOISE_IMPACT);

    if (!other->takedamage)
        return;

    T_Damage(other, self, owner, self->velocity, self->s.origin, tr->plane.dir, self->dmg, 1, DAMAGE_ENERGY, MOD_RIPPER);
    G_FreeEdict(self);
}

void fire_ionripper(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, effects_t effect)
{
    edict_t *ion;

    ion = G_SpawnMissile(self, start, dir, speed);
    ion->movetype = MOVETYPE_WALLBOUNCE;
    ion->flags |= FL_DODGE;
    ion->s.effects |= effect;
    ion->s.renderfx |= RF_FULLBRIGHT;
    ion->s.modelindex = G_ModelIndex("models/objects/boomrang/tris.md2");
    ion->s.sound = G_SoundIndex("misc/lasfly.wav");
    ion->touch = ionripper_touch;
    ion->nextthink = level.time + SEC(3);
    ion->think = ionripper_sparks;
    ion->dmg = damage;
    ion->dmg_radius = 100;
    trap_LinkEntity(ion);

    G_CheckMissileImpact(self, ion);
}

/*
fire_heat
*/

void THINK(heat_think)(edict_t *self)
{
    edict_t *target = NULL;
    edict_t *acquire = NULL;
    vec3_t   vec;
    float    len;
    float    oldlen = 0;
    float    dot, olddot = 1;

    vec3_t fwd;
    AngleVectors(self->s.angles, &fwd, NULL, NULL);

    // try to stay on current target if possible
    if (self->enemy) {
        acquire = self->enemy;

        if (acquire->health <= 0 || !visible(self, acquire))
            self->enemy = acquire = NULL;
    }

    if (!acquire) {
        // acquire new target
        while ((target = findradius(target, self->s.origin, 1024)) != NULL) {
            if (self->r.ownernum == target->s.number)
                continue;
            if (!target->client)
                continue;
            if (target->health <= 0)
                continue;
            if (!visible(self, target))
                continue;

            vec = Vec3_Sub(self->s.origin, target->s.origin);
            len = Vec3_Normalize(&vec);
            dot = Vec3_Dot(vec, fwd);

            // targets that require us to turn less are preferred
            if (dot >= olddot)
                continue;

            if (!acquire || dot < olddot || len < oldlen) {
                acquire = target;
                oldlen = len;
                olddot = dot;
            }
        }
    }

    if (acquire) {
        vec = Vec3_Direction(acquire->s.origin, self->s.origin);

        float t = self->accel;
        float d = Vec3_Dot(vec, self->movedir);

        if (d < 0.45f && d > -0.45f)
            vec = Vec3_Negate(vec);

        self->movedir = Vec3_SLerp(self->movedir, vec, t);
        self->s.angles = vectoangles(self->movedir);

        if (self->enemy != acquire) {
            G_StartSound(self, CHAN_WEAPON, G_SoundIndex("weapons/railgr1a.wav"), 1, 0.25f);
            self->enemy = acquire;
        }
    } else
        self->enemy = NULL;

    self->velocity = Vec3_Scale(self->movedir, self->speed);
    self->nextthink = level.time + FRAME_TIME;
}

void fire_heat(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int radius_damage, float turn_fraction)
{
    edict_t *heat;

    heat = G_SpawnMissile(self, start, dir, speed);
    heat->movedir = dir;
    heat->flags |= FL_DODGE;
    heat->s.effects |= EF_ROCKET;
    heat->s.modelindex = G_ModelIndex("models/objects/rocket/tris.md2");
    heat->s.sound = G_SoundIndex("weapons/rockfly.wav");
    heat->touch = rocket_touch;
    heat->speed = speed;
    heat->accel = turn_fraction;
    heat->nextthink = level.time + FRAME_TIME;
    heat->think = heat_think;
    heat->dmg = damage;
    heat->radius_dmg = radius_damage;
    heat->dmg_radius = damage_radius;

    if (visible(heat, self->enemy)) {
        heat->enemy = self->enemy;
        G_StartSound(heat, CHAN_WEAPON, G_SoundIndex("weapons/railgr1a.wav"), 1.f, 0.25f);
    }

    trap_LinkEntity(heat);
}

/*
fire_plasma
*/

void TOUCH(plasma_touch)(edict_t *ent, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    edict_t *owner = &g_edicts[ent->r.ownernum];

    if (other == owner)
        return;

    if (tr->surface_flags & SURF_SKY) {
        G_FreeEdict(ent);
        return;
    }

    if (owner->client)
        PlayerNoise(owner, ent->s.origin, PNOISE_IMPACT);

    if (other->takedamage)
        T_Damage(other, ent, owner, ent->velocity, ent->s.origin, tr->plane.dir, ent->dmg, ent->dmg, DAMAGE_ENERGY, MOD_PHALANX);

    T_RadiusDamage(ent, owner, ent->radius_dmg, other, ent->dmg_radius, DAMAGE_ENERGY, MOD_PHALANX);

    ent->s.origin = Vec3_Add(ent->s.origin, tr->plane.normal);
    G_BecomeEvent(ent, EV_EXPLOSION1, 0);
}

void fire_plasma(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int radius_damage)
{
    edict_t *plasma;

    plasma = G_SpawnMissile(self, start, dir, speed);
    plasma->flags |= FL_DODGE;
    plasma->s.effects |= EF_PLASMA | EF_ANIM_ALLFAST;
    plasma->s.modelindex = G_ModelIndex("sprites/s_photon.sp2");
    plasma->s.sound = G_SoundIndex("weapons/rockfly.wav");
    plasma->touch = plasma_touch;
    plasma->nextthink = level.time + SEC(8000.0f / speed);
    plasma->think = G_FreeEdict;
    plasma->dmg = damage;
    plasma->radius_dmg = radius_damage;
    plasma->dmg_radius = damage_radius;

    trap_LinkEntity(plasma);
}

void THINK(Trap_Gib_Think)(edict_t *ent)
{
    edict_t *owner = &g_edicts[ent->r.ownernum];

    if (owner->s.frame != 5) {
        G_FreeEdict(ent);
        return;
    }

    vec3_t up;
    AngleVectors(owner->s.angles, NULL, NULL, &up);

    // rotate us around the center
    float degrees = (150 * FRAME_TIME_SEC) + owner->delay;
    vec3_t diff = Vec3_Sub(owner->s.origin, ent->s.origin);
    vec3_t vec = Vec3_RotateDir(diff, up, degrees);

    ent->s.angles.yaw += degrees;

    vec = Vec3_Sub(owner->s.origin, vec);

    trace_t tr = G_TraceLine(ent->s.origin, vec, ent->s.number, MASK_SOLID);
    ent->s.origin = tr.endpos;

    // pull us towards the trap's center
    diff = Vec3_Normalize(diff);
    ent->s.origin = Vec3_MA(ent->s.origin, 15.0f * FRAME_TIME_SEC, diff);

    ent->watertype = trap_PointContents(ent->s.origin);
    if (ent->watertype & MASK_WATER)
        ent->waterlevel = WATER_FEET;

    ent->nextthink = level.time + FRAME_TIME;
    trap_LinkEntity(ent);
}

void DIE(trap_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    BecomeExplosion1(self);
}

void SP_item_foodcube(edict_t *best);
void SpawnDamage(int type, vec3_t origin, vec3_t normal, int damage);
bool player_start_point(edict_t *ent);

void THINK(Trap_Think)(edict_t *ent)
{
    edict_t *target = NULL;
    edict_t *best = NULL;
    vec3_t   vec;
    float    len;
    float    oldlen = 8000;

    if (ent->timestamp < level.time) {
        BecomeExplosion1(ent);
        // note to self
        // cause explosion damage???
        return;
    }

    ent->nextthink = level.time + HZ(10);

    if (!ent->groundentity)
        return;

    // ok lets do the blood effect
    if (ent->s.frame > 4) {
        if (ent->s.frame == 5) {
            bool spawn = ent->wait == 64;

            ent->wait -= 2;

            if (spawn)
                G_StartSound(ent, CHAN_VOICE, G_SoundIndex("weapons/trapdown.wav"), 1, ATTN_IDLE);

            ent->delay += 2;

            if (ent->wait < 19)
                ent->s.frame++;

            return;
        }
        ent->s.frame++;
        if (ent->s.frame == 8) {
            ent->nextthink = level.time + SEC(1);
            ent->think = G_FreeEdict;
            ent->s.effects &= ~EF_TRAP;

            edict_t *cube = G_Spawn();
            cube->count = ent->mass;
            cube->s.scale = 1.0f + (ent->accel - 100.0f) / 300.0f;
            SP_item_foodcube(cube);
            cube->s.origin = ent->s.origin;
            cube->s.origin.z += 24 * cube->s.scale;
            cube->s.angles.yaw = frandom() * 360;
            cube->velocity.z = 400;
            cube->think(cube);
            cube->nextthink = 0;
            trap_LinkEntity(cube);

            G_StartSound(cube, CHAN_AUTO, G_SoundIndex("misc/fhit3.wav"), 1, ATTN_NORM);

            return;
        }
        return;
    }

    ent->s.effects &= ~EF_TRAP;
    if (ent->s.frame >= 4) {
        ent->s.effects |= EF_TRAP;
        // clear the owner if in deathmatch
        if (deathmatch.integer)
            ent->r.ownernum = ENTITYNUM_NONE;
    }

    if (ent->s.frame < 4) {
        ent->s.frame++;
        return;
    }

    while ((target = findradius(target, ent->s.origin, 256)) != NULL) {
        if (target == ent)
            continue;
        if (!target->classname)
            continue;

        // [Paril-KEX] don't allow traps to be placed near flags or teleporters
        // if it's a monster or player with health > 0
        // or it's a player start point
        // and we can see it
        // blow up
        if (player_start_point(target) && visible(target, ent)) {
            BecomeExplosion1(ent);
            return;
        }

        if (!(target->r.svflags & SVF_MONSTER) && !target->client)
            continue;
        if (target != ent->teammaster && CheckTeamDamage(target, ent->teammaster))
            continue;
        // [Paril-KEX]
        if (!deathmatch.integer && target->client)
            continue;
        if (target->health <= 0)
            continue;
        if (!visible(ent, target))
            continue;
        len = Vec3_Distance(ent->s.origin, target->s.origin);
        if (!best || len < oldlen) {
            oldlen = len;
            best = target;
        }
    }

    // pull the enemy in
    if (!best)
        return;

    if (best->groundentity) {
        best->s.origin.z += 1;
        best->groundentity = NULL;
    }

    vec = Vec3_Sub(ent->s.origin, best->s.origin);
    len = Vec3_Normalize(&vec);

    float max_speed = best->client ? 290 : 150;
    float speed = max(max_speed - len, 64);

    best->velocity = Vec3_MA(best->velocity, speed, vec);

    ent->s.sound = G_SoundIndex("weapons/trapsuck.wav");

    if (len >= 48)
        return;

    if (best->mass >= 400) {
        BecomeExplosion1(ent);
        // note to self
        // cause explosion damage???
        return;
    }

    ent->takedamage = false;
    ent->r.solid = SOLID_NOT;
    ent->die = NULL;

    T_Damage(best, ent, ent->teammaster, vec3_origin, best->s.origin, 0, 100000, 1, DAMAGE_NONE, MOD_TRAP);

    if (best->r.svflags & SVF_MONSTER)
        M_ProcessPain(best);

    ent->enemy = best;
    ent->wait = 64;
    ent->s.old_origin = ent->s.origin;
    ent->timestamp = level.time + SEC(30);
    ent->accel = best->mass;
    if (deathmatch.integer)
        ent->mass = best->mass / 4;
    else
        ent->mass = best->mass / 10;
    // ok spawn the food cube
    ent->s.frame = 5;

    // link up any gibs that this monster may have spawned
    for (int i = game.maxclients; i < level.num_edicts; i++) {
        edict_t *e = &g_edicts[i];

        if (!e->r.inuse)
            continue;
        if (strcmp(e->classname, "gib"))
            continue;
        if (Vec3_Distance(e->s.origin, ent->s.origin) > 128)
            continue;

        e->movetype = MOVETYPE_NONE;
        e->nextthink = level.time + FRAME_TIME;
        e->think = Trap_Gib_Think;
        e->r.ownernum = ent->s.number;
        Trap_Gib_Think(e);
    }
}

// RAFAEL
void fire_trap(edict_t *self, vec3_t start, vec3_t aimdir, int speed)
{
    edict_t *trap;
    vec3_t   dir;
    vec3_t   right, up;

    dir = vectoangles(aimdir);
    AngleVectors(dir, NULL, &right, &up);

    trap = G_Spawn();
    trap->s.origin = start;
    trap->velocity = Vec3_Scale(aimdir, speed);

    float scale = (200 + crandom() * 10.0f) * (level.gravity / 800.0f);
    trap->velocity = Vec3_MA(trap->velocity, scale, up);
    trap->velocity = Vec3_MA(trap->velocity, crandom() * 10.0f, right);

    trap->avelocity.yaw = 300;
    trap->movetype = MOVETYPE_BOUNCE;
    trap->r.solid = SOLID_BBOX;
    trap->takedamage = true;
    trap->r.box = Box3_FromSize(4, 0, 8);
    trap->die = trap_die;
    trap->health = 20;
    trap->s.modelindex = G_ModelIndex("models/weapons/z_trap/tris.md2");
    trap->teammaster = self;
    trap->r.ownernum = self->s.number;
    trap->nextthink = level.time + SEC(1);
    trap->think = Trap_Think;
    trap->classname = "food_cube_trap";
    // RAFAEL 16-APR-98
    trap->s.sound = G_SoundIndex("weapons/traploop.wav");
    // END 16-APR-98
    trap->flags |= (FL_DAMAGEABLE | FL_MECHANICAL);
    trap->clipmask = G_ProjectileClipmask(self) & ~CONTENTS_DEADMONSTER;
    trap->r.svflags |= SVF_TRAP;
    trap->timestamp = level.time + SEC(30);

    trap_LinkEntity(trap);
}
