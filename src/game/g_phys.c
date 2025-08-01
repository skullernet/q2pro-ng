// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_phys.c

#include "g_local.h"

/*


pushmove objects do not obey gravity, and do not interact with each other or trigger fields, but block normal movement
and push normal objects when they move.

onground is set for toss objects when they come to a complete rest.  it is set for steping or walking objects

doors, plats, etc are SOLID_BSP, and MOVETYPE_PUSH
bonus items are SOLID_TRIGGER touch, and MOVETYPE_TOSS
corpses are SOLID_NOT and MOVETYPE_TOSS
crates are SOLID_BBOX and MOVETYPE_TOSS
walking monsters are SOLID_SLIDEBOX and MOVETYPE_STEP
flying/floating monsters are SOLID_SLIDEBOX and MOVETYPE_FLY

solid_edge items only clip against bsp models.

*/

void SV_Physics_NewToss(edict_t *ent); // PGM

// [Paril-KEX] fetch the clipmask for this entity; certain modifiers
// affect the clipping behavior of objects.
contents_t G_GetClipMask(edict_t *ent)
{
    contents_t mask = ent->clipmask;

    // default masks
    if (!mask) {
        if (ent->r.svflags & SVF_MONSTER)
            mask = MASK_MONSTERSOLID;
        else if (ent->r.svflags & SVF_PROJECTILE)
            mask = MASK_PROJECTILE;
        else
            mask = MASK_SHOT & ~CONTENTS_DEADMONSTER;
    }

    // non-solid objects (items, etc) shouldn't try to clip
    // against players/monsters
    if (ent->r.solid == SOLID_NOT || ent->r.solid == SOLID_TRIGGER)
        mask &= ~(CONTENTS_MONSTER | CONTENTS_PLAYER);

    // monsters/players that are also dead shouldn't clip
    // against players/monsters
    if ((ent->r.svflags & (SVF_MONSTER | SVF_PLAYER)) && (ent->r.svflags & SVF_DEADMONSTER))
        mask &= ~(CONTENTS_MONSTER | CONTENTS_PLAYER);

    // remove special mask value
    mask &= ~CONTENTS_AREAPORTAL;

    return mask;
}

/*
============
SV_TestEntityPosition

============
*/
static bool SV_TestEntityPosition(edict_t *ent)
{
    trace_t     trace;

    trap_Trace(&trace, ent->s.origin, ent->r.mins, ent->r.maxs,
               ent->s.origin, ent->s.number, G_GetClipMask(ent));

    return trace.startsolid;
}

/*
================
SV_CheckVelocity
================
*/
void SV_CheckVelocity(edict_t *ent)
{
    //
    // bound velocity
    //
    float speed = VectorLength(ent->velocity);

    if (speed > sv_maxvelocity.value)
        VectorScale(ent->velocity, sv_maxvelocity.value / speed, ent->velocity);
}

/*
=============
SV_RunThink

Runs thinking code for this frame if necessary
=============
*/
bool SV_RunThink(edict_t *ent)
{
    gtime_t thinktime = ent->nextthink;
    if (thinktime <= 0)
        return true;
    if (thinktime > level.time)
        return true;

    ent->nextthink = 0;
    if (!ent->think)
        G_Error("NULL ent->think");
    ent->think(ent);

    return false;
}

/*
==================
G_Impact

Two entities have touched, so run their touch functions
==================
*/
void G_Impact(edict_t *e1, const trace_t *trace)
{
    edict_t *e2 = &g_edicts[trace->entnum];

    if (e1->touch && (e1->r.solid != SOLID_NOT || (e1->flags & FL_ALWAYS_TOUCH)))
        e1->touch(e1, e2, trace, false);

    if (e2->touch && (e2->r.solid != SOLID_NOT || (e2->flags & FL_ALWAYS_TOUCH)))
        e2->touch(e2, e1, trace, true);
}

/*
==================
G_ClipVelocity

Slide off of the impacting object
==================
*/
void G_ClipVelocity(const vec3_t in, const vec3_t normal, vec3_t out, float overbounce)
{
    float dot = DotProduct(in, normal);

    VectorMA(in, -2.0f * dot, normal, out);
    VectorScale(out, overbounce - 1.0f, out);

    if (VectorLengthSquared(out) < STOP_EPSILON * STOP_EPSILON)
        VectorClear(out);
}

/*
============
SV_FlyMove

The basic solid body movement clip that slides along multiple planes
============
*/
void SV_FlyMove(edict_t *ent, float time, contents_t mask)
{
    ent->groundentity = NULL;

    touch_list_t touch;
    touch.num = 0;
    PM_StepSlideMove_Generic(ent->s.origin, ent->velocity, time, ent->r.mins,
                             ent->r.maxs, ent->s.number, mask, &touch, false, trap_Trace);

    for (int i = 0; i < touch.num; i++) {
        trace_t *trace = &touch.traces[i];

        if (trace->plane.normal[2] > 0.7f) {
            ent->groundentity = &g_edicts[trace->entnum];
            ent->groundentity_linkcount = ent->groundentity->r.linkcount;
        }

        //
        // run the impact function
        //
        G_Impact(ent, trace);

        // impact func requested velocity kill
        if (ent->flags & FL_KILL_VELOCITY) {
            ent->flags &= ~FL_KILL_VELOCITY;
            VectorClear(ent->velocity);
        }
    }
}

/*
============
SV_AddGravity

============
*/
void SV_AddGravity(edict_t *ent)
{
    if (ent->no_gravity_time > level.time)
        return;

    float gravity = ent->gravity * level.gravity * FRAME_TIME_SEC;
    VectorMA(ent->velocity, gravity, ent->gravityVector, ent->velocity);
}

/*
===============================================================================

PUSHMOVE

===============================================================================
*/

/*
============
SV_PushEntity

Does not change the entities velocity at all
============
*/
static void SV_PushEntity(edict_t *ent, const vec3_t push, trace_t *trace)
{
    vec3_t start, end;
    VectorCopy(ent->s.origin, start);
    VectorAdd(start, push, end);

    while (true) {
        trap_Trace(trace, start, ent->r.mins, ent->r.maxs, end, ent->s.number, G_GetClipMask(ent));

        VectorMA(trace->endpos, 0.5f, trace->plane.normal, ent->s.origin);
        trap_LinkEntity(ent);

        if (trace->fraction == 1.0f && !trace->startsolid)
            break;

        G_Impact(ent, trace);

        if (!ent->r.inuse)
            return;

        if (g_edicts[trace->entnum].r.inuse)
            break;

        // if the pushed entity went away and the pusher is still there
        // move the pusher back and try again
        VectorCopy(start, ent->s.origin);
        trap_LinkEntity(ent);
    }

    // ================
    // PGM
    // FIXME - is this needed?
    ent->gravity = 1.0f;
    // PGM
    // ================

    G_TouchTriggers(ent);
}

typedef struct {
    edict_t *ent;
    vec3_t   origin;
    vec3_t   angles;
    //int    deltayaw;
} pushed_t;

static pushed_t pushed[MAX_EDICTS];
static int      num_pushed;

/*
============
SV_Push

Objects need to be moved back on a failed push,
otherwise riders would continue to slide.
============
*/
static edict_t *SV_Push(edict_t *pusher, const vec3_t move, const vec3_t amove)
{
    edict_t  *check;
    bool      block;
    vec3_t    mins, maxs;
    pushed_t *p;
    vec3_t    org, org2, move2, axis[3];

    // find the bounding box of entire move
    VectorCopy(pusher->r.absmin, mins);
    VectorCopy(pusher->r.absmax, maxs);
    for (int i = 0; i < 3; i++) {
        if (move[i] > 0)
            maxs[i] += move[i];
        else
            mins[i] += move[i];
    }

    // we need this for pushing things later
    AnglesToAxis(amove, axis);
    TransposeAxis(axis);

    // save the pusher's original position
    Q_assert(num_pushed < q_countof(pushed));
    p = &pushed[num_pushed++];
    p->ent = pusher;
    VectorCopy(pusher->s.origin, p->origin);
    VectorCopy(pusher->s.angles, p->angles);

    // move the pusher to it's final position
    VectorAdd(pusher->s.origin, move, pusher->s.origin);
    VectorAdd(pusher->s.angles, amove, pusher->s.angles);
    trap_LinkEntity(pusher);

    // no clip mask, so it won't move anything
    if (!G_GetClipMask(pusher))
        return NULL;

    int list[MAX_EDICTS_OLD];
    int count = trap_BoxEdicts(mins, maxs, list, q_countof(list), AREA_SOLID | AREA_TRIGGERS);

    // see if any solid entities are inside the final position
    for (int i = 0; i < count; i++) {
        check = g_edicts + list[i];
        if (!check->r.inuse)
            continue;
        if (check->movetype == MOVETYPE_PUSH || check->movetype == MOVETYPE_STOP ||
            check->movetype == MOVETYPE_NONE || check->movetype == MOVETYPE_NOCLIP)
            continue;

        // if the entity is standing on the pusher, it will definitely be moved
        if (check->groundentity != pusher) {
            // see if the ent needs to be tested
            if (!G_EntitiesContact(check, pusher))
                continue;
            // see if the ent's bbox is inside the pusher's final position
            if (!SV_TestEntityPosition(check))
                continue;
        }

        if ((pusher->movetype == MOVETYPE_PUSH) || (check->groundentity == pusher)) {
            // move this entity
            Q_assert(num_pushed < q_countof(pushed));
            p = &pushed[num_pushed++];
            p->ent = check;
            VectorCopy(check->s.origin, p->origin);
            VectorCopy(check->s.angles, p->angles);

            // try moving the contacted entity
            VectorAdd(check->s.origin, move, check->s.origin);
            if (check->client) {
                // Paril: disabled because in vanilla delta_angles are never
                // lerped. delta_angles can probably be lerped as long as event
                // isn't EV_PLAYER_TELEPORT or a new RDF flag is set
                // check->client->ps.delta_angles[YAW] += amove[YAW];
            } else
                check->s.angles[YAW] += amove[YAW];

            // figure movement due to the pusher's amove
            VectorSubtract(check->s.origin, pusher->s.origin, org);
            VectorRotate(org, axis, org2);
            VectorSubtract(org2, org, move2);
            VectorAdd(check->s.origin, move2, check->s.origin);

            // may have pushed them off an edge
            if (check->groundentity != pusher)
                check->groundentity = NULL;

            block = SV_TestEntityPosition(check);

            // [Paril-KEX] this is a bit of a hack; allow dead player skulls
            // to be a blocker because otherwise elevators/doors get stuck
            if (block && check->client && !check->takedamage) {
                VectorCopy(p->origin, check->s.origin);
                block = false;
            }

            if (!block) {
                // pushed ok
                trap_LinkEntity(check);
                // impact?
                continue;
            }

            // if it is ok to leave in the old position, do it.
            // this is only relevent for riding entities, not pushed
            VectorCopy(p->origin, check->s.origin);
            VectorCopy(p->angles, check->s.angles);
            block = SV_TestEntityPosition(check);
            if (!block) {
                num_pushed--;
                continue;
            }
        }

        // move back any entities we already moved
        // go backwards, so if the same entity was pushed
        // twice, it goes back to the original position
        for (int i = num_pushed - 1; i >= 0; i--) {
            p = &pushed[i];
            VectorCopy(p->origin, p->ent->s.origin);
            VectorCopy(p->angles, p->ent->s.angles);
            //if (p->ent->client)
            //  p->ent->client->ps.delta_angles[YAW] = p->deltayaw;
            trap_LinkEntity(p->ent);
        }

        // return the obstacle so we can call the block function
        return check;
    }

    // FIXME: is there a better way to handle this?
    // see if anything we moved has touched a trigger
    for (int i = num_pushed - 1; i >= 0; i--)
        G_TouchTriggers(pushed[i].ent);

    return NULL;
}

/*
================
SV_Physics_Pusher

Bmodel objects don't interact with each other, but
push all box objects
================
*/
static void SV_Physics_Pusher(edict_t *ent)
{
    vec3_t   move, amove;
    edict_t *part, *obstacle;

    // if not a team captain, so movement will be handled elsewhere
    if (ent->flags & FL_TEAMSLAVE)
        return;

    // make sure all team slaves can move before commiting
    // any moves or calling any think functions
    // if the move is blocked, all moved objects will be backed out
retry:
    obstacle = NULL;
    num_pushed = 0;
    for (part = ent; part; part = part->teamchain) {
        if (!VectorEmpty(part->velocity) || !VectorEmpty(part->avelocity)) {
            // object is moving
            VectorScale(part->velocity, FRAME_TIME_SEC, move);
            VectorScale(part->avelocity, FRAME_TIME_SEC, amove);

            obstacle = SV_Push(part, move, amove);
            if (obstacle)
                break; // move was blocked
        }
    }

    if (part) {
        // if the pusher has a "blocked" function, call it
        // otherwise, just stay in place until the obstacle is gone
        if (part->moveinfo.blocked)
            part->moveinfo.blocked(part, obstacle);

        if (!obstacle->r.inuse)
            goto retry;
    } else {
        // the move succeeded, so call all think functions
        for (part = ent; part; part = part->teamchain) {
            // prevent entities that are on trains that have gone away from thinking!
            if (part->r.inuse)
                SV_RunThink(part);
        }
    }
}

//==================================================================

/*
=============
SV_Physics_None

Non moving objects can only think
=============
*/
static void SV_Physics_None(edict_t *ent)
{
    // regular thinking
    SV_RunThink(ent);
}

/*
=============
SV_Physics_Noclip

A moving object that doesn't obey physics
=============
*/
static void SV_Physics_Noclip(edict_t *ent)
{
    // regular thinking
    if (!SV_RunThink(ent) || !ent->r.inuse)
        return;

    VectorMA(ent->s.angles, FRAME_TIME_SEC, ent->avelocity, ent->s.angles);
    VectorMA(ent->s.origin, FRAME_TIME_SEC, ent->velocity, ent->s.origin);

    trap_LinkEntity(ent);
}

/*
==============================================================================

TOSS / BOUNCE

==============================================================================
*/

/*
=============
SV_Physics_Toss

Toss, bounce, and fly movement.  When onground, do nothing.
=============
*/
static void SV_Physics_Toss(edict_t *ent)
{
    trace_t  trace;
    vec3_t   move;
    float    backoff;
    edict_t *slave;
    bool     wasinwater;
    bool     isinwater;
    vec3_t   old_origin;

    // regular thinking
    SV_RunThink(ent);

    if (!ent->r.inuse)
        return;

    // if not a team captain, so movement will be handled elsewhere
    if (ent->flags & FL_TEAMSLAVE)
        return;

    if (ent->velocity[2] > 0)
        ent->groundentity = NULL;

    // check for the groundentity going away
    if (ent->groundentity && !ent->groundentity->r.inuse)
        ent->groundentity = NULL;

    // if onground, return without moving
    if (ent->groundentity && ent->gravity > 0.0f) { // PGM - gravity hack
        if (ent->r.svflags & SVF_MONSTER) {
            M_CategorizePosition(ent, ent->s.origin, &ent->waterlevel, &ent->watertype);
            M_WorldEffects(ent);
        }
        return;
    }

    VectorCopy(ent->s.origin, old_origin);

    SV_CheckVelocity(ent);

    // add gravity
    if (ent->movetype != MOVETYPE_FLY &&
        ent->movetype != MOVETYPE_FLYMISSILE
        // RAFAEL
        // move type for rippergun projectile
        && ent->movetype != MOVETYPE_WALLBOUNCE
        // RAFAEL
       )
        SV_AddGravity(ent);

    // move angles
    VectorMA(ent->s.angles, FRAME_TIME_SEC, ent->avelocity, ent->s.angles);

    // move origin
    int num_tries = 5;
    float time_left = FRAME_TIME_SEC;

    while (time_left > 0) {
        if (num_tries == 0)
            break;

        num_tries--;
        VectorScale(ent->velocity, time_left, move);
        SV_PushEntity(ent, move, &trace);

        if (!ent->r.inuse)
            return;

        if (trace.fraction == 1.0f)
            break;

        edict_t *hit = &g_edicts[trace.entnum];

        // [Paril-KEX] don't build up velocity if we're stuck.
        // just assume that the object we hit is our ground.
        if (trace.allsolid) {
            ent->groundentity = hit;
            ent->groundentity_linkcount = hit->r.linkcount;
            VectorClear(ent->velocity);
            VectorClear(ent->avelocity);
            break;
        }

        time_left -= time_left * trace.fraction;

        if (ent->movetype == MOVETYPE_TOSS)
            PM_ClipVelocity(ent->velocity, trace.plane.normal, ent->velocity, 0.5f);
        else {
            // RAFAEL
            if (ent->movetype == MOVETYPE_WALLBOUNCE)
                backoff = 2.0f;
            // RAFAEL
            else
                backoff = 1.6f;

            G_ClipVelocity(ent->velocity, trace.plane.normal, ent->velocity, backoff);
        }

        // RAFAEL
        if (ent->movetype == MOVETYPE_WALLBOUNCE) {
            vectoangles(ent->velocity, ent->s.angles);
            break;
        }
        // RAFAEL

        // stop if on ground
        if (trace.plane.normal[2] > 0.7f) {
            if ((ent->movetype == MOVETYPE_TOSS && VectorLength(ent->velocity) < 60) ||
                (ent->movetype != MOVETYPE_TOSS && DotProduct(ent->velocity, trace.plane.normal) < 60)) {
                if (!(ent->flags & FL_NO_STANDING) || hit->r.solid == SOLID_BSP) {
                    ent->groundentity = hit;
                    ent->groundentity_linkcount = hit->r.linkcount;
                }
                VectorClear(ent->velocity);
                VectorClear(ent->avelocity);
                break;
            }

            // friction for tossing stuff (gibs, etc)
            if (ent->movetype == MOVETYPE_TOSS) {
                VectorScale(ent->velocity, 0.75f, ent->velocity);
                VectorScale(ent->avelocity, 0.75f, ent->avelocity);
            }
        }

        // only toss "slides" multiple times
        if (ent->movetype != MOVETYPE_TOSS)
            break;
    }

    // check for water transition
    wasinwater = (ent->watertype & MASK_WATER);
    ent->watertype = trap_PointContents(ent->s.origin);
    isinwater = ent->watertype & MASK_WATER;

    if (isinwater)
        ent->waterlevel = WATER_FEET;
    else
        ent->waterlevel = WATER_NONE;

    if (ent->r.svflags & SVF_MONSTER) {
        M_CategorizePosition(ent, ent->s.origin, &ent->waterlevel, &ent->watertype);
        M_WorldEffects(ent);
    } else {
        if (!wasinwater && isinwater)
            G_PositionedSound(old_origin, CHAN_AUTO, G_SoundIndex("misc/h2ohit1.wav"), 1, ATTN_NORM);
        else if (wasinwater && !isinwater)
            G_PositionedSound(ent->s.origin, CHAN_AUTO, G_SoundIndex("misc/h2ohit1.wav"), 1, ATTN_NORM);
    }

    // prevent softlocks from keys falling into slime/lava
    if (isinwater && ent->watertype & (CONTENTS_SLIME | CONTENTS_LAVA) && ent->item &&
        (ent->item->flags & IF_KEY) && (ent->spawnflags & SPAWNFLAG_ITEM_DROPPED)) {
        for (int i = 0; i < 3; i++)
            ent->velocity[i] = crandom_open() * 300;
        ent->velocity[2] += 300;
    }

    // move teamslaves
    for (slave = ent->teamchain; slave; slave = slave->teamchain) {
        VectorCopy(ent->s.origin, slave->s.origin);
        trap_LinkEntity(slave);
    }
}

/*
===============================================================================

STEPPING MOVEMENT

===============================================================================
*/

void SV_AddRotationalFriction(edict_t *ent)
{
    int   n;
    float adjustment;

    VectorMA(ent->s.angles, FRAME_TIME_SEC, ent->avelocity, ent->s.angles);
    adjustment = FRAME_TIME_SEC * sv_stopspeed.value * sv_friction; // PGM now a cvar

    for (n = 0; n < 3; n++) {
        if (ent->avelocity[n] > 0) {
            ent->avelocity[n] -= adjustment;
            if (ent->avelocity[n] < 0)
                ent->avelocity[n] = 0;
        } else {
            ent->avelocity[n] += adjustment;
            if (ent->avelocity[n] > 0)
                ent->avelocity[n] = 0;
        }
    }
}

/*
=============
SV_Physics_Step

Monsters freefall when they don't have a ground entity, otherwise
all movement is done with discrete steps.

This is also used for objects that have become still on the ground, but
will fall if the floor is pulled out from under them.
=============
*/
static void SV_Physics_Step(edict_t *ent)
{
    bool       wasonground;
    bool       hitsound = false;
    float     *vel;
    float      speed, newspeed, control;
    float      friction;
    edict_t   *groundentity;
    contents_t mask = G_GetClipMask(ent);

    // [Paril-KEX]
    if (ent->no_gravity_time > level.time)
        ent->groundentity = NULL;
    // airborne monsters should always check for ground
    else if (!ent->groundentity)
        M_CheckGround(ent, mask);

    groundentity = ent->groundentity;

    SV_CheckVelocity(ent);

    if (groundentity)
        wasonground = true;
    else
        wasonground = false;

    if (!VectorEmpty(ent->avelocity))
        SV_AddRotationalFriction(ent);

    // FIXME: figure out how or why this is happening
    if (isnan(ent->velocity[0]) || isnan(ent->velocity[1]) || isnan(ent->velocity[2]))
        VectorClear(ent->velocity);

    // add gravity except:
    //   flying monsters
    //   swimming monsters who are in the water
    if (!wasonground && !(ent->flags & FL_FLY) && !((ent->flags & FL_SWIM) && (ent->waterlevel > WATER_WAIST))) {
        if (ent->velocity[2] < level.gravity * -0.1f)
            hitsound = true;
        if (ent->waterlevel != WATER_UNDER)
            SV_AddGravity(ent);
    }

    // friction for flying monsters that have been given vertical velocity
    if ((ent->flags & FL_FLY) && (ent->velocity[2] != 0) && !(ent->monsterinfo.aiflags & AI_ALTERNATE_FLY)) {
        speed = fabsf(ent->velocity[2]);
        control = speed < sv_stopspeed.value ? sv_stopspeed.value : speed;
        friction = sv_friction / 3;
        newspeed = speed - (FRAME_TIME_SEC * control * friction);
        if (newspeed < 0)
            newspeed = 0;
        newspeed /= speed;
        ent->velocity[2] *= newspeed;
    }

    // friction for flying monsters that have been given vertical velocity
    if ((ent->flags & FL_SWIM) && (ent->velocity[2] != 0) && !(ent->monsterinfo.aiflags & AI_ALTERNATE_FLY)) {
        speed = fabsf(ent->velocity[2]);
        control = speed < sv_stopspeed.value ? sv_stopspeed.value : speed;
        newspeed = speed - (FRAME_TIME_SEC * control * sv_waterfriction * ent->waterlevel);
        if (newspeed < 0)
            newspeed = 0;
        newspeed /= speed;
        ent->velocity[2] *= newspeed;
    }

    if (!VectorEmpty(ent->velocity) || ent->no_gravity_time > level.time) {
        // apply friction
        if ((wasonground || (ent->flags & (FL_SWIM | FL_FLY))) && !(ent->monsterinfo.aiflags & AI_ALTERNATE_FLY)) {
            vel = ent->velocity;
            speed = sqrtf(vel[0] * vel[0] + vel[1] * vel[1]);
            if (speed) {
                friction = sv_friction;

                // Paril: lower friction for dead monsters
                if (ent->deadflag)
                    friction *= 0.5f;

                control = speed < sv_stopspeed.value ? sv_stopspeed.value : speed;
                newspeed = speed - FRAME_TIME_SEC * control * friction;

                if (newspeed < 0)
                    newspeed = 0;
                newspeed /= speed;

                vel[0] *= newspeed;
                vel[1] *= newspeed;
            }
        }

        vec3_t old_origin;
        VectorCopy(ent->s.origin, old_origin);

        SV_FlyMove(ent, FRAME_TIME_SEC, mask);

        G_TouchProjectiles(ent, old_origin);

        M_CheckGround(ent, mask);

        trap_LinkEntity(ent);

        // ========
        // PGM - reset this every time they move.
        //       G_touchtriggers will set it back if appropriate
        ent->gravity = 1.0f;
        // ========

        // [Paril-KEX] this is something N64 does to avoid doors opening
        // at the start of a level, which triggers some monsters to spawn.
        if (!level.is_n64 || level.time > FRAME_TIME)
            G_TouchTriggers(ent);

        if (!ent->r.inuse)
            return;

        if (ent->groundentity && !wasonground && hitsound)
            G_AddEvent(ent, EV_FOOTSTEP, 0);
    }

    if (!ent->r.inuse) // PGM g_touchtrigger free problem
        return;

    if (ent->r.svflags & SVF_MONSTER) {
        M_CategorizePosition(ent, ent->s.origin, &ent->waterlevel, &ent->watertype);
        M_WorldEffects(ent);

        // [Paril-KEX] last minute hack to fix Stalker upside down gravity
        if (wasonground != !!ent->groundentity) {
            if (ent->monsterinfo.physics_change)
                ent->monsterinfo.physics_change(ent);
        }
    }

    // regular thinking
    SV_RunThink(ent);
}

// [Paril-KEX]
static void G_RunBmodelAnimation(edict_t *ent)
{
    bmodel_anim_t *anim = &ent->bmodel_anim;

    if (anim->currently_alternate != anim->alternate) {
        anim->currently_alternate = anim->alternate;
        anim->next_tick = 0;
    }

    if (level.time < anim->next_tick)
        return;

    const bmodel_anim_param_t *p = &anim->params[anim->alternate];

    switch (p->style) {
    case BMODEL_ANIM_FORWARDS:
        if (p->end >= p->start)
            ent->s.frame++;
        else
            ent->s.frame--;
        break;
    case BMODEL_ANIM_BACKWARDS:
        if (p->end >= p->start)
            ent->s.frame--;
        else
            ent->s.frame++;
        break;
    case BMODEL_ANIM_RANDOM:
        if (p->end >= p->start)
            ent->s.frame = irandom2(p->start, p->end + 1);
        else
            ent->s.frame = irandom2(p->end, p->start + 1);
        break;
    }

    if (p->nowrap) {
        if (p->end >= p->start)
            ent->s.frame = Q_clip(ent->s.frame, p->start, p->end);
        else
            ent->s.frame = Q_clip(ent->s.frame, p->end, p->start);
    } else {
        if (ent->s.frame < p->start)
            ent->s.frame = p->end;
        else if (ent->s.frame > p->end)
            ent->s.frame = p->start;
    }

    anim->next_tick = level.time + MSEC(p->speed);
}

//============================================================================

/*
================
G_RunEntity

================
*/
void G_RunEntity(edict_t *ent)
{
    // PGM
    trace_t trace;
    vec3_t  previous_origin;
    bool    has_previous_origin = false;

    if (ent->movetype == MOVETYPE_STEP) {
        VectorCopy(ent->s.origin, previous_origin);
        has_previous_origin = true;
    }
    // PGM

    if (ent->prethink)
        ent->prethink(ent);

    // bmodel animation stuff runs first, so custom entities
    // can override them
    if (ent->bmodel_anim.enabled)
        G_RunBmodelAnimation(ent);

    switch (ent->movetype) {
    case MOVETYPE_PUSH:
    case MOVETYPE_STOP:
        SV_Physics_Pusher(ent);
        break;
    case MOVETYPE_NONE:
        SV_Physics_None(ent);
        break;
    case MOVETYPE_NOCLIP:
        SV_Physics_Noclip(ent);
        break;
    case MOVETYPE_STEP:
        SV_Physics_Step(ent);
        break;
    case MOVETYPE_TOSS:
    case MOVETYPE_BOUNCE:
    case MOVETYPE_FLY:
    case MOVETYPE_FLYMISSILE:
    // RAFAEL
    case MOVETYPE_WALLBOUNCE:
    // RAFAEL
        SV_Physics_Toss(ent);
        break;
    // ROGUE
    case MOVETYPE_NEWTOSS:
        SV_Physics_NewToss(ent);
        break;
    // ROGUE
    default:
        G_Error("SV_Physics: bad movetype %d", ent->movetype);
    }

    // PGM
    if (has_previous_origin && ent->movetype == MOVETYPE_STEP) {
        // if we moved, check and fix origin if needed
        if (!VectorCompare(ent->s.origin, previous_origin)) {
            trap_Trace(&trace, ent->s.origin, ent->r.mins, ent->r.maxs,
                       previous_origin, ent->s.number, G_GetClipMask(ent));
            if (trace.allsolid || trace.startsolid)
                VectorCopy(previous_origin, ent->s.origin);
        }
    }
    // PGM

    if (ent->postthink)
        ent->postthink(ent);
}
