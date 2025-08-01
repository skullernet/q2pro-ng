// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "g_local.h"

/*
========================
fire_flechette
========================
*/
void TOUCH(flechette_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
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
        T_Damage(other, self, owner, self->velocity, self->s.origin, tr->plane.dir,
                 self->dmg, self->dmg_radius, DAMAGE_NO_REG_ARMOR, (mod_t) { MOD_ETF_RIFLE });
        G_FreeEdict(self);
    } else {
        G_BecomeEvent(self, EV_FLECHETTE, tr->plane.dir);
    }
}

void fire_flechette(edict_t *self, const vec3_t start, const vec3_t dir, int damage, int speed, int kick)
{
    edict_t *bolt;

    bolt = G_SpawnMissile(self, start, dir, speed);
    bolt->flags |= FL_DODGE;
    bolt->s.renderfx |= RF_FULLBRIGHT;
    bolt->s.modelindex = G_ModelIndex("models/proj/flechette/tris.md2");
    bolt->touch = flechette_touch;
    bolt->nextthink = level.time + SEC(8000.0f / speed);
    bolt->think = G_FreeEdict;
    bolt->dmg = damage;
    bolt->dmg_radius = kick;
    trap_LinkEntity(bolt);

    G_CheckMissileImpact(self, bolt);
}

// **************************
// PROX
// **************************

#define PROX_TIME_TO_LIVE   SEC(45) // 45, 30, 15, 10
#define PROX_TIME_DELAY     SEC(0.5f)
#define PROX_BOUND_SIZE     96
#define PROX_DAMAGE_RADIUS  192
#define PROX_HEALTH         20
#define PROX_DAMAGE         60
#define PROX_DAMAGE_OPEN_MULT 1.5f // expands 60 to 90 when it opens

static void Prox_ExplodeReal(edict_t *ent, edict_t *other, const vec3_t normal)
{
    edict_t *owner;

    // free the trigger field

    // PMM - changed teammaster to "mover" .. owner of the field is the prox
    if (ent->teamchain && ent->teamchain->r.ownernum == ent->s.number)
        G_FreeEdict(ent->teamchain);

    owner = ent;
    if (ent->teammaster) {
        owner = ent->teammaster;
        PlayerNoise(owner, ent->s.origin, PNOISE_IMPACT);
    }

    if (other) {
        vec3_t dir;
        VectorSubtract(other->s.origin, ent->s.origin, dir);
        T_Damage(other, ent, owner, dir, ent->s.origin, DirToByte(normal),
                 ent->dmg, ent->dmg, DAMAGE_NONE, (mod_t) { MOD_PROX });
    }

    // play quad sound if appopriate
    if (ent->dmg > PROX_DAMAGE * PROX_DAMAGE_OPEN_MULT)
        G_StartSound(ent, CHAN_ITEM, G_SoundIndex("items/damage3.wav"), 1, ATTN_NORM);

    ent->takedamage = false;
    T_RadiusDamage(ent, owner, ent->dmg, other, PROX_DAMAGE_RADIUS, DAMAGE_NONE, (mod_t) { MOD_PROX });

    VectorAdd(ent->s.origin, normal, ent->s.origin);
    G_BecomeEvent(ent, ent->groundentity ? EV_GRENADE_EXPLOSION : EV_ROCKET_EXPLOSION, 0);
}

void THINK(Prox_Explode)(edict_t *ent)
{
    vec3_t normal;
    VectorScale(ent->velocity, -0.02f, normal);
    Prox_ExplodeReal(ent, NULL, normal);
}

void DIE(prox_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t point, mod_t mod)
{
    // if set off by another prox, delay a little (chained explosions)
    if (strcmp(inflictor->classname, "prox_mine")) {
        self->takedamage = false;
        Prox_Explode(self);
    } else {
        self->takedamage = false;
        self->think = Prox_Explode;
        self->nextthink = level.time + FRAME_TIME;
    }
}

void TOUCH(Prox_Field_Touch)(edict_t *ent, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    edict_t *prox;

    if (!(other->r.svflags & SVF_MONSTER) && !other->client)
        return;

    // trigger the prox mine if it's still there, and still mine.
    prox = &g_edicts[ent->r.ownernum];

    // teammate avoidance
    if (CheckTeamDamage(prox->teammaster, other))
        return;

    if (!deathmatch.integer && other->client)
        return;

    if (other == prox) // don't set self off
        return;

    if (prox->think == Prox_Explode) // we're set to blow!
        return;

    if (prox->teamchain == ent) {
        G_StartSound(ent, CHAN_VOICE, G_SoundIndex("weapons/proxwarn.wav"), 1, ATTN_NORM);
        prox->think = Prox_Explode;
        prox->nextthink = level.time + PROX_TIME_DELAY;
        return;
    }

    ent->r.solid = SOLID_NOT;
    G_FreeEdict(ent);
}

void THINK(prox_seek)(edict_t *ent)
{
    if (level.time >= ent->timestamp) {
        Prox_Explode(ent);
        return;
    }

    ent->s.frame++;
    if (ent->s.frame > 13)
        ent->s.frame = 9;
    ent->think = prox_seek;
    ent->nextthink = level.time + HZ(10);
}

static bool monster_or_player(edict_t *ent)
{
    return ((ent->r.svflags & SVF_MONSTER) || (deathmatch.integer && (ent->client || !strcmp(ent->classname, "prox_mine")))) && (ent->health > 0);
}

bool player_start_point(edict_t *ent)
{
    return deathmatch.integer && (!strncmp(ent->classname, "info_player_", 12) || !strcmp(ent->classname, "misc_teleporter_dest") || !strncmp(ent->classname, "item_flag_", 10));
}

void THINK(prox_open)(edict_t *ent)
{
    edict_t *search;

    if (ent->s.frame == 9) { // end of opening animation
        ent->s.sound = 0;

        // set the owner to NULL so the owner can walk through it.  needs to be done here so the owner
        // doesn't get stuck on it while it's opening if fired at point blank wall
        if (deathmatch.integer)
            ent->r.ownernum = ENTITYNUM_NONE;

        if (ent->teamchain)
            ent->teamchain->touch = Prox_Field_Touch;

        search = NULL;
        while ((search = findradius(search, ent->s.origin, PROX_DAMAGE_RADIUS + 10)) != NULL) {
            if (search == ent)
                continue;
            if (!search->classname) // tag token and other weird shit
                continue;
            // teammate avoidance
            if (CheckTeamDamage(search, ent->teammaster))
                continue;

            // if it's a monster or player with health > 0
            // or it's a player start point
            // and we can see it
            // blow up
            if (!monster_or_player(search) && !player_start_point(search))
                continue;
            if (!visible(search, ent))
                continue;

            G_StartSound(ent, CHAN_VOICE, G_SoundIndex("weapons/proxwarn.wav"), 1, ATTN_NORM);
            Prox_Explode(ent);
            return;
        }

        if (g_dm_strong_mines.integer)
            ent->timestamp = level.time + PROX_TIME_TO_LIVE;
        else {
            switch ((int)(ent->dmg / (PROX_DAMAGE * PROX_DAMAGE_OPEN_MULT))) {
            default:
            case 1:
                ent->timestamp = level.time + PROX_TIME_TO_LIVE;
                break;
            case 2:
                ent->timestamp = level.time + SEC(30);
                break;
            case 4:
                ent->timestamp = level.time + SEC(15);
                break;
            case 8:
                ent->timestamp = level.time + SEC(10);
                break;
            }
        }

        ent->think = prox_seek;
        ent->nextthink = level.time + SEC(0.2f);
    } else {
        if (ent->s.frame == 0) {
            G_StartSound(ent, CHAN_VOICE, G_SoundIndex("weapons/proxopen.wav"), 1, ATTN_NORM);
            ent->dmg *= PROX_DAMAGE_OPEN_MULT;
        }
        ent->s.frame++;
        ent->think = prox_open;
        ent->nextthink = level.time + HZ(10);
    }
}

void TOUCH(prox_land)(edict_t *ent, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    edict_t   *field;
    movetype_t movetype = MOVETYPE_NONE;
    vec3_t     land_point;

    // must turn off owner so owner can shoot it and set it off
    // moved to prox_open so owner can get away from it if fired at pointblank range into
    // wall
    if (tr->surface_flags & SURF_SKY) {
        G_FreeEdict(ent);
        return;
    }

    if (!VectorEmpty(tr->plane.normal)) {
        VectorMA(ent->s.origin, -10.0f, tr->plane.normal, land_point);
        if (trap_PointContents(land_point) & (CONTENTS_SLIME | CONTENTS_LAVA)) {
            Prox_Explode(ent);
            return;
        }
    }

    if (VectorEmpty(tr->plane.normal) || (other->r.svflags & SVF_MONSTER) || other->client || (other->flags & FL_DAMAGEABLE)) {
        if (other != ent->teammaster)
            Prox_ExplodeReal(ent, other, tr->plane.normal);
        return;
    }

    if (other != world) {
        // Here we need to check to see if we can stop on this entity.
        if (tr->plane.normal[2] <= 0.7f)
            return;

        if (other->movetype != MOVETYPE_PUSH) {
            Prox_Explode(ent);
            return;
        }

        float backoff = DotProduct(ent->velocity, tr->plane.normal) * 1.5f;
        float change = tr->plane.normal[2] * backoff;
        if (ent->velocity[2] - change > 60)
            return;

        movetype = MOVETYPE_BOUNCE;
    }

    if (trap_PointContents(ent->s.origin) & (CONTENTS_LAVA | CONTENTS_SLIME)) {
        Prox_Explode(ent);
        return;
    }

    field = G_Spawn();
    VectorCopy(ent->s.origin, field->s.origin);
    VectorSet(field->r.mins, -PROX_BOUND_SIZE, -PROX_BOUND_SIZE, -PROX_BOUND_SIZE);
    VectorSet(field->r.maxs, PROX_BOUND_SIZE, PROX_BOUND_SIZE, PROX_BOUND_SIZE);
    field->movetype = MOVETYPE_NONE;
    field->r.solid = SOLID_TRIGGER;
    field->r.ownernum = ent->s.number;
    field->classname = "prox_field";
    field->teammaster = ent;
    trap_LinkEntity(field);

    VectorClear(ent->velocity);
    VectorClear(ent->avelocity);
    vectoangles(tr->plane.normal, ent->s.angles);
    // rotate to vertical
    ent->s.angles[PITCH] += 90;
    ent->takedamage = true;
    ent->movetype = movetype; // either bounce or none, depending on whether we stuck to something
    ent->die = prox_die;
    ent->teamchain = field;
    ent->health = PROX_HEALTH;
    ent->nextthink = level.time;
    ent->think = prox_open;
    ent->touch = NULL;
    ent->r.solid = SOLID_BBOX;
    ent->r.svflags &= ~SVF_PROJECTILE;

    trap_LinkEntity(ent);
}

void THINK(Prox_Think)(edict_t *self)
{
    if (level.time >= self->timestamp) {
        Prox_Explode(self);
        return;
    }

    vectoangles(self->velocity, self->s.angles);
    self->s.angles[PITCH] -= 90;
    self->nextthink = level.time;
}

void fire_prox(edict_t *self, const vec3_t start, const vec3_t aimdir, int prox_damage_multiplier, int speed)
{
    edict_t *prox;
    vec3_t   dir;
    vec3_t   forward, right, up;

    vectoangles(aimdir, dir);
    AngleVectors(dir, forward, right, up);

    prox = G_Spawn();
    VectorCopy(start, prox->s.origin);
    VectorScale(aimdir, speed, prox->velocity);

    float scale = (200 + crandom() * 10.0f) * (level.gravity / 800.0f);
    VectorMA(prox->velocity, scale, up, prox->velocity);

    scale = crandom() * 10.0f;
    VectorMA(prox->velocity, scale, right, prox->velocity);

    VectorCopy(dir, prox->s.angles);
    prox->s.angles[PITCH] -= 90;
    prox->movetype = MOVETYPE_BOUNCE;
    prox->r.solid = SOLID_BBOX;
    prox->r.svflags |= SVF_PROJECTILE | SVF_TRAP;
    prox->s.effects |= EF_GRENADE;
    prox->flags |= FL_DODGE;
    prox->clipmask = MASK_PROJECTILE | CONTENTS_LAVA | CONTENTS_SLIME;

    // [Paril-KEX]
    if (self->client && !G_ShouldPlayersCollide(true))
        prox->clipmask &= ~CONTENTS_PLAYER;

    prox->s.renderfx |= RF_IR_VISIBLE;
    // FIXME - this needs to be bigger.  Has other effects, though.  Maybe have to change origin to compensate
    //  so it sinks in correctly.  Also in lavacheck, might have to up the distance
    VectorSet(prox->r.mins, -6, -6, -6);
    VectorSet(prox->r.maxs, 6, 6, 6);
    prox->s.modelindex = G_ModelIndex("models/weapons/g_prox/tris.md2");
    prox->r.ownernum = self->s.number;
    prox->teammaster = self;
    prox->touch = prox_land;
    prox->think = Prox_Think;
    prox->nextthink = level.time;
    prox->dmg = PROX_DAMAGE * prox_damage_multiplier;
    prox->classname = "prox_mine";
    prox->flags |= FL_DAMAGEABLE;
    prox->flags |= FL_MECHANICAL;

    switch (prox_damage_multiplier) {
    case 1:
    default:
        prox->timestamp = level.time + PROX_TIME_TO_LIVE;
        break;
    case 2:
        prox->timestamp = level.time + SEC(30);
        break;
    case 4:
        prox->timestamp = level.time + SEC(15);
        break;
    case 8:
        prox->timestamp = level.time + SEC(10);
        break;
    }

    trap_LinkEntity(prox);
}

// *************************
// MELEE WEAPONS
// *************************

bool fire_player_melee(edict_t *self, const vec3_t start, const vec3_t aim, int reach, int damage, int kick, mod_t mod)
{
    vec3_t mins, maxs;
    for (int i = 0; i < 3; i++) {
        mins[i] = self->r.absmin[i] - reach + 1;
        maxs[i] = self->r.absmax[i] + reach - 1;
    }

    // find all the things we could maybe hit
    int list[MAX_EDICTS_OLD];
    int count = trap_BoxEdicts(mins, maxs, list, q_countof(list), AREA_SOLID);

    bool was_hit = false;

    for (int i = 0; i < count; i++) {
        edict_t *check = g_edicts + list[i];

        if (check == self || !check->r.inuse || !check->takedamage)
            continue;

        // check distance
        vec3_t closest_point_to_check;
        vec3_t closest_point_to_self;

        closest_point_to_box(start, check->r.absmin, check->r.absmax,
                             closest_point_to_check);
        closest_point_to_box(closest_point_to_check, self->r.absmin,
                             self->r.absmax, closest_point_to_self);

        if (Distance(closest_point_to_check, closest_point_to_self) > reach)
            continue;

        // check angle if we aren't intersecting
        static const vec3_t shrink = { 2, 2, 2 };
        vec3_t mins2, maxs2;

        VectorAdd(self->r.absmin, shrink, mins);
        VectorSubtract(self->r.absmax, shrink, maxs);

        VectorAdd(check->r.absmin, shrink, mins2);
        VectorSubtract(check->r.absmax, shrink, maxs2);

        if (!boxes_intersect(mins, maxs, mins2, maxs2)) {
            vec3_t point, dir;
            VectorAvg(check->r.absmin, check->r.absmax, point);
            VectorSubtract(point, start, dir);
            VectorNormalize(dir);
            if (DotProduct(dir, aim) < 0.70f)
                continue;
        }

        if (!CanDamage(self, check))
            continue;

        // do the damage
        if (check->r.svflags & SVF_MONSTER)
            check->pain_debounce_time -= random_time_sec(0.005f, 0.075f);

        vec3_t normal;
        VectorNegate(aim, normal);

        if (mod.id == MOD_CHAINFIST)
            T_Damage(check, self, self, aim, closest_point_to_check, DirToByte(normal),
                     damage, kick / 2, DAMAGE_DESTROY_ARMOR | DAMAGE_NO_KNOCKBACK, mod);
        else
            T_Damage(check, self, self, aim, closest_point_to_check, DirToByte(normal),
                     damage, kick / 2, DAMAGE_NO_KNOCKBACK, mod);

        was_hit = true;
    }

    return was_hit;
}

// *************************
// NUKE
// *************************

#define NUKE_DELAY          SEC(4)
#define NUKE_TIME_TO_LIVE   SEC(6)
#define NUKE_RADIUS         512
#define NUKE_DAMAGE         400
#define NUKE_QUAKE_TIME     SEC(3)
#define NUKE_QUAKE_STRENGTH 100

void THINK(Nuke_Quake)(edict_t *self)
{
    int      i;
    edict_t *e;

    if (self->last_move_time < level.time) {
        G_StartSound(self, CHAN_AUTO, self->noise_index, 0.75f, ATTN_NONE);
        self->last_move_time = level.time + SEC(0.5f);
    }

    for (i = 0, e = g_edicts + i; i < game.maxclients; i++, e++) {
        if (!e->r.inuse)
            continue;
        if (!e->client)
            continue;
        if (!e->groundentity)
            continue;

        e->groundentity = NULL;
        e->velocity[0] += crandom() * 150;
        e->velocity[1] += crandom() * 150;
        e->velocity[2] = self->speed * (100.0f / e->mass);
    }

    if (level.time < self->timestamp)
        self->nextthink = level.time + FRAME_TIME;
    else
        G_FreeEdict(self);
}

static void Nuke_Explode(edict_t *ent)
{
    if (ent->teammaster->client)
        PlayerNoise(ent->teammaster, ent->s.origin, PNOISE_IMPACT);

    T_RadiusNukeDamage(ent, ent->teammaster, ent->dmg, ent, ent->dmg_radius, (mod_t) { MOD_NUKE });

    if (ent->dmg > NUKE_DAMAGE)
        G_StartSound(ent, CHAN_ITEM, G_SoundIndex("items/damage3.wav"), 1, ATTN_NORM);

    G_AddEvent(ent, EV_NUKEBLAST, 0);

    // become a quake
    ent->s.modelindex = 0;
    ent->noise_index = G_SoundIndex("world/rumble.wav");
    ent->think = Nuke_Quake;
    ent->speed = NUKE_QUAKE_STRENGTH;
    ent->timestamp = level.time + NUKE_QUAKE_TIME;
    ent->nextthink = level.time + FRAME_TIME;
    ent->last_move_time = 0;
}

void DIE(nuke_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t point, mod_t mod)
{
    self->takedamage = false;

    if (attacker && attacker->classname && !strcmp(attacker->classname, "nuke"))
        G_FreeEdict(self);
    else
        Nuke_Explode(self);
}

void THINK(Nuke_Think)(edict_t *ent)
{
    float           attenuation, default_atten = 1.8f;
    player_muzzle_t muzzleflash;

    if (level.time >= ent->timestamp) {
        Nuke_Explode(ent);
        return;
    }

    switch (ent->dmg / NUKE_DAMAGE) {
    case 1:
        attenuation = default_atten / 1.4f;
        muzzleflash = MZ_NUKE1;
        break;
    case 2:
        attenuation = default_atten / 2.0f;
        muzzleflash = MZ_NUKE2;
        break;
    case 4:
        attenuation = default_atten / 3.0f;
        muzzleflash = MZ_NUKE4;
        break;
    case 8:
        attenuation = default_atten / 5.0f;
        muzzleflash = MZ_NUKE8;
        break;
    default:
        attenuation = default_atten;
        muzzleflash = MZ_NUKE1;
        break;
    }

    gtime_t remaining = ent->timestamp - level.time;
    if (remaining < NUKE_TIME_TO_LIVE) {
        ent->s.frame++;

        if (ent->s.frame > 11)
            ent->s.frame = 6;

        if (trap_PointContents(ent->s.origin) & (CONTENTS_SLIME | CONTENTS_LAVA)) {
            Nuke_Explode(ent);
            return;
        }

        ent->think = Nuke_Think;
        ent->nextthink = level.time + HZ(10);
        ent->health = 1;
        ent->r.ownernum = ENTITYNUM_NONE;
        G_AddEvent(ent, EV_MUZZLEFLASH, muzzleflash);

        if (ent->pain_debounce_time <= level.time) {
            if (remaining <= (NUKE_TIME_TO_LIVE / 2.0f)) {
                G_StartSound(ent, CHAN_VOICE, G_SoundIndex("weapons/nukewarn2.wav"), 1, attenuation);
                ent->pain_debounce_time = level.time + SEC(0.3f);
            } else {
                G_StartSound(ent, CHAN_VOICE, G_SoundIndex("weapons/nukewarn2.wav"), 1, attenuation);
                ent->pain_debounce_time = level.time + SEC(0.5f);
            }
        }
    } else {
        if (ent->pain_debounce_time <= level.time) {
            G_StartSound(ent, CHAN_VOICE, G_SoundIndex("weapons/nukewarn2.wav"), 1, attenuation);
            ent->pain_debounce_time = level.time + SEC(1);
        }
        ent->nextthink = level.time + FRAME_TIME;
    }
}

void TOUCH(nuke_bounce)(edict_t *ent, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    if (tr->surface_id) {
        if (brandom())
            G_StartSound(ent, CHAN_BODY, G_SoundIndex("weapons/hgrenb1a.wav"), 1, ATTN_NORM);
        else
            G_StartSound(ent, CHAN_BODY, G_SoundIndex("weapons/hgrenb2a.wav"), 1, ATTN_NORM);
    }
}

void fire_nuke(edict_t *self, const vec3_t start, const vec3_t aimdir, int speed)
{
    edict_t *nuke;
    vec3_t   dir;
    vec3_t   forward, right, up;
    int      damage_modifier = P_DamageModifier(self);

    vectoangles(aimdir, dir);
    AngleVectors(dir, forward, right, up);

    nuke = G_Spawn();
    VectorCopy(start, nuke->s.origin);
    VectorScale(aimdir, speed, nuke->velocity);

    float scale = 200 + crandom() * 10.0f;
    VectorMA(nuke->velocity, scale, up, nuke->velocity);

    scale = crandom() * 10.0f;
    VectorMA(nuke->velocity, scale, right, nuke->velocity);

    nuke->r.svflags |= SVF_NOCULL;
    nuke->movetype = MOVETYPE_BOUNCE;
    nuke->clipmask = MASK_PROJECTILE;
    nuke->r.solid = SOLID_BBOX;
    nuke->s.effects |= EF_GRENADE;
    nuke->s.renderfx |= RF_IR_VISIBLE;
    VectorSet(nuke->r.mins, -8, -8, 0);
    VectorSet(nuke->r.maxs, 8, 8, 16);
    nuke->s.modelindex = G_ModelIndex("models/weapons/g_nuke/tris.md2");
    nuke->r.ownernum = self->s.number;
    nuke->teammaster = self;
    nuke->nextthink = level.time + FRAME_TIME;
    nuke->timestamp = level.time + NUKE_DELAY + NUKE_TIME_TO_LIVE;
    nuke->think = Nuke_Think;
    nuke->touch = nuke_bounce;

    nuke->health = 10000;
    nuke->takedamage = true;
    nuke->flags |= FL_DAMAGEABLE;
    nuke->dmg = NUKE_DAMAGE * damage_modifier;
    if (damage_modifier == 1)
        nuke->dmg_radius = NUKE_RADIUS;
    else
        nuke->dmg_radius = NUKE_RADIUS + NUKE_RADIUS * (0.25f * damage_modifier);
    // this yields 1.0, 1.5, 2.0, 3.0 times radius

    nuke->classname = "nuke";
    nuke->die = nuke_die;

    trap_LinkEntity(nuke);
}

// *************************
// TESLA
// *************************

#define TESLA_TIME_TO_LIVE          SEC(30)
#define TESLA_DAMAGE_RADIUS         128
#define TESLA_DAMAGE                3
#define TESLA_KNOCKBACK             8
#define TESLA_ACTIVATE_TIME         SEC(3)
#define TESLA_EXPLOSION_DAMAGE_MULT 50 // this is the amount the damage is multiplied by for underwater explosions
#define TESLA_EXPLOSION_RADIUS      200

static void tesla_remove(edict_t *self)
{
    edict_t *cur, *next;

    self->takedamage = false;
    if (self->teamchain) {
        for (cur = self->teamchain; cur; cur = next) {
            next = cur->teamchain;
            G_FreeEdict(cur);
        }
    } else if (self->air_finished)
        G_Printf("tesla_mine without a field!\n");

    self->r.ownernum = self->teammaster->s.number; // Going away, set the owner correctly.
    // PGM - grenade explode does damage to self->enemy
    self->enemy = NULL;

    // play quad sound if quadded and an underwater explosion
    if ((self->dmg_radius) && (self->dmg > (TESLA_DAMAGE * TESLA_EXPLOSION_DAMAGE_MULT)))
        G_StartSound(self, CHAN_ITEM, G_SoundIndex("items/damage3.wav"), 1, ATTN_NORM);

    Grenade_Explode(self);
}

void DIE(tesla_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t point, mod_t mod)
{
    tesla_remove(self);
}

static void tesla_blow(edict_t *self)
{
    self->dmg *= TESLA_EXPLOSION_DAMAGE_MULT;
    self->dmg_radius = TESLA_EXPLOSION_RADIUS;
    tesla_remove(self);
}

static edict_t *tesla_find_beam(edict_t *self, edict_t *hit)
{
    for (int i = game.maxclients + BODY_QUEUE_SIZE; i < level.num_edicts; i++) {
        edict_t *ent = &g_edicts[i];
        if (ent->r.inuse && ent->r.ownernum == self->s.number && ent->enemy == hit)
            return ent;
    }

    return NULL;
}

void TOUCH(tesla_zap)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
}

void THINK(tesla_think_active)(edict_t *self)
{
    int      i, num;
    int      touch[MAX_EDICTS_OLD];
    edict_t *hit;
    vec3_t   dir, start;
    trace_t  tr;

    if (level.time >= self->air_finished) {
        tesla_remove(self);
        return;
    }

    VectorCopy(self->s.origin, start);
    start[2] += 16;

    num = trap_BoxEdicts(self->teamchain->r.absmin, self->teamchain->r.absmax, touch, q_countof(touch), AREA_SOLID);
    for (i = 0; i < num; i++) {
        // if the tesla died while zapping things, stop zapping.
        if (!self->r.inuse)
            break;

        hit = g_edicts + touch[i];
        if (!hit->r.inuse)
            continue;
        if (hit == self)
            continue;
        if (hit->health < 1)
            continue;

        // don't hit teammates
        if (hit->client) {
            if (!deathmatch.integer)
                continue;
            if (CheckTeamDamage(hit, &g_edicts[self->teamchain->r.ownernum]))
                continue;
        }

        if (!(hit->r.svflags & SVF_MONSTER) && !(hit->flags & FL_DAMAGEABLE) && !hit->client)
            continue;

        // don't hit other teslas in SP/coop
        if (!deathmatch.integer && hit->classname && (hit->r.svflags & SVF_TRAP))
            continue;

        trap_Trace(&tr, start, NULL, NULL, hit->s.origin, self->s.number, MASK_PROJECTILE);
        if (tr.fraction == 1 || tr.entnum == hit->s.number) {
            VectorSubtract(hit->s.origin, start, dir);

            // PMM - play quad sound if it's above the "normal" damage
            if (self->dmg > TESLA_DAMAGE)
                G_StartSound(self, CHAN_ITEM, G_SoundIndex("items/damage3.wav"), 1, ATTN_NORM);

            // PGM - don't do knockback to walking monsters
            if ((hit->r.svflags & SVF_MONSTER) && !(hit->flags & (FL_FLY | FL_SWIM)))
                T_Damage(hit, self, self->teammaster, dir, tr.endpos, tr.plane.dir,
                         self->dmg, 0, DAMAGE_NONE, (mod_t) { MOD_TESLA });
            else
                T_Damage(hit, self, self->teammaster, dir, tr.endpos, tr.plane.dir,
                         self->dmg, TESLA_KNOCKBACK, DAMAGE_NONE, (mod_t) { MOD_TESLA });

            edict_t *te = tesla_find_beam(self, hit);
            if (!te) {
                te = G_Spawn();
                te->s.renderfx = RF_BEAM;
                te->s.modelindex = G_ModelIndex("models/proj/lightning/tris.md2");
                te->s.sound = G_EncodeSound(CHAN_AUTO, G_SoundIndex("weapons/tesla.wav"), 1, ATTN_NORM);
                te->s.othernum = ENTITYNUM_NONE;
                te->r.ownernum = self->s.number;
                te->enemy = hit;
                te->think = G_FreeEdict;
            }

            G_SnapVector(start, te->s.old_origin);
            G_SnapVectorTowards(tr.endpos, start, te->s.origin);
            te->nextthink = level.time + SEC(0.2f);
            trap_LinkEntity(te);
        }
    }

    if (self->r.inuse) {
        self->think = tesla_think_active;
        self->nextthink = level.time + HZ(10);
    }
}

void THINK(tesla_activate)(edict_t *self)
{
    edict_t *trigger;
    edict_t *search;

    if (trap_PointContents(self->s.origin) & (CONTENTS_SLIME | CONTENTS_LAVA | CONTENTS_WATER)) {
        tesla_blow(self);
        return;
    }

    // only check for spawn points in deathmatch
    if (deathmatch.integer) {
        search = NULL;
        while ((search = findradius(search, self->s.origin, 1.5f * TESLA_DAMAGE_RADIUS)) != NULL) {
            if (search == self)
                continue;
            if (!search->classname)
                continue;
            // [Paril-KEX] don't allow traps to be placed near flags or teleporters
            // if it's a monster or player with health > 0
            // or it's a player start point
            // and we can see it
            // blow up
            if (!player_start_point(search))
                continue;
            if (!visible(search, self))
                continue;
            BecomeExplosion1(self);
            return;
        }
    }

    trigger = G_Spawn();
    VectorCopy(self->s.origin, trigger->s.origin);
    VectorSet(trigger->r.mins, -TESLA_DAMAGE_RADIUS, -TESLA_DAMAGE_RADIUS, self->r.mins[2]);
    VectorSet(trigger->r.maxs, TESLA_DAMAGE_RADIUS, TESLA_DAMAGE_RADIUS, TESLA_DAMAGE_RADIUS);
    trigger->movetype = MOVETYPE_NONE;
    trigger->r.solid = SOLID_TRIGGER;
    trigger->r.ownernum = self->s.number;
    trigger->touch = tesla_zap;
    trigger->classname = "tesla trigger";
    // doesn't need to be marked as a teamslave since the move code for bounce looks for teamchains
    trap_LinkEntity(trigger);

    VectorClear(self->s.angles);
    // clear the owner if in deathmatch
    if (deathmatch.integer)
        self->r.ownernum = ENTITYNUM_NONE;
    self->teamchain = trigger;
    self->think = tesla_think_active;
    self->nextthink = level.time + FRAME_TIME;
    self->air_finished = level.time + TESLA_TIME_TO_LIVE;
}

void THINK(tesla_think)(edict_t *ent)
{
    if (trap_PointContents(ent->s.origin) & (CONTENTS_SLIME | CONTENTS_LAVA)) {
        tesla_remove(ent);
        return;
    }

    VectorClear(ent->s.angles);

    if (ent->s.frame == 0)
        G_StartSound(ent, CHAN_VOICE, G_SoundIndex("weapons/teslaopen.wav"), 1, ATTN_NORM);

    ent->s.frame++;
    if (ent->s.frame > 14) {
        ent->s.frame = 14;
        ent->think = tesla_activate;
        ent->nextthink = level.time + HZ(10);
    } else {
        if (ent->s.frame > 9) {
            if (ent->s.frame == 10) {
                edict_t *owner = &g_edicts[ent->r.ownernum];
                if (owner->client)
                    PlayerNoise(owner, ent->s.origin, PNOISE_WEAPON); // PGM
                ent->s.skinnum = 1;
            } else if (ent->s.frame == 12)
                ent->s.skinnum = 2;
            else if (ent->s.frame == 14)
                ent->s.skinnum = 3;
        }
        ent->think = tesla_think;
        ent->nextthink = level.time + HZ(10);
    }
}

void TOUCH(tesla_lava)(edict_t *ent, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    if (tr->contents & (CONTENTS_SLIME | CONTENTS_LAVA)) {
        tesla_blow(ent);
        return;
    }

    if (!VectorEmpty(ent->velocity)) {
        if (brandom())
            G_StartSound(ent, CHAN_VOICE, G_SoundIndex("weapons/hgrenb1a.wav"), 1, ATTN_NORM);
        else
            G_StartSound(ent, CHAN_VOICE, G_SoundIndex("weapons/hgrenb2a.wav"), 1, ATTN_NORM);
    }
}

void fire_tesla(edict_t *self, const vec3_t start, const vec3_t aimdir, int tesla_damage_multiplier, int speed)
{
    edict_t *tesla;
    vec3_t   dir;
    vec3_t   forward, right, up;

    vectoangles(aimdir, dir);
    AngleVectors(dir, forward, right, up);

    tesla = G_Spawn();
    VectorCopy(start, tesla->s.origin);
    VectorScale(aimdir, speed, tesla->velocity);

    float scale = (200 + crandom() * 10.0f) * (level.gravity / 800.0f);
    VectorMA(tesla->velocity, scale, up, tesla->velocity);

    scale = crandom() * 10.0f;
    VectorMA(tesla->velocity, scale, right, tesla->velocity);

    VectorClear(tesla->s.angles);
    tesla->movetype = MOVETYPE_BOUNCE;
    tesla->r.solid = SOLID_BBOX;
    tesla->s.effects |= EF_GRENADE;
    tesla->s.renderfx |= RF_IR_VISIBLE;
    VectorSet(tesla->r.mins, -12, -12, 0);
    VectorSet(tesla->r.maxs, 12, 12, 20);
    tesla->s.modelindex = G_ModelIndex("models/weapons/g_tesla/tris.md2");
    tesla->r.ownernum = self->s.number; // PGM - we don't want it owned by self YET.
    tesla->teammaster = self;
    tesla->think = tesla_think;
    tesla->nextthink = level.time + TESLA_ACTIVATE_TIME;

    // blow up on contact with lava & slime code
    tesla->touch = tesla_lava;

    if (deathmatch.integer)
        // PMM - lowered from 50 - 7/29/1998
        tesla->health = 20;
    else
        tesla->health = 50; // FIXME - change depending on skill?

    tesla->takedamage = true;
    tesla->die = tesla_die;
    tesla->dmg = TESLA_DAMAGE * tesla_damage_multiplier;
    tesla->classname = "tesla_mine";
    tesla->flags |= (FL_DAMAGEABLE | FL_MECHANICAL);
    tesla->clipmask = (MASK_PROJECTILE | CONTENTS_SLIME | CONTENTS_LAVA) & ~CONTENTS_DEADMONSTER;
    tesla->r.svflags |= SVF_TRAP;

    // [Paril-KEX]
    if (self->client && !G_ShouldPlayersCollide(true))
        tesla->clipmask &= ~CONTENTS_PLAYER;

    trap_LinkEntity(tesla);
}

// *************************
//  HEATBEAM
// *************************

void THINK(heatbeam_think)(edict_t *self)
{
    g_edicts[self->r.ownernum].beam = NULL;
    G_FreeEdict(self);
}

/*
=================
fire_heat

Fires a single heat beam.  Zap.
=================
*/
void fire_heatbeam(edict_t *self, const vec3_t start, const vec3_t aimdir, const vec3_t offset, int damage, int kick, bool monster)
{
    trace_t    tr;
    vec3_t     dir;
    vec3_t     forward, right, up;
    vec3_t     end, pos;
    vec3_t     water_start, endpoint;
    bool       water = false, underwater = false;
    contents_t content_mask = MASK_PROJECTILE | MASK_WATER;

    // [Paril-KEX]
    if (self->client && !G_ShouldPlayersCollide(true))
        content_mask &= ~CONTENTS_PLAYER;

    vectoangles(aimdir, dir);
    AngleVectors(dir, forward, right, up);

    VectorMA(start, 8192, forward, end);

    if (trap_PointContents(start) & MASK_WATER) {
        underwater = true;
        VectorCopy(start, water_start);
        content_mask &= ~MASK_WATER;
    }

    trap_Trace(&tr, start, NULL, NULL, end, self->s.number, content_mask);

    // see if we hit water
    if (tr.contents & MASK_WATER) {
        water = true;
        VectorCopy(tr.endpos, water_start);

        if (!VectorCompare(start, tr.endpos)) {
            G_SnapVectorTowards(water_start, start, pos);
            G_TempEntity(pos, EV_HEATBEAM_STEAM, tr.plane.dir);
        }

        // re-trace ignoring water this time
        trap_Trace(&tr, water_start, NULL, NULL, end, self->s.number, content_mask & ~MASK_WATER);
    }
    G_SnapVectorTowards(tr.endpos, start, endpoint);

    // halve the damage if target underwater
    if (water)
        damage = damage / 2;

    edict_t *hit = &g_edicts[tr.entnum];

    // send gun puff / flash
    if ((tr.fraction < 1.0f) && !(tr.surface_flags & SURF_SKY)) {
        if (hit->takedamage) {
            T_Damage(hit, self, self, aimdir, tr.endpos, tr.plane.dir, damage, kick, DAMAGE_ENERGY, (mod_t) { MOD_HEATBEAM });
        } else if (!water) {
            G_SnapVectorTowards(tr.endpos, start, pos);
            G_TempEntity(pos, EV_HEATBEAM_SPARKS, tr.plane.dir);

            if (self->client)
                PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
        }
    }

    // if went through water, determine where the end and make a bubble trail
    if ((water) || (underwater)) {
        VectorSubtract(tr.endpos, water_start, dir);
        VectorNormalize(dir);
        VectorMA(tr.endpos, -2, dir, pos);
        if (trap_PointContents(pos) & MASK_WATER)
            VectorCopy(pos, tr.endpos);
        else
            trap_Trace(&tr, pos, NULL, NULL, water_start, hit->s.number, MASK_WATER);

        G_SpawnTrail(water_start, tr.endpos, EV_BUBBLETRAIL2);
    }

    edict_t *te = self->beam;
    if (!te) {
        self->beam = te = G_Spawn();
        te->s.renderfx = RF_BEAM;
        te->s.modelindex = G_ModelIndex("models/proj/beam/tris.md2");
        te->s.othernum = self->s.number;
        te->r.ownernum = self->s.number;
        te->think = heatbeam_think;
    }

    G_SnapVector(start, te->s.old_origin);
    VectorCopy(endpoint, te->s.origin);
    te->nextthink = level.time + SEC(0.2f);
    trap_LinkEntity(te);
}

// *************************
//  BLASTER 2
// *************************

/*
=================
fire_blaster2

Fires a single green blaster bolt.  Used by monsters, generally.
=================
*/
void TOUCH(blaster2_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    edict_t *owner = &g_edicts[self->r.ownernum];
    mod_t    mod;
    int      damagestat;

    if (other == owner)
        return;

    if (tr->surface_flags & SURF_SKY) {
        G_FreeEdict(self);
        return;
    }

    if (owner->client)
        PlayerNoise(owner, self->s.origin, PNOISE_IMPACT);

    if (other->takedamage) {
        // the only time players will be firing blaster2 bolts will be from the
        // defender sphere.
        if (owner->client)
            mod = (mod_t) { MOD_DEFENDER_SPHERE };
        else
            mod = (mod_t) { MOD_BLASTER2 };

        damagestat = owner->takedamage;
        owner->takedamage = false;
        if (self->dmg >= 5)
            T_RadiusDamage(self, owner, self->dmg * 2, other, self->dmg_radius, DAMAGE_ENERGY, (mod_t) { MOD_UNKNOWN });
        T_Damage(other, self, owner, self->velocity, self->s.origin, tr->plane.dir, self->dmg, 1, DAMAGE_ENERGY, mod);
        owner->takedamage = damagestat;
        G_FreeEdict(self);
    } else {
        // PMM - yeowch this will get expensive
        if (self->dmg >= 5)
            T_RadiusDamage(self, owner, self->dmg * 2, owner, self->dmg_radius, DAMAGE_ENERGY, (mod_t) { MOD_UNKNOWN });
        G_BecomeEvent(self, EV_BLASTER2, tr->plane.dir);
    }
}

void fire_blaster2(edict_t *self, const vec3_t start, const vec3_t dir, int damage, int speed, effects_t effect, bool hyper)
{
    edict_t *bolt;

    bolt = G_SpawnMissile(self, start, dir, speed);
    bolt->flags |= FL_DODGE;
    bolt->s.effects |= effect;
    if (effect)
        bolt->s.effects |= EF_TRACKER;
    bolt->s.renderfx |= RF_NOSHADOW;
    bolt->s.modelindex = G_ModelIndex("models/objects/laser/tris.md2");
    bolt->s.skinnum = 2;
    bolt->s.scale = 2.5f;
    bolt->touch = blaster2_touch;
    bolt->nextthink = level.time + SEC(2);
    bolt->think = G_FreeEdict;
    bolt->dmg = damage;
    bolt->dmg_radius = 128;
    bolt->classname = "bolt";
    trap_LinkEntity(bolt);

    G_CheckMissileImpact(self, bolt);
}

// *************************
// tracker
// *************************

#define TRACKER_DAMAGE_FLAGS    (DAMAGE_NO_POWER_ARMOR | DAMAGE_ENERGY | DAMAGE_NO_KNOCKBACK)
#define TRACKER_IMPACT_FLAGS    (DAMAGE_NO_POWER_ARMOR | DAMAGE_ENERGY)
#define TRACKER_DAMAGE_TIME_SEC 0.5f
#define TRACKER_DAMAGE_TIME     SEC(TRACKER_DAMAGE_TIME_SEC)

void THINK(tracker_pain_daemon_think)(edict_t *self)
{
    if (!self->r.inuse)
        return;

    if ((level.time >= self->timestamp) || (self->enemy->health <= 0)) {
        if (!self->enemy->client)
            self->enemy->s.effects &= ~EF_TRACKERTRAIL;
        G_FreeEdict(self);
        return;
    }

    vec3_t center;
    VectorAvg(self->enemy->r.absmin, self->enemy->r.absmax, center);

    edict_t *owner = &g_edicts[self->r.ownernum];
    T_Damage(self->enemy, self, owner, vec3_origin, center, DIRTOBYTE_UP,
             self->dmg, 0, TRACKER_DAMAGE_FLAGS, (mod_t) { MOD_TRACKER });

    // if we kill the player, we'll be removed.
    if (self->r.inuse) {
        int hurt;

        // if we killed a monster, gib them.
        if (self->enemy->health < 1) {
            if (self->enemy->gib_health)
                hurt = -self->enemy->gib_health;
            else
                hurt = 500;

            T_Damage(self->enemy, self, owner, vec3_origin, center, DIRTOBYTE_UP,
                     hurt, 0, TRACKER_DAMAGE_FLAGS, (mod_t) { MOD_TRACKER });
        }

        self->nextthink = level.time + HZ(10);

        if (self->enemy->client)
            self->enemy->client->tracker_pain_time = self->nextthink;
        else
            self->enemy->s.effects |= EF_TRACKERTRAIL;
    }
}

static void tracker_pain_daemon_spawn(edict_t *owner, edict_t *enemy, int damage)
{
    edict_t *daemon;

    if (enemy == NULL)
        return;

    daemon = G_Spawn();
    daemon->classname = "pain daemon";
    daemon->think = tracker_pain_daemon_think;
    daemon->nextthink = level.time;
    daemon->timestamp = level.time + TRACKER_DAMAGE_TIME;
    daemon->r.ownernum = owner->s.number;
    daemon->enemy = enemy;
    daemon->dmg = damage;
}

static void tracker_explode(edict_t *self)
{
    G_BecomeEvent(self, EV_TRACKER_EXPLOSION, 0);
}

void TOUCH(tracker_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
{
    edict_t *owner = &g_edicts[self->r.ownernum];
    float    damagetime;

    if (other == owner)
        return;

    if (tr->surface_flags & SURF_SKY) {
        G_FreeEdict(self);
        return;
    }

    if (owner->client)
        PlayerNoise(owner, self->s.origin, PNOISE_IMPACT);

    if (other->takedamage) {
        if ((other->r.svflags & SVF_MONSTER) || other->client) {
            if (other->health > 0) { // knockback only for living creatures
                // PMM - kickback was times 4 .. reduced to 3
                // now this does no damage, just knockback
                T_Damage(other, self, owner, self->velocity, self->s.origin, tr->plane.dir,
                         0, self->dmg * 3, TRACKER_IMPACT_FLAGS, (mod_t) { MOD_TRACKER });

                if (!(other->flags & (FL_FLY | FL_SWIM)))
                    other->velocity[2] += 140;

                damagetime = (self->dmg * 0.1f) / TRACKER_DAMAGE_TIME_SEC;
                tracker_pain_daemon_spawn(owner, other, damagetime);
            } else { // lots of damage (almost autogib) for dead bodies
                T_Damage(other, self, owner, self->velocity, self->s.origin, tr->plane.dir,
                         self->dmg * 4, self->dmg * 3, TRACKER_IMPACT_FLAGS, (mod_t) { MOD_TRACKER });
            }
        } else { // full damage in one shot for inanimate objects
            T_Damage(other, self, owner, self->velocity, self->s.origin, tr->plane.dir,
                     self->dmg, self->dmg * 3, TRACKER_IMPACT_FLAGS, (mod_t) { MOD_TRACKER });
        }
    }

    tracker_explode(self);
    return;
}

void THINK(tracker_fly)(edict_t *self)
{
    vec3_t dest;
    vec3_t dir;

    if ((!self->enemy) || (!self->enemy->r.inuse) || (self->enemy->health < 1)) {
        tracker_explode(self);
        return;
    }

    // PMM - try to hunt for center of enemy, if possible and not client
    if (self->enemy->client) {
        VectorCopy(self->enemy->s.origin, dest);
        dest[2] += self->enemy->viewheight;
    } else if (!self->r.linked) { // paranoia
        VectorCopy(self->enemy->s.origin, dest);
    } else {
        VectorAvg(self->enemy->r.absmin, self->enemy->r.absmax, dest);
    }

    VectorSubtract(dest, self->s.origin, dir);
    VectorNormalize(dir);
    vectoangles(dir, self->s.angles);
    VectorScale(dir, self->speed, self->velocity);
    VectorCopy(dest, self->monsterinfo.saved_goal);

    self->nextthink = level.time + HZ(10);
}

void fire_tracker(edict_t *self, const vec3_t start, const vec3_t dir, int damage, int speed, edict_t *enemy)
{
    edict_t *bolt;

    bolt = G_SpawnMissile(self, start, dir, speed);
    bolt->speed = speed;
    bolt->s.effects = EF_TRACKER;
    bolt->s.sound = G_SoundIndex("weapons/disrupt.wav");
    bolt->s.modelindex = G_ModelIndex("models/proj/disintegrator/tris.md2");
    bolt->touch = tracker_touch;
    bolt->enemy = enemy;
    bolt->dmg = damage;
    bolt->classname = "tracker";
    trap_LinkEntity(bolt);

    if (enemy) {
        bolt->nextthink = level.time + HZ(10);
        bolt->think = tracker_fly;
    } else {
        bolt->nextthink = level.time + SEC(10);
        bolt->think = G_FreeEdict;
    }

    G_CheckMissileImpact(self, bolt);
}
