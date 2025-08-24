// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "g_local.h"

/*
=================
fire_hit

Used for all impact (hit/punch/slash) attacks
=================
*/
bool fire_hit(edict_t *self, vec3_t aim, int damage, int kick)
{
    trace_t tr;
    vec3_t  forward, right, up;
    vec3_t  v;
    vec3_t  point;
    float   range;
    vec3_t  dir;
    edict_t *hit;

    if (!self->enemy)
        return false;

    // see if enemy is in range
    range = distance_between_boxes(self->enemy->r.absmin, self->enemy->r.absmax,
                                   self->r.absmin, self->r.absmax);
    if (range > aim[0])
        return false;

    if (!(aim[1] > self->r.mins[0] && aim[1] < self->r.maxs[0])) {
        // this is a side hit so adjust the "right" value out to the edge of their bbox
        if (aim[1] < 0)
            aim[1] = self->enemy->r.mins[0];
        else
            aim[1] = self->enemy->r.maxs[0];
    }

    closest_point_to_box(self->s.origin, self->enemy->r.absmin, self->enemy->r.absmax, point);

    // check that we can hit the point on the bbox
    trap_Trace(&tr, self->s.origin, NULL, NULL, point, self->s.number, MASK_PROJECTILE);
    hit = &g_edicts[tr.entnum];

    if (tr.fraction < 1) {
        if (!hit->takedamage)
            return false;
        // if it will hit any client/monster then hit the one we wanted to hit
        if ((hit->r.svflags & SVF_MONSTER) || (hit->client))
            hit = self->enemy;
    }

    // check that we can hit the player from the point
    trap_Trace(&tr, point, NULL, NULL, self->enemy->s.origin, self->s.number, MASK_PROJECTILE);
    hit = &g_edicts[tr.entnum];

    if (tr.fraction < 1) {
        if (!hit->takedamage)
            return false;
        // if it will hit any client/monster then hit the one we wanted to hit
        if ((hit->r.svflags & SVF_MONSTER) || (hit->client))
            hit = self->enemy;
    }

    AngleVectors(self->s.angles, forward, right, up);
    VectorMA(self->s.origin, range, forward, point);
    VectorMA(point, aim[1], right, point);
    VectorMA(point, aim[2], up, point);
    VectorSubtract(point, self->enemy->s.origin, dir);

    // do the damage
    T_Damage(hit, self, self, dir, point, 0, damage, kick / 2, DAMAGE_NO_KNOCKBACK, (mod_t) { MOD_HIT });

    if (!(hit->r.svflags & SVF_MONSTER) && (!hit->client))
        return false;

    // do our special form of knockback here
    VectorAvg(self->enemy->r.absmin, self->enemy->r.absmax, v);
    VectorSubtract(v, point, v);
    VectorNormalize(v);
    VectorMA(self->enemy->velocity, kick, v, self->enemy->velocity);
    if (self->enemy->velocity[2] > 0)
        self->enemy->groundentity = NULL;
    return true;
}

static trace_t fire_lead_pierce(edict_t *self, const vec3_t start, const vec3_t end_, const vec3_t aimdir,
                                int damage, int kick, entity_event_t te_impact, int hspread, int vspread,
                                mod_t mod, contents_t *mask, bool *water, vec3_t water_start)
{
    trace_t tr;
    pierce_t pierce;
    pierce_begin(&pierce);

    vec3_t end, pos;
    VectorCopy(end_, end);
    while (1) {
        trap_Trace(&tr, start, NULL, NULL, end, self->s.number, *mask);

        // didn't hit anything, so we're done
        if (tr.fraction == 1.0f)
            break;

        // see if we hit water
        if (tr.contents & MASK_WATER) {
            *water = true;
            VectorCopy(tr.endpos, water_start);

            // CHECK: is this compare ever true?
            if (te_impact != EV_NONE && !VectorCompare(start, tr.endpos)) {
                entity_event_t color;

                if (tr.contents & CONTENTS_WATER)
                    color = EV_SPLASH_BLUE_WATER;
                else if (tr.contents & CONTENTS_SLIME)
                    color = EV_SPLASH_SLIME;
                else if (tr.contents & CONTENTS_LAVA)
                    color = EV_SPLASH_LAVA;
                else
                    color = EV_SPLASH_UNKNOWN;

                if (color != EV_SPLASH_UNKNOWN) {
                    G_SnapVectorTowards(tr.endpos, start, pos);
                    G_TempEntity(pos, color, MakeLittleShort(tr.plane.dir, 8));
                }

                // change bullet's course when it enters water
                vec3_t dir, forward, right, up;
                VectorSubtract(end, start, dir);
                vectoangles(dir, dir);
                AngleVectors(dir, forward, right, up);
                float r = crandom() * hspread * 2;
                float u = crandom() * vspread * 2;
                VectorMA(water_start, 8192, forward, end);
                VectorMA(end, r, right, end);
                VectorMA(end, u, up, end);
            }

            // re-trace ignoring water this time
            *mask &= ~MASK_WATER;
            continue;
        }

        edict_t *hit = &g_edicts[tr.entnum];

        // did we hit an hurtable entity?
        if (hit->takedamage) {
            T_Damage(hit, self, self, aimdir, tr.endpos, tr.plane.dir, damage, kick, mod.id == MOD_TESLA ? DAMAGE_ENERGY : DAMAGE_BULLET, mod);

            // only deadmonster is pierceable, or actual dead monsters
            // that haven't been made non-solid yet
            if ((hit->r.svflags & SVF_DEADMONSTER) || (hit->health <= 0 && (hit->r.svflags & SVF_MONSTER))) {
                if (pierce_mark(&pierce, hit))
                    continue;
            }
        } else {
            // send gun puff / flash
            // don't mark the sky
            if (te_impact != EV_NONE && !(tr.surface_flags & SURF_SKY)) {
                G_SnapVectorTowards(tr.endpos, start, pos);
                G_TempEntity(pos, te_impact, tr.plane.dir);

                if (self->client)
                    PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
            }
        }

        // hit a solid, so we're stopping here
        break;
    }

    pierce_end(&pierce);
    return tr;
}

/*
=================
fire_lead

This is an internal support routine used for bullet/pellet based weapons.
=================
*/
static void fire_lead(edict_t *self, const vec3_t start, const vec3_t aimdir, int damage, int kick, entity_event_t te_impact, int hspread, int vspread, mod_t mod)
{
    contents_t   mask = G_ProjectileClipmask(self) | MASK_WATER;
    bool         water = false;
    vec3_t       water_start = { 0 };

    // special case: we started in water.
    if (trap_PointContents(start) & MASK_WATER) {
        water = true;
        VectorCopy(start, water_start);
        mask &= ~MASK_WATER;
    }

    // check initial firing position
    trace_t tr = fire_lead_pierce(self, self->s.origin, start, aimdir, damage, kick, te_impact, hspread, vspread, mod, &mask, &water, water_start);

    // we're clear, so do the second pierce
    if (tr.fraction == 1.0f) {
        vec3_t end, dir, forward, right, up;
        vectoangles(aimdir, dir);
        AngleVectors(dir, forward, right, up);

        float r = crandom() * hspread;
        float u = crandom() * vspread;
        VectorMA(start, 8192, forward, end);
        VectorMA(end, r, right, end);
        VectorMA(end, u, up, end);

        tr = fire_lead_pierce(self, start, end, aimdir, damage, kick, te_impact, hspread, vspread, mod, &mask, &water, water_start);
    }

    // if went through water, determine where the end is and make a bubble trail
    if (water && te_impact != EV_NONE) {
        vec3_t pos, dir;

        VectorSubtract(tr.endpos, water_start, dir);
        VectorNormalize(dir);
        VectorMA(tr.endpos, -2, dir, pos);
        if (trap_PointContents(pos) & MASK_WATER)
            VectorCopy(pos, tr.endpos);
        else
            trap_Trace(&tr, pos, NULL, NULL, water_start, tr.entnum, MASK_WATER);

        G_SpawnTrail(water_start, tr.endpos, EV_BUBBLETRAIL);
    }
}

/*
=================
fire_bullet

Fires a single round.  Used for machinegun and chaingun.  Would be fine for
pistols, rifles, etc....
=================
*/
void fire_bullet(edict_t *self, const vec3_t start, const vec3_t aimdir, int damage, int kick, int hspread, int vspread, mod_t mod)
{
    fire_lead(self, start, aimdir, damage, kick, mod.id == MOD_TESLA ? EV_NONE : EV_GUNSHOT, hspread, vspread, mod);
}

/*
=================
fire_shotgun

Shoots shotgun pellets.  Used by shotgun and super shotgun.
=================
*/
void fire_shotgun(edict_t *self, const vec3_t start, const vec3_t aimdir, int damage, int kick, int hspread, int vspread, int count, mod_t mod)
{
    while (count--)
        fire_lead(self, start, aimdir, damage, kick, EV_SHOTGUN, hspread, vspread, mod);
}

/*
=================
fire_blaster

Fires a single blaster bolt.  Used by the blaster and hyper blaster.
=================
*/
void TOUCH(blaster_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
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

    if (other->takedamage) {
        T_Damage(other, self, owner, self->velocity, self->s.origin, tr->plane.dir, self->dmg, 1, DAMAGE_ENERGY, (mod_t) { self->style });
        G_FreeEdict(self);
    } else {
        entity_event_t event = (self->style != MOD_BLUEBLASTER) ? EV_BLASTER : EV_BLUEHYPERBLASTER;
        G_BecomeEvent(self, event, tr->plane.dir);
    }
}

edict_t *G_SpawnMissile(edict_t *self, const vec3_t start, const vec3_t dir, int speed)
{
    edict_t *bolt = G_Spawn();
    bolt->r.svflags = SVF_PROJECTILE;
    bolt->r.solid = SOLID_BBOX;
    bolt->r.ownernum = self->s.number;
    VectorCopy(start, bolt->s.origin);
    VectorCopy(start, bolt->s.old_origin);
    vectoangles(dir, bolt->s.angles);
    VectorScale(dir, speed, bolt->velocity);
    bolt->movetype = MOVETYPE_FLYMISSILE;
    bolt->clipmask = G_ProjectileClipmask(self);
    return bolt;
}

void G_CheckMissileImpact(edict_t *self, edict_t *bolt)
{
    trace_t tr;
    trap_Trace(&tr, self->s.origin, NULL, NULL, bolt->s.origin, bolt->s.number, bolt->clipmask);
    if (tr.fraction < 1.0f) {
        VectorAdd(tr.endpos, tr.plane.normal, bolt->s.origin);
        bolt->touch(bolt, &g_edicts[tr.entnum], &tr, false);
    }
}

edict_t *fire_blaster(edict_t *self, const vec3_t start, const vec3_t dir, int damage, int speed, effects_t effect, mod_t mod)
{
    edict_t *bolt;

    bolt = G_SpawnMissile(self, start, dir, speed);
    bolt->flags |= FL_DODGE;
    bolt->s.effects |= effect;
    bolt->s.renderfx |= RF_NOSHADOW;
    bolt->s.modelindex = G_ModelIndex("models/objects/laser/tris.md2");
    bolt->s.sound = G_SoundIndex("misc/lasfly.wav");
    bolt->touch = blaster_touch;
    bolt->nextthink = level.time + SEC(2);
    bolt->think = G_FreeEdict;
    bolt->dmg = damage;
    bolt->classname = "bolt";
    bolt->style = mod.id;
    trap_LinkEntity(bolt);

    G_CheckMissileImpact(self, bolt);
    return bolt;
}

#define SPAWNFLAG_GRENADE_HAND  1
#define SPAWNFLAG_GRENADE_HELD  2

/*
=================
fire_grenade
=================
*/
static void Grenade_ExplodeReal(edict_t *ent, edict_t *other, const vec3_t normal)
{
    edict_t *owner = &g_edicts[ent->r.ownernum];
    mod_id_t mod;
    entity_event_t event;

    if (owner->client)
        PlayerNoise(owner, ent->s.origin, PNOISE_IMPACT);

    // FIXME: if we are onground then raise our Z just a bit since we are a point?
    if (other) {
        vec3_t dir;

        VectorSubtract(other->s.origin, ent->s.origin, dir);
        if (ent->spawnflags & SPAWNFLAG_GRENADE_HAND)
            mod = MOD_HANDGRENADE;
        else
            mod = MOD_GRENADE;
        T_Damage(other, ent, owner, dir, ent->s.origin, DirToByte(normal), ent->dmg, ent->dmg,
                 mod == MOD_HANDGRENADE ? DAMAGE_RADIUS : DAMAGE_NONE, (mod_t) { mod });
    }

    if (ent->spawnflags & SPAWNFLAG_GRENADE_HELD)
        mod = MOD_HELD_GRENADE;
    else if (ent->spawnflags & SPAWNFLAG_GRENADE_HAND)
        mod = MOD_HG_SPLASH;
    else
        mod = MOD_G_SPLASH;
    T_RadiusDamage(ent, owner, ent->dmg, other, ent->dmg_radius, DAMAGE_NONE, (mod_t) { mod });

    VectorAdd(ent->s.origin, normal, ent->s.origin);
    if (ent->waterlevel)
        event = ent->groundentity ? EV_GRENADE_EXPLOSION_WATER : EV_ROCKET_EXPLOSION_WATER;
    else
        event = ent->groundentity ? EV_GRENADE_EXPLOSION : EV_ROCKET_EXPLOSION;
    G_BecomeEvent(ent, event, 0);
}

void THINK(Grenade_Explode)(edict_t *ent)
{
    vec3_t normal;
    VectorScale(ent->velocity, -0.02f, normal);
    Grenade_ExplodeReal(ent, NULL, normal);
}

void TOUCH(Grenade_Touch)(edict_t *ent, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    if (other == &g_edicts[ent->r.ownernum])
        return;

    if (tr->surface_flags & SURF_SKY) {
        G_FreeEdict(ent);
        return;
    }

    if (!other->takedamage) {
        if (!(ent->spawnflags & SPAWNFLAG_GRENADE_HAND))
            G_StartSound(ent, CHAN_VOICE, G_SoundIndex("weapons/grenlb1b.wav"), 1, ATTN_NORM);
        else if (brandom())
            G_StartSound(ent, CHAN_VOICE, G_SoundIndex("weapons/hgrenb1a.wav"), 1, ATTN_NORM);
        else
            G_StartSound(ent, CHAN_VOICE, G_SoundIndex("weapons/hgrenb2a.wav"), 1, ATTN_NORM);
        return;
    }

    Grenade_ExplodeReal(ent, other, tr->plane.normal);
}

void THINK(Grenade4_Think)(edict_t *self)
{
    if (level.time >= self->timestamp) {
        Grenade_Explode(self);
        return;
    }

    if (!VectorEmpty(self->velocity)) {
        float p = self->s.angles[0];
        float r = self->s.angles[2];
        float speed_frac = Q_clipf(VectorLengthSquared(self->velocity) / (self->speed * self->speed), 0, 1);
        vectoangles(self->velocity, self->s.angles);
        self->s.angles[0] = LerpAngle(p, self->s.angles[0], speed_frac);
        self->s.angles[2] = r + (FRAME_TIME_SEC * 360 * speed_frac);
    }

    self->nextthink = level.time + FRAME_TIME;
}

void fire_grenade(edict_t *self, const vec3_t start, const vec3_t aimdir, int damage, int speed, gtime_t timer, float damage_radius, float right_adjust, float up_adjust, bool monster)
{
    edict_t *grenade;
    vec3_t   dir;
    vec3_t   forward, right, up;

    vectoangles(aimdir, dir);
    AngleVectors(dir, forward, right, up);

    grenade = G_Spawn();
    VectorCopy(start, grenade->s.origin);
    VectorScale(aimdir, speed, grenade->velocity);

    if (up_adjust) {
        up_adjust *= level.gravity / 800.0f;
        VectorMA(grenade->velocity, up_adjust, up, grenade->velocity);
    }

    if (right_adjust)
        VectorMA(grenade->velocity, right_adjust, right, grenade->velocity);

    grenade->movetype = MOVETYPE_BOUNCE;
    grenade->clipmask = G_ProjectileClipmask(self);
    grenade->r.solid = SOLID_BBOX;
    grenade->r.svflags |= SVF_PROJECTILE | SVF_TRAP;
    grenade->flags |= FL_DODGE;
    grenade->s.effects |= EF_GRENADE;
    grenade->speed = speed;
    if (monster) {
        crandom_vec(grenade->avelocity, 360);
        grenade->s.modelindex = G_ModelIndex("models/objects/grenade/tris.md2");
        grenade->nextthink = level.time + timer;
        grenade->think = Grenade_Explode;
        grenade->s.morefx |= EFX_GRENADE_LIGHT;
    } else {
        grenade->s.modelindex = G_ModelIndex("models/objects/grenade4/tris.md2");
        vectoangles(grenade->velocity, grenade->s.angles);
        grenade->nextthink = level.time + FRAME_TIME;
        grenade->timestamp = level.time + timer;
        grenade->think = Grenade4_Think;
        grenade->s.renderfx |= RF_MINLIGHT;
    }
    grenade->r.ownernum = self->s.number;
    grenade->touch = Grenade_Touch;
    grenade->dmg = damage;
    grenade->dmg_radius = damage_radius;
    grenade->classname = "grenade";

    trap_LinkEntity(grenade);
}

void fire_grenade2(edict_t *self, const vec3_t start, const vec3_t aimdir, int damage, int speed, gtime_t timer, float damage_radius, bool held)
{
    edict_t *grenade;
    vec3_t   dir;
    vec3_t   forward, right, up;

    vectoangles(aimdir, dir);
    AngleVectors(dir, forward, right, up);

    grenade = G_Spawn();
    VectorCopy(start, grenade->s.origin);
    VectorScale(aimdir, speed, grenade->velocity);

    float scale = (200 + crandom() * 10.0f) * (level.gravity / 800.0f);
    VectorMA(grenade->velocity, scale, up, grenade->velocity);

    scale = crandom() * 10.0f;
    VectorMA(grenade->velocity, scale, right, grenade->velocity);

    crandom_vec(grenade->avelocity, 360);

    grenade->movetype = MOVETYPE_BOUNCE;
    grenade->clipmask = G_ProjectileClipmask(self);
    grenade->r.solid = SOLID_BBOX;
    grenade->r.svflags |= SVF_PROJECTILE | SVF_TRAP;
    grenade->flags |= FL_DODGE;
    grenade->s.effects |= EF_GRENADE;

    grenade->s.modelindex = G_ModelIndex("models/objects/grenade3/tris.md2");
    grenade->r.ownernum = self->s.number;
    grenade->touch = Grenade_Touch;
    grenade->nextthink = level.time + timer;
    grenade->think = Grenade_Explode;
    grenade->dmg = damage;
    grenade->dmg_radius = damage_radius;
    grenade->classname = "hand_grenade";
    grenade->spawnflags = SPAWNFLAG_GRENADE_HAND;
    if (held)
        grenade->spawnflags |= SPAWNFLAG_GRENADE_HELD;
    grenade->s.sound = G_SoundIndex("weapons/hgrenc1b.wav");

    if (timer <= 0)
        Grenade_Explode(grenade);
    else {
        G_StartSound(self, CHAN_WEAPON, G_SoundIndex("weapons/hgrent1a.wav"), 1, ATTN_NORM);
        trap_LinkEntity(grenade);
    }
}

/*
=================
fire_rocket
=================
*/
void TOUCH(rocket_touch)(edict_t *ent, edict_t *other, const trace_t *tr, bool other_touching_self)
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

    if (other->takedamage) {
        T_Damage(other, ent, owner, ent->velocity, ent->s.origin, tr->plane.dir, ent->dmg, ent->dmg, DAMAGE_NONE, (mod_t) { MOD_ROCKET });
        // don't throw any debris in net games
    } else if (!deathmatch.integer && !coop.integer && !(tr->surface_flags & (SURF_WARP | SURF_TRANS33 | SURF_TRANS66 | SURF_FLOWING))) {
        int n = irandom1(5);
        while (n--)
            ThrowGib(ent, "models/objects/debris2/tris.md2", 2, GIB_METALLIC | GIB_DEBRIS);
    }

    T_RadiusDamage(ent, owner, ent->radius_dmg, other, ent->dmg_radius, DAMAGE_NONE, (mod_t) { MOD_R_SPLASH });

    VectorAdd(ent->s.origin, tr->plane.normal, ent->s.origin);
    G_BecomeEvent(ent, ent->waterlevel ? EV_ROCKET_EXPLOSION_WATER : EV_ROCKET_EXPLOSION, 0);
}

edict_t *fire_rocket(edict_t *self, const vec3_t start, const vec3_t dir, int damage, int speed, float damage_radius, int radius_damage)
{
    edict_t *rocket;

    rocket = G_SpawnMissile(self, start, dir, speed);
    rocket->flags |= FL_DODGE;
    rocket->s.effects |= EF_ROCKET;
    rocket->s.modelindex = G_ModelIndex("models/objects/rocket/tris.md2");
    rocket->s.sound = G_SoundIndex("weapons/rockfly.wav");
    rocket->touch = rocket_touch;
    rocket->nextthink = level.time + SEC(8000.0f / speed);
    rocket->think = G_FreeEdict;
    rocket->dmg = damage;
    rocket->radius_dmg = radius_damage;
    rocket->dmg_radius = damage_radius;
    rocket->classname = "rocket";

    trap_LinkEntity(rocket);

    return rocket;
}

/*
=================
fire_rail
=================
*/
bool fire_rail(edict_t *self, const vec3_t start, const vec3_t aimdir, int damage, int kick)
{
    contents_t mask = G_ProjectileClipmask(self);

    vec3_t end;
    VectorMA(start, 8192, aimdir, end);

    pierce_t pierce;
    trace_t tr;

    pierce_begin(&pierce);

    while (1) {
        trap_Trace(&tr, start, NULL, NULL, end, self->s.number, mask);

        // didn't hit anything, so we're done
        if (tr.fraction == 1.0f)
            break;

        edict_t *hit = &g_edicts[tr.entnum];

        // try to kill it first
        if ((hit != self) && (hit->takedamage))
            T_Damage(hit, self, self, aimdir, tr.endpos, tr.plane.dir, damage, kick, DAMAGE_NONE, (mod_t) { MOD_RAILGUN });

        // dead, so we don't need to care about checking pierce
        if (!hit->r.inuse || (!hit->r.solid || hit->r.solid == SOLID_TRIGGER))
            continue;

        // ZOID--added so rail goes through SOLID_BBOX entities (gibs, etc)
        if ((hit->r.svflags & SVF_MONSTER) || (hit->client) ||
            // ROGUE
            (hit->flags & FL_DAMAGEABLE) ||
            // ROGUE
            (hit->r.solid == SOLID_BBOX)) {
            if (pierce_mark(&pierce, hit))
                continue;
        }

        // hit a solid, so we're stopping here
        break;
    }

    pierce_end(&pierce);

    // send gun puff / flash
    entity_event_t te = (deathmatch.integer && g_instagib.integer) ? EV_RAILTRAIL2 : EV_RAILTRAIL;
    G_SnapVectorTowards(tr.endpos, start, end);
    G_SpawnTrail(start, end, te);

    if (self->client)
        PlayerNoise(self, tr.endpos, PNOISE_IMPACT);

    return pierce.count;
}

static void bfg_laser_pos(const vec3_t p, float dist, vec3_t out)
{
    float theta = frandom1(2 * M_PIf);
    float phi = acosf(crandom());

    vec3_t d = {
        sinf(phi) * cosf(theta),
        sinf(phi) * sinf(theta),
        cosf(phi)
    };

    VectorMA(p, dist, d, out);
}

void THINK(bfg_laser_update)(edict_t *self)
{
    edict_t *owner = &g_edicts[self->r.ownernum];

    if (level.time > self->timestamp || !owner->r.inuse) {
        G_FreeEdict(self);
        return;
    }

    G_SnapVector(owner->s.origin, self->s.origin);
    self->nextthink = level.time + FRAME_TIME;
    trap_LinkEntity(self);
}

static void bfg_spawn_laser(edict_t *self)
{
    vec3_t end;
    bfg_laser_pos(self->s.origin, 256, end);
    trace_t tr;
    trap_Trace(&tr, self->s.origin, NULL, NULL, end, self->s.number, MASK_OPAQUE | CONTENTS_PROJECTILECLIP);

    if (tr.fraction == 1.0f)
        return;

    edict_t *laser = G_Spawn();
    laser->s.frame = 3;
    laser->s.renderfx = RF_BEAM_LIGHTNING;
    laser->movetype = MOVETYPE_NONE;
    laser->r.solid = SOLID_NOT;
    laser->s.modelindex = MODELINDEX_DUMMY; // must be non-zero
    G_SnapVector(self->s.origin, laser->s.origin);
    G_SnapVectorTowards(tr.endpos, self->s.origin, laser->s.old_origin);
    laser->s.skinnum = 0xD0D0D0D0;
    laser->think = bfg_laser_update;
    laser->nextthink = level.time + FRAME_TIME;
    laser->timestamp = level.time + SEC(0.3f);
    laser->r.ownernum = self->s.number;
    trap_LinkEntity(laser);
}

/*
=================
fire_bfg
=================
*/
void THINK(bfg_explode)(edict_t *self)
{
    edict_t *ent, *owner;
    float    points;
    vec3_t   v;
    float    dist;

    bfg_spawn_laser(self);

    if (self->s.frame == 0) {
        // the BFG effect
        ent = NULL;
        owner = &g_edicts[self->r.ownernum];
        while ((ent = findradius(ent, self->s.origin, self->dmg_radius)) != NULL) {
            if (!ent->takedamage)
                continue;
            if (ent == owner)
                continue;
            if (!CanDamage(ent, self))
                continue;
            if (!CanDamage(ent, owner))
                continue;
            // ROGUE - make tesla hurt by bfg
            if (!(ent->r.svflags & SVF_MONSTER) && !(ent->flags & FL_DAMAGEABLE) && (!ent->client) && (strcmp(ent->classname, "misc_explobox") != 0))
                continue;
            // ZOID
            // don't target players in CTF
            if (CheckTeamDamage(ent, owner))
                continue;
            // ZOID

            vec3_t centroid;
            VectorAvg(ent->r.mins, ent->r.maxs, v);
            VectorAdd(ent->s.origin, v, centroid);
            dist = Distance(self->s.origin, centroid);
            points = self->radius_dmg * (1.0f - sqrtf(dist / self->dmg_radius));

            T_Damage(ent, self, owner, self->velocity, centroid, 0, points, 0, DAMAGE_ENERGY, (mod_t) { MOD_BFG_EFFECT });

            // Paril: draw BFG lightning laser to enemies
            G_SpawnTrail(self->s.origin, centroid, EV_BFG_ZAP);
        }
    }

    self->nextthink = level.time + HZ(10);
    self->s.frame++;
    if (self->s.frame == 5)
        self->think = G_FreeEdict;
}

void TOUCH(bfg_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
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

    // core explosion - prevents firing it into the wall/floor
    if (other->takedamage)
        T_Damage(other, self, owner, self->velocity, self->s.origin, tr->plane.dir, 200, 0, DAMAGE_ENERGY, (mod_t) { MOD_BFG_BLAST });
    T_RadiusDamage(self, owner, 200, other, 100, DAMAGE_ENERGY, (mod_t) { MOD_BFG_BLAST });

    G_StartSound(self, CHAN_VOICE, G_SoundIndex("weapons/bfg__x1b.wav"), 1, ATTN_NORM);
    self->r.solid = SOLID_NOT;
    self->touch = NULL;
    VectorMA(self->s.origin, -1 * FRAME_TIME_SEC, self->velocity, self->s.origin);
    VectorClear(self->velocity);
    self->s.modelindex = G_ModelIndex("sprites/s_bfg3.sp2");
    self->s.frame = 0;
    self->s.sound = 0;
    self->s.effects &= ~EF_ANIM_ALLFAST;
    self->think = bfg_explode;
    self->nextthink = level.time + HZ(10);
    self->enemy = other;

    G_AddEvent(self, EV_BFG_EXPLOSION_BIG, 0);
}

void THINK(bfg_think)(edict_t *self)
{
    edict_t *ent, *owner, *hit;
    vec3_t   point, dir, start, end, pos;
    int      dmg;
    trace_t  tr;

    if (deathmatch.integer)
        dmg = 5;
    else
        dmg = 10;

    bfg_spawn_laser(self);

    ent = NULL;
    owner = &g_edicts[self->r.ownernum];
    while ((ent = findradius(ent, self->s.origin, 256)) != NULL) {
        if (ent == self)
            continue;

        if (ent == owner)
            continue;

        if (!ent->takedamage)
            continue;

        // ROGUE - make tesla hurt by bfg
        if (!(ent->r.svflags & SVF_MONSTER) && !(ent->flags & FL_DAMAGEABLE) && (!ent->client) && (strcmp(ent->classname, "misc_explobox") != 0))
            continue;
        // ZOID
        // don't target players in CTF
        if (CheckTeamDamage(ent, owner))
            continue;
        // ZOID

        VectorAvg(ent->r.absmin, ent->r.absmax, point);

        VectorSubtract(point, self->s.origin, dir);
        VectorNormalize(dir);

        VectorCopy(self->s.origin, start);
        VectorMA(start, 2048, dir, end);

        // [Paril-KEX] don't fire a laser if we're blocked by the world
        trap_Trace(&tr, start, NULL, NULL, point, ENTITYNUM_NONE, MASK_SOLID | CONTENTS_PROJECTILECLIP);

        if (tr.fraction < 1.0f)
            continue;

        pierce_t pierce;
        pierce_begin(&pierce);

        do {
            trap_Trace(&tr, start, NULL, NULL, end, self->s.number,
                       CONTENTS_SOLID | CONTENTS_MONSTER | CONTENTS_PLAYER | CONTENTS_DEADMONSTER | CONTENTS_PROJECTILECLIP);

            // didn't hit anything, so we're done
            if (tr.fraction == 1.0f)
                break;

            hit = &g_edicts[tr.entnum];

            // hurt it if we can
            if ((hit->takedamage) && !(hit->flags & FL_IMMUNE_LASER) && (hit != owner))
                T_Damage(hit, self, owner, dir, tr.endpos, 0, dmg, 1, DAMAGE_ENERGY, (mod_t) { MOD_BFG_LASER });

            // if we hit something that's not a monster or player we're done
            if (!(hit->r.svflags & SVF_MONSTER) && !(hit->flags & FL_DAMAGEABLE) && (!hit->client)) {
                G_SnapVectorTowards(tr.endpos, start, pos);
                G_TempEntity(pos, EV_LASER_SPARKS, MakeLittleLong(tr.plane.dir, 208, 4, 0));
                break;
            }
        } while (pierce_mark(&pierce, hit));

        pierce_end(&pierce);

        G_SnapVectorTowards(tr.endpos, start, pos);
        G_SpawnTrail(self->s.origin, pos, EV_BFG_LASER);
    }

    self->nextthink = level.time + HZ(10);
}

void fire_bfg(edict_t *self, const vec3_t start, const vec3_t dir, int damage, int speed, float damage_radius)
{
    edict_t *bfg;

    bfg = G_SpawnMissile(self, start, dir, speed);
    bfg->s.effects |= EF_BFG | EF_ANIM_ALLFAST;
    bfg->s.modelindex = G_ModelIndex("sprites/s_bfg1.sp2");
    bfg->s.sound = G_SoundIndex("weapons/bfg__l1a.wav");
    bfg->touch = bfg_touch;
    bfg->nextthink = level.time + SEC(8000.0f / speed);
    bfg->think = G_FreeEdict;
    bfg->radius_dmg = damage;
    bfg->dmg_radius = damage_radius;
    bfg->classname = "bfg blast";
    bfg->think = bfg_think;
    bfg->nextthink = level.time + FRAME_TIME;
    bfg->teammaster = bfg;
    bfg->teamchain = NULL;

    trap_LinkEntity(bfg);
}

void TOUCH(disintegrator_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    vec3_t pos;
    VectorMA(self->s.origin, -0.01f, self->velocity, pos);

    G_TempEntity(pos, EV_WIDOWSPLASH, 0);

    G_FreeEdict(self);

    if (other->r.svflags & (SVF_MONSTER | SVF_PLAYER)) {
        other->disintegrator_time += SEC(50);
        other->disintegrator = &g_edicts[self->r.ownernum];
    }
}

void fire_disintegrator(edict_t *self, const vec3_t start, const vec3_t dir, int speed)
{
    edict_t *bfg;

    bfg = G_SpawnMissile(self, start, dir, speed);
    bfg->flags |= FL_DODGE;
    bfg->s.effects |= EF_TAGTRAIL | EF_ANIM_ALL;
    bfg->s.renderfx |= RF_TRANSLUCENT;
    bfg->s.modelindex = G_ModelIndex("sprites/s_bfg1.sp2");
    bfg->s.sound = G_SoundIndex("weapons/bfg__l1a.wav");
    bfg->touch = disintegrator_touch;
    bfg->nextthink = level.time + SEC(8000.0f / speed);
    bfg->think = G_FreeEdict;
    bfg->classname = "disint ball";

    trap_LinkEntity(bfg);
}
