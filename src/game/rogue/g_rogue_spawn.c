// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"

//
// ROGUE
//

//
// Monster spawning code
//
// Used by the carrier, the medic_commander, and the black widow
//
// The sequence to create a flying monster is:
//
//  FindSpawnPoint - tries to find suitable spot to spawn the monster in
//  CreateFlyMonster  - this verifies the point as good and creates the monster

// To create a ground walking monster:
//
//  FindSpawnPoint - same thing
//  CreateGroundMonster - this checks the volume and makes sure the floor under the volume is suitable
//

// FIXME - for the black widow, if we want the stalkers coming in on the roof, we'll have to tweak some things

//
// CreateMonster
//
edict_t *CreateMonster(vec3_t origin, vec3_t angles, const char *classname)
{
    edict_t *newEnt;

    newEnt = G_Spawn();

    newEnt->s.origin = origin;
    newEnt->s.angles = angles;
    newEnt->classname = classname;
    newEnt->monsterinfo.aiflags |= AI_DO_NOT_COUNT;

    ED_InitSpawnVars();
    ED_CallSpawn(newEnt);
    newEnt->s.renderfx |= RF_IR_VISIBLE;

    return newEnt;
}

edict_t *CreateFlyMonster(vec3_t origin, vec3_t angles, box3_t box, const char *classname)
{
    if (!CheckSpawnPoint(origin, box))
        return NULL;

    return CreateMonster(origin, angles, classname);
}

// This is just a wrapper for CreateMonster that looks down height # of CMUs and sees if there
// are bad things down there or not

edict_t *CreateGroundMonster(vec3_t origin, vec3_t angles, box3_t box, const char *classname, float height)
{
    // check the ground to make sure it's there, it's relatively flat, and it's not toxic
    if (!CheckGroundSpawnPoint(origin, box, height, -1))
        return NULL;

    return CreateMonster(origin, angles, classname);
}

// FindSpawnPoint
// PMM - this is used by the medic commander (possibly by the carrier) to find a good spawn point
// if the startpoint is bad, try above the startpoint for a bit

bool FindSpawnPoint(vec3_t startpoint, box3_t box, vec3_t *spawnpoint, float maxMoveUp, bool drop)
{
    *spawnpoint = startpoint;

    // drop first
    if (drop && M_droptofloor_generic(spawnpoint, box, false, ENTITYNUM_NONE, MASK_MONSTERSOLID, false))
        return true;

    *spawnpoint = startpoint;

    // fix stuck if we couldn't drop initially
    if (PM_FixStuckObject_Generic(spawnpoint, box, ENTITYNUM_NONE, MASK_MONSTERSOLID, trap_Trace) == NO_GOOD_POSITION)
        return false;

    // fixed, so drop again
    if (drop && !M_droptofloor_generic(spawnpoint, box, false, ENTITYNUM_NONE, MASK_MONSTERSOLID, false))
        return false; // ???

    return true;
}

// FIXME - all of this needs to be tweaked to handle the new gravity rules
// if we ever want to spawn stuff on the roof

//
// CheckSpawnPoint
//
// PMM - checks volume to make sure we can spawn a monster there (is it solid?)
//
// This is all fliers should need

bool CheckSpawnPoint(vec3_t origin, box3_t box)
{
    if (Box3_IsEmpty(box))
        return false;

    trace_t tr = G_Trace(origin, origin, box, ENTITYNUM_NONE, MASK_MONSTERSOLID);
    if (tr.startsolid || tr.allsolid)
        return false;

    if (tr.entnum != ENTITYNUM_WORLD)
        return false;

    return true;
}

//
// CheckGroundSpawnPoint
//
// PMM - used for walking monsters
//  checks:
//      1)  is there a ground within the specified height of the origin?
//      2)  is the ground non-water?
//      3)  is the ground flat enough to walk on?
//

bool CheckGroundSpawnPoint(vec3_t origin, box3_t box, float height, float gravity)
{
    if (!CheckSpawnPoint(origin, box))
        return false;

    if (M_CheckBottom_Fast_Generic(Box3_Translate(box, origin), false))
        return true;

    if (M_CheckBottom_Slow_Generic(origin, box, ENTITYNUM_NONE, MASK_MONSTERSOLID, false, false))
        return true;

    return false;
}

// ****************************
// SPAWNGROW stuff
// ****************************

#define SPAWNGROW_LIFESPAN_SEC  1
#define SPAWNGROW_LIFESPAN      SEC(SPAWNGROW_LIFESPAN_SEC)

void THINK(spawngrow_think)(edict_t *self)
{
    if (level.time >= self->timestamp) {
        G_FreeEdict(self->target_ent);
        G_FreeEdict(self);
        return;
    }

    self->s.angles = Vec3_MA(self->s.angles, FRAME_TIME_SEC, self->avelocity);

    float t = 1.0f - TO_SEC(level.time - self->teleport_time) / self->wait;
    float s = Q_lerpf(self->decel, self->accel, t) / 16;

    self->s.scale = Q_clipf(s, 1.0f / 16, 16);
    self->s.alpha = t * t;

    self->nextthink += FRAME_TIME;
}

static vec3_t SpawnGro_laser_pos(edict_t *ent)
{
    float dist = g_edicts[ent->r.ownernum].s.scale * 9;
    return Vec3_MA(ent->s.origin, dist, Vec3_RandomDir());
}

void THINK(SpawnGro_laser_think)(edict_t *self)
{
    self->s.old_origin = SpawnGro_laser_pos(self);
    trap_LinkEntity(self);
    self->nextthink = level.time + FRAME_TIME;
}

void SpawnGrow_Spawn(vec3_t startpos, float start_size, float end_size)
{
    edict_t *ent;

    ent = G_Spawn();
    ent->s.origin = startpos;

    ent->s.angles.pitch = irandom1(360);
    ent->s.angles.yaw = irandom1(360);
    ent->s.angles.roll = irandom1(360);

    ent->avelocity.pitch = frandom2(280, 360) * 2;
    ent->avelocity.yaw = frandom2(280, 360) * 2;
    ent->avelocity.roll = frandom2(280, 360) * 2;

    ent->r.solid = SOLID_NOT;
    ent->s.renderfx |= RF_IR_VISIBLE;
    ent->movetype = MOVETYPE_NONE;
    ent->classname = "spawngro";

    ent->s.modelindex = G_ModelIndex("models/items/spawngro3/tris.md2");
    ent->s.skinnum = 1;

    ent->accel = start_size;
    ent->decel = end_size;
    ent->think = spawngrow_think;

    ent->s.scale = Q_clipf(start_size / 16, 1.0f / 16, 16);

    ent->teleport_time = level.time;
    ent->wait = SPAWNGROW_LIFESPAN_SEC;
    ent->timestamp = level.time + SPAWNGROW_LIFESPAN;

    ent->nextthink = level.time + FRAME_TIME;

    trap_LinkEntity(ent);

    // [Paril-KEX]
    edict_t *beam = ent->target_ent = G_Spawn();
    beam->s.modelindex = MODELINDEX_DUMMY;
    beam->s.renderfx = RF_BEAM_LIGHTNING | RF_FRAMELERP;
    beam->s.frame = 1;
    beam->s.skinnum = 0x30303030;
    beam->classname = "spawngro_beam";
    beam->angle = end_size;
    beam->r.ownernum = ent->s.number;
    beam->s.origin = ent->s.origin;
    beam->s.old_origin = SpawnGro_laser_pos(beam);
    beam->think = SpawnGro_laser_think;
    beam->nextthink = level.time + FRAME_TIME;
    trap_LinkEntity(beam);
}

// ****************************
// WidowLeg stuff
// ****************************

#define MAX_LEGSFRAME   23
#define LEG_WAIT_TIME   SEC(1)

void ThrowMoreStuff(edict_t *self, vec3_t point);
void ThrowSmallStuff(edict_t *self, vec3_t point);
void ThrowWidowGibLoc(edict_t *self, const char *gibname, int damage, gib_type_t type, vec3_t startpos, bool fade);
void ThrowWidowGibSized(edict_t *self, const char *gibname, int damage, gib_type_t type, vec3_t startpos, int hitsound, bool fade);

void THINK(widowlegs_think)(edict_t *self)
{
    vec3_t offset;
    vec3_t point;
    vec3_t f, r;

    if (self->s.frame == 17) {
        offset = Vec3(11.77f, -7.24f, 23.31f);
        AngleVectors(self->s.angles, &f, &r, NULL);
        point = M_ProjectFlashSource(self, offset, f, r);
        G_TempEntity(point, EV_EXPLOSION1, 0);
        ThrowSmallStuff(self, point);
    }

    if (self->s.frame < MAX_LEGSFRAME) {
        self->s.frame++;
        self->nextthink = level.time + HZ(10);
        return;
    }

    if (self->timestamp == 0)
        self->timestamp = level.time + LEG_WAIT_TIME;

    if (level.time > self->timestamp) {
        AngleVectors(self->s.angles, &f, &r, NULL);

        offset = Vec3(-65.6f, -8.44f, 28.59f);
        point = M_ProjectFlashSource(self, offset, f, r);
        G_TempEntity(point, EV_EXPLOSION1, 0);
        ThrowSmallStuff(self, point);

        ThrowWidowGibSized(self, "models/monsters/blackwidow/gib1/tris.md2", 80 + frandom1(20.0f), GIB_METALLIC, point, 0, true);
        ThrowWidowGibSized(self, "models/monsters/blackwidow/gib2/tris.md2", 80 + frandom1(20.0f), GIB_METALLIC, point, 0, true);

        offset = Vec3(-1.04f, -51.18f, 7.04f);
        point = M_ProjectFlashSource(self, offset, f, r);
        G_TempEntity(point, EV_EXPLOSION1, 0);
        ThrowSmallStuff(self, point);

        ThrowWidowGibSized(self, "models/monsters/blackwidow/gib1/tris.md2", 80 + frandom1(20.0f), GIB_METALLIC, point, 0, true);
        ThrowWidowGibSized(self, "models/monsters/blackwidow/gib2/tris.md2", 80 + frandom1(20.0f), GIB_METALLIC, point, 0, true);
        ThrowWidowGibSized(self, "models/monsters/blackwidow/gib3/tris.md2", 80 + frandom1(20.0f), GIB_METALLIC, point, 0, true);

        G_FreeEdict(self);
        return;
    }

    if ((level.time > self->timestamp - SEC(0.5f)) && (self->count == 0)) {
        self->count = 1;
        AngleVectors(self->s.angles, &f, &r, NULL);

        offset = Vec3(31, -88.7f, 10.96f);
        point = M_ProjectFlashSource(self, offset, f, r);
        G_TempEntity(point, EV_EXPLOSION1, 0);

        offset = Vec3(-12.67f, -4.39f, 15.68f);
        point = M_ProjectFlashSource(self, offset, f, r);
        G_TempEntity(point, EV_EXPLOSION1, 0);
    }

    self->nextthink = level.time + HZ(10);
}

void Widowlegs_Spawn(edict_t *self)
{
    edict_t *ent;

    ent = G_Spawn();
    ent->s.origin = self->s.origin;
    ent->s.angles = self->s.angles;
    ent->s.scale = self->s.scale;
    ent->s.alpha = self->s.alpha;
    ent->r.solid = SOLID_NOT;
    ent->s.renderfx = RF_IR_VISIBLE;
    ent->movetype = MOVETYPE_NONE;
    ent->classname = "widowlegs";

    ent->s.modelindex = G_ModelIndex("models/monsters/legs/tris.md2");
    ent->think = widowlegs_think;

    ent->nextthink = level.time + HZ(10);
    trap_LinkEntity(ent);
}
