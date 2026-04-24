// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_misc.c

#include "g_local.h"

/*
========================
fire_flechette
========================
*/
void TOUCH(nails_touch)(edict_t *self, edict_t *other, const trace_t *tr, bool other_touching_self)
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
                 self->dmg, self->dmg_radius, DAMAGE_NO_REG_ARMOR, MOD_ETF_RIFLE);
        G_FreeEdict(self);
    } else {
        G_BecomeEvent(self, EV_NAILS, tr->plane.dir);
    }
}

void fire_nails(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, int kick)
{
    edict_t *bolt;

    bolt = G_SpawnMissile(self, start, dir, speed);
    bolt->flags |= FL_DODGE;
    bolt->s.renderfx |= RF_FULLBRIGHT;
    bolt->s.modelindex = G_ModelIndex("models/objects/spike/tris.md2");
    bolt->touch = nails_touch;
    bolt->nextthink = level.time + SEC(8000.0f / speed);
    bolt->think = G_FreeEdict;
    bolt->dmg = damage;
    bolt->dmg_radius = kick;
    trap_LinkEntity(bolt);

    G_CheckMissileImpact(self, bolt);
}

/*QUAKED misc_chtondead (1 .5 0) (-176 -120 -24) (176 120 72)
Just the chthon model dead on the ground
*/
void SP_misc_chtondead(edict_t *ent)
{
    ent->movetype = MOVETYPE_NONE;
    ent->r.solid = SOLID_BBOX;
    ent->r.box.mins = Vec3(-176, -120, -24);
    ent->r.box.maxs = Vec3(176, 120, 72);
    ent->s.modelindex = G_ModelIndex("models/props/chtondead/tris.md2");
    trap_LinkEntity(ent);
}

static vec3_t event_lighting_pierce(edict_t *self)
{
    pierce_t pierce;
    trace_t tr;
    edict_t *hit;

    trace_args_t args = {
        .start = self->pos1,
        .end = self->pos2,
        .entnum = self->s.number,
        .mask = MASK_SHOT
    };

    pierce_begin(&pierce);

    do {
        trap_Trace(&tr, &args);

        // didn't hit anything, so we're done
        if (tr.fraction == 1.0f)
            break;

        hit = &g_edicts[tr.entnum];

        // hurt it if we can
        if (!strcmp(hit->classname, "monster_chthon")) {
            hit->takedamage = true;
            T_Damage(hit, self, self->activator, self->movedir, tr.endpos, 0, self->dmg, 1, DAMAGE_ENERGY, MOD_TARGET_LASER);
            hit->takedamage = false;
        }

        // if we hit something that's not a monster or player or is immune to lasers, we're done
        if (!(hit->r.svflags & SVF_MONSTER) && (!hit->client) && !(hit->flags & FL_DAMAGEABLE))
            break;
    } while (pierce_mark(&pierce, hit));

    pierce_end(&pierce);

    return tr.endpos;
}

void THINK(event_lighting_think)(edict_t *self)
{
    if (level.time >= self->touch_debounce_time) {
        if (self->beam) {
            G_FreeEdict(self->beam);
            self->beam = NULL;
        }
        return;
    }

    vec3_t end = event_lighting_pierce(self);

    if (!self->beam)
        self->beam = G_SpawnLightning(self);

    self->beam->s.old_origin = G_SnapVector(self->pos1);
    self->beam->s.origin = G_SnapVectorTowards(end, self->pos1);
    trap_LinkEntity(self->beam);

    self->nextthink = level.time + HZ(10);
}

void USE(use_event_lighting)(edict_t *self, edict_t *other, edict_t *activator)
{
    edict_t *a, *b = NULL;

    a = G_Find(NULL, FOFS(target), "lighting");
    if (a)
        b = G_Find(a, FOFS(target), "lighting");
    if (b) {
        self->pos1 = G_EntityCenter(a);
        self->pos2 = G_EntityCenter(b);
        //self->pos1.z = self->pos2.z = max(self->pos1.z, self->pos2.z);
    } else {
        self->pos1 = self->s.origin;
        self->pos2 = Vec3_MA(self->pos1, 8192, self->movedir);
    }

    self->activator = activator;

    if (self->delay > 0) {
        self->touch_debounce_time = level.time + SEC(self->delay);
        self->think = event_lighting_think;
        self->nextthink = level.time + HZ(10);
        return;
    }

    vec3_t end = event_lighting_pierce(self);
    edict_t *te = G_SpawnLightning(self);
    te->s.old_origin = G_SnapVector(self->pos1);
    te->s.origin = G_SnapVectorTowards(end, self->pos1);
    te->think = G_FreeEdict;
    te->nextthink = level.time + SEC(0.2f);
    trap_LinkEntity(te);
}

/*QUAKED event_lighting (1 .5 0) (-176 -120 -24) (176 120 72)
Sparks shoot out between two targeted points.
*/
void SP_event_lighting(edict_t *self)
{
    self->use = use_event_lighting;
    self->movetype = MOVETYPE_NONE;
    self->r.solid = SOLID_NOT;
    self->r.svflags |= SVF_NOCLIENT;

    G_SetMovedir(self);

    if (!self->dmg)
        self->dmg = 2;
    if (!self->s.scale)
        self->s.scale = 2;
}

void THINK(misc_fiend_craft_think)(edict_t *self)
{
    self->s.frame++;
    if (self->s.frame >= 60)
        self->s.frame = 0;
    self->nextthink = level.time + HZ(10);
}

void USE(misc_fiend_craft_use)(edict_t *self, edict_t* other, edict_t* activator)
{
    self->think = misc_fiend_craft_think;
    self->nextthink = level.time + HZ(10);
}

void SP_misc_fiend_craft(edict_t *ent)
{
    ent->r.box = Box3_FromSize(64, 0, 128);
    ent->s.modelindex = G_ModelIndex("models/props/fiend/tris.md2");
    if (ent->targetname) {
        ent->use = misc_fiend_craft_use;
    } else {
        ent->think = misc_fiend_craft_think;
        ent->nextthink = level.time + HZ(10);
    }
    trap_LinkEntity(ent);
}

/*QUAKED light_flame_small (1 0.5 0) (-4 -4 -8) (4 4 8)
*/
void THINK(light_flame_small_think)(edict_t *ent)
{
    ent->s.frame++;
    if (ent->s.frame >= 11)
        ent->s.frame = 0;
    ent->nextthink = level.time + HZ(10);
}

void SP_light_flame_small(edict_t *ent)
{
    ent->s.modelindex = G_ModelIndex("models/props/flame/tris.md2");
    ent->think = light_flame_small_think;
    ent->nextthink = level.time + HZ(10);
    trap_LinkEntity(ent);
}

/*QUAKED light_torch_small (1 0.5 0) (-4 -4 -8) (4 4 8)
*/
void THINK(light_torch_small_think)(edict_t *ent)
{
    ent->s.frame++;
    if (ent->s.frame >= 6)
        ent->s.frame = 0;
    ent->nextthink = level.time + HZ(10);
}

void SP_light_torch_small(edict_t *ent)
{
    ent->s.modelindex = G_ModelIndex("models/props/torch/tris.md2");
    ent->think = light_torch_small_think;
    ent->nextthink = level.time + HZ(10);
    trap_LinkEntity(ent);
}
