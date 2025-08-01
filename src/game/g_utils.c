// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_utils.c -- misc utility functions for game module

#include "g_local.h"

static byte g_mem_pool[0x100000];
static int  g_mem_used;

void G_ProjectSource(const vec3_t point, const vec3_t distance, const vec3_t forward, const vec3_t right, vec3_t result)
{
    result[0] = point[0] + forward[0] * distance[0] + right[0] * distance[1];
    result[1] = point[1] + forward[1] * distance[0] + right[1] * distance[1];
    result[2] = point[2] + forward[2] * distance[0] + right[2] * distance[1] + distance[2];
}

void G_ProjectSource2(const vec3_t point, const vec3_t distance, const vec3_t forward, const vec3_t right, const vec3_t up, vec3_t result)
{
    result[0] = point[0] + forward[0] * distance[0] + right[0] * distance[1] + up[0] * distance[2];
    result[1] = point[1] + forward[1] * distance[0] + right[1] * distance[1] + up[1] * distance[2];
    result[2] = point[2] + forward[2] * distance[0] + right[2] * distance[1] + up[2] * distance[2];
}

void closest_point_to_box(const vec3_t from, const vec3_t mins, const vec3_t maxs, vec3_t point)
{
    point[0] = Q_clipf(from[0], mins[0], maxs[0]);
    point[1] = Q_clipf(from[1], mins[1], maxs[1]);
    point[2] = Q_clipf(from[2], mins[2], maxs[2]);
}

float distance_between_boxes(const vec3_t mins1, const vec3_t maxs1, const vec3_t mins2, const vec3_t maxs2)
{
    float len = 0;

    for (int i = 0; i < 3; i++) {
        if (maxs1[i] < mins2[i]) {
            float d = maxs1[i] - mins2[i];
            len += d * d;
        } else if (mins1[i] > maxs2[i]) {
            float d = mins1[i] - maxs2[i];
            len += d * d;
        }
    }

    return sqrtf(len);
}

bool boxes_intersect(const vec3_t mins1, const vec3_t maxs1, const vec3_t mins2, const vec3_t maxs2)
{
    for (int i = 0; i < 3; i++) {
        if (mins1[i] > maxs2[i])
            return false;
        if (maxs1[i] < mins2[i])
            return false;
    }

    return true;
}

bool G_EntitiesContact(const edict_t *a, const edict_t *b)
{
    return boxes_intersect(a->r.absmin, a->r.absmax, b->r.absmin, b->r.absmax);
}

/*
=============
G_Find

Searches all active entities for the next one that validates the given callback.

Searches beginning at the edict after from, or the beginning if NULL
NULL will be returned if the end of the list is reached.
=============
*/
edict_t *G_Find(edict_t *from, int fieldofs, const char *match)
{
    char    *s;

    if (!from)
        from = g_edicts;
    else
        from++;

    for (; from < &g_edicts[level.num_edicts]; from++) {
        if (!from->r.inuse)
            continue;
        s = *(char **)((byte *)from + fieldofs);
        if (!s)
            continue;
        if (!Q_stricmp(s, match))
            return from;
    }

    return NULL;
}

/*
=================
findradius

Returns entities that have origins within a spherical area

findradius (origin, radius)
=================
*/
edict_t *findradius(edict_t *from, const vec3_t org, float rad)
{
    vec3_t eorg;
    vec3_t mid;

    if (!from)
        from = g_edicts;
    else
        from++;
    for (; from < &g_edicts[level.num_edicts]; from++) {
        if (!from->r.inuse)
            continue;
        if (from->r.solid == SOLID_NOT)
            continue;
        VectorAvg(from->r.mins, from->r.maxs, mid);
        VectorAdd(from->s.origin, mid, eorg);
        if (Distance(eorg, org) > rad)
            continue;
        return from;
    }

    return NULL;
}

/*
=============
G_PickTarget

Searches all active entities for the next one that holds
the matching string at fieldofs in the structure.

Searches beginning at the edict after from, or the beginning if NULL
NULL will be returned if the end of the list is reached.

=============
*/
#define MAXCHOICES  8

edict_t *G_PickTarget(const char *targetname)
{
    edict_t *ent = NULL;
    int      num_choices = 0;
    edict_t *choice[MAXCHOICES];

    if (!targetname) {
        G_Printf("G_PickTarget called with NULL targetname\n");
        return NULL;
    }

    while (1) {
        ent = G_Find(ent, FOFS(targetname), targetname);
        if (!ent)
            break;
        choice[num_choices++] = ent;
        if (num_choices == MAXCHOICES)
            break;
    }

    if (!num_choices) {
        G_Printf("G_PickTarget: target %s not found\n", targetname);
        return NULL;
    }

    return choice[irandom1(num_choices)];
}

void THINK(Think_Delay)(edict_t *ent)
{
    G_UseTargets(ent, ent->activator);
    G_FreeEdict(ent);
}

void G_PrintActivationMessage(edict_t *ent, edict_t *activator, bool coop_global)
{
    //
    // print the message
    //
    if ((ent->message) && !(activator->r.svflags & SVF_MONSTER)) {
        if (coop_global && coop.integer)
            G_ClientPrintf(NULL, PRINT_CENTER, "%s", ent->message);
        else
            G_ClientPrintf(activator, PRINT_CENTER, "%s", ent->message);

        // [Paril-KEX] allow non-noisy centerprints
        if (ent->noise_index >= 0) {
            if (ent->noise_index)
                G_StartSound(activator, CHAN_AUTO, ent->noise_index, 1, ATTN_NORM);
            else
                G_StartSound(activator, CHAN_AUTO, G_SoundIndex("misc/talk1.wav"), 1, ATTN_NORM);
        }
    }
}

/*
==============================
G_UseTargets

the global "activator" should be set to the entity that initiated the firing.

If self.delay is set, a DelayedUse entity will be created that will actually
do the SUB_UseTargets after that many seconds have passed.

Centerprints any self.message to the activator.

Search for (string)targetname in all entities that
match (string)self.target and call their .use function

==============================
*/
void G_UseTargets(edict_t *ent, edict_t *activator)
{
    edict_t *t;

    //
    // check for a delay
    //
    if (ent->delay) {
        // create a temp object to fire at a later time
        t = G_Spawn();
        t->classname = "DelayedUse";
        t->nextthink = level.time + SEC(ent->delay);
        t->think = Think_Delay;
        t->activator = activator;
        if (!activator)
            G_Printf("Think_Delay with no activator\n");
        t->message = ent->message;
        t->target = ent->target;
        t->killtarget = ent->killtarget;
        return;
    }

    //
    // print the message
    //
    G_PrintActivationMessage(ent, activator, true);

    //
    // kill killtargets
    //
    if (ent->killtarget) {
        t = NULL;
        while ((t = G_Find(t, FOFS(targetname), ent->killtarget))) {
            if (t->teammaster) {
                // PMM - if this entity is part of a chain, cleanly remove it
                if (t->flags & FL_TEAMSLAVE) {
                    for (edict_t *master = t->teammaster; master; master = master->teamchain) {
                        if (master->teamchain == t) {
                            master->teamchain = t->teamchain;
                            break;
                        }
                    }
                // [Paril-KEX] remove teammaster too
                } else if (t->flags & FL_TEAMMASTER) {
                    t->teammaster->flags &= ~FL_TEAMMASTER;

                    edict_t *new_master = t->teammaster->teamchain;

                    if (new_master) {
                        new_master->flags |= FL_TEAMMASTER;
                        new_master->flags &= ~FL_TEAMSLAVE;

                        for (edict_t *m = new_master; m; m = m->teamchain)
                            m->teammaster = new_master;
                    }
                }
            }

            // [Paril-KEX] if we killtarget a monster, clean up properly
            if ((t->r.svflags & SVF_MONSTER) && !t->deadflag &&
                !(t->monsterinfo.aiflags & AI_DO_NOT_COUNT) && !(t->spawnflags & SPAWNFLAG_MONSTER_DEAD))
                G_MonsterKilled(t);

            // PMM
            G_FreeEdict(t);

            if (!ent->r.inuse) {
                G_Printf("entity was removed while using killtargets\n");
                return;
            }
        }
    }

    //
    // fire targets
    //
    if (ent->target) {
        t = NULL;
        while ((t = G_Find(t, FOFS(targetname), ent->target))) {
            // doors fire area portals in a specific way
            if (!Q_strcasecmp(t->classname, "func_areaportal") &&
                (!Q_strcasecmp(ent->classname, "func_door") || !Q_strcasecmp(ent->classname, "func_door_rotating") ||
                 !Q_strcasecmp(ent->classname, "func_door_secret") || !Q_strcasecmp(ent->classname, "func_water")))
                continue;

            if (t == ent)
                G_Printf("WARNING: Entity used itself.\n");
            else if (t->use)
                t->use(t, ent, activator);

            if (!ent->r.inuse) {
                G_Printf("entity was removed while using targets\n");
                return;
            }
        }
    }
}

char *etos(edict_t *ent)
{
    if (ent->r.linked) {
        vec3_t mid;
        VectorAvg(ent->r.absmin, ent->r.absmax, mid);
        return va("%s @ %s", ent->classname, vtos(mid));
    }
    return va("%s @ %s", ent->classname, vtos(ent->s.origin));
}

static const vec3_t VEC_UP = { 0, -1, 0 };
static const vec3_t MOVEDIR_UP = { 0, 0, 1 };
static const vec3_t VEC_DOWN = { 0, -2, 0 };
static const vec3_t MOVEDIR_DOWN = { 0, 0, -1 };

void G_SetMovedir(vec3_t angles, vec3_t movedir)
{
    if (VectorCompare(angles, VEC_UP))
        VectorCopy(MOVEDIR_UP, movedir);
    else if (VectorCompare(angles, VEC_DOWN))
        VectorCopy(MOVEDIR_DOWN, movedir);
    else
        AngleVectors(angles, movedir, NULL, NULL);

    VectorClear(angles);
}

float vectoyaw(const vec3_t vec)
{
    // PMM - fixed to correct for pitch of 0
    if (vec[PITCH] == 0) {
        if (vec[YAW] == 0)
            return 0;
        else if (vec[YAW] > 0)
            return 90;
        else
            return 270;
    }

    float yaw = RAD2DEG(atan2f(vec[YAW], vec[PITCH]));

    if (yaw < 0)
        yaw += 360;

    return yaw;
}

void G_FreeMemory(void)
{
    g_mem_used = 0;
}

void *G_Malloc(size_t len)
{
    if (len > sizeof(g_mem_pool))
        G_Error("Bad alloc size");
    len = Q_ALIGN(len, 8);
    if (len > sizeof(g_mem_pool) - g_mem_used)
        G_Error("Out of memory");
    void *out = g_mem_pool + g_mem_used;
    g_mem_used += len;
    return out;
}

char *G_CopyString(const char *in)
{
    if (!in)
        return NULL;
    size_t len = strlen(in) + 1;
    char *out = G_Malloc(len);
    memcpy(out, in, len);
    return out;
}

void G_MemoryInfo_f(void)
{
    G_Printf("%d bytes allocated (%.1f%%)\n", g_mem_used, g_mem_used * 100.0f / sizeof(g_mem_pool));
}

void G_InitEdict(edict_t *e)
{
    // ROGUE
    // FIXME -
    //   this fixes a bug somewhere that is setting "nextthink" for an entity that has
    //   already been released.  nextthink is being set to FRAME_TIME after level.time,
    //   since freetime = nextthink - FRAME_TIME
    if (e->nextthink)
        e->nextthink = 0;
    // ROGUE

    e->r.inuse = true;
    e->r.ownernum = ENTITYNUM_NONE;
    e->classname = "noclass";
    e->gravity = 1.0f;
    e->attenuation = ATTN_STATIC;
    e->vision_cone = -2.0f; // special value to use old algorithm
    e->s.number = e - g_edicts;

    // PGM - do this before calling the spawn function so it can be overridden.
    VectorSet(e->gravityVector, 0, 0, -1);
    // PGM
}

/*
=================
G_Spawn

Either finds a free edict, or allocates a new one.
Try to avoid reusing an entity that was recently freed, because it
can cause the client to think the entity morphed into something else
instead of being removed and recreated, which can cause interpolated
angles and bad trails.
=================
*/
edict_t *G_Spawn(void)
{
    int      i;
    edict_t *e;

    for (i = game.maxclients, e = g_edicts + i; i < level.num_edicts; i++, e++) {
        // the first couple seconds of server time can involve a lot of
        // freeing and allocating, so relax the replacement policy
        if (!e->r.inuse && (e->freetime < SEC(2) || level.time - e->freetime > SEC(0.5f))) {
            G_InitEdict(e);
            return e;
        }
    }

    if (i == ENTITYNUM_WORLD)
        G_Error("ED_Alloc: no free edicts");

    // allocate new edict
    level.num_edicts++;

    // inform the server that num_edicts has changed
    trap_SetNumEdicts(level.num_edicts);

    G_InitEdict(e);
    return e;
}

/*
=================
G_FreeEdict

Marks the edict as free
=================
*/
void THINK(G_FreeEdict)(edict_t *ed)
{
    // already freed
    if (!ed->r.inuse)
        return;

    trap_UnlinkEntity(ed); // unlink from world

    if ((ed - g_edicts) < (game.maxclients + BODY_QUEUE_SIZE))
        return;

    int id = ed->spawn_count + 1;
    memset(ed, 0, sizeof(*ed));
    ed->s.number = ed - g_edicts;
    ed->classname = "freed";
    ed->freetime = level.time;
    ed->r.inuse = false;
    ed->r.ownernum = ENTITYNUM_NONE;
    ed->spawn_count = id;
}

/*
============
G_TouchTriggers

============
*/
void G_TouchTriggers(edict_t *ent)
{
    int      i, num;
    int      touch[MAX_EDICTS_OLD];
    edict_t *hit;

    // dead things don't activate triggers!
    if ((ent->client || (ent->r.svflags & SVF_MONSTER)) && (ent->health <= 0))
        return;

    num = trap_BoxEdicts(ent->r.absmin, ent->r.absmax, touch, q_countof(touch), AREA_TRIGGERS);

    // be careful, it is possible to have an entity in this
    // list removed before we get to it (killtriggered)
    for (i = 0; i < num; i++) {
        hit = g_edicts + touch[i];
        if (!hit->r.inuse)
            continue;
        if (!hit->touch)
            continue;
        hit->touch(hit, ent, &null_trace, true);
    }
}

typedef struct {
    edict_t     *projectile;
    int          spawn_count;
} skipped_projectile_t;

// [Paril-KEX] scan for projectiles between our movement positions
// to see if we need to collide against them
void G_TouchProjectiles(edict_t *ent, const vec3_t previous_origin)
{
    // a bit ugly, but we'll store projectiles we are ignoring here.
    skipped_projectile_t skipped[MAX_EDICTS_OLD], *skip;
    int num_skipped = 0;

    while (num_skipped < q_countof(skipped)) {
        trace_t tr;
        trap_Trace(&tr, previous_origin, ent->r.mins, ent->r.maxs,
                   ent->s.origin, ent->s.number, ent->clipmask | CONTENTS_PROJECTILE);

        if (tr.fraction == 1.0f)
            break;

        edict_t *hit = &g_edicts[tr.entnum];

        if (!(hit->r.svflags & SVF_PROJECTILE))
            break;

        // always skip this projectile since certain conditions may cause the projectile
        // to not disappear immediately
        hit->r.svflags &= ~SVF_PROJECTILE;

        skip = &skipped[num_skipped++];
        skip->projectile = hit;
        skip->spawn_count = hit->spawn_count;

        // if we're both players and it's coop, allow the projectile to "pass" through
        if (ent->client && hit->r.ownernum != ENTITYNUM_NONE &&
            g_edicts[hit->r.ownernum].client && !G_ShouldPlayersCollide(true))
            continue;

        G_Impact(ent, &tr);
    }

    for (int i = 0; i < num_skipped; i++) {
        skip = &skipped[i];
        if (skip->projectile->r.inuse && skip->projectile->spawn_count == skip->spawn_count)
            skip->projectile->r.svflags |= SVF_PROJECTILE;
    }
}

/*
==============================================================================

Kill box

==============================================================================
*/

bool G_BrushModelClip(edict_t *self, edict_t *other)
{
    if (self->r.solid == SOLID_BSP || (self->r.svflags & SVF_HULL)) {
        trace_t clip;
        trap_Clip(&clip, other->s.origin, other->r.mins, other->r.maxs, other->s.origin, self->s.number, G_GetClipMask(other));

        if (clip.fraction == 1.0f)
            return false;
    }

    return true;
}

/*
=================
KillBox

Kills all entities that would touch the proposed new positioning
of ent.
=================
*/
bool KillBoxEx(edict_t *ent, bool from_spawning, mod_id_t mod, bool bsp_clipping, bool allow_safety)
{
    // don't telefrag as spectator...
    if (ent->movetype == MOVETYPE_NOCLIP)
        return true;

    contents_t mask = CONTENTS_MONSTER | CONTENTS_PLAYER;

    // [Paril-KEX] don't gib other players in coop if we're not colliding
    if (from_spawning && ent->client && coop.integer && !G_ShouldPlayersCollide(false))
        mask &= ~CONTENTS_PLAYER;

    int      i, num;
    int      touch[MAX_EDICTS_OLD];
    edict_t *hit;

    num = trap_BoxEdicts(ent->r.absmin, ent->r.absmax, touch, q_countof(touch), AREA_SOLID);

    for (i = 0; i < num; i++) {
        hit = g_edicts + touch[i];

        if (hit == ent)
            continue;
        if (!hit->r.inuse || !hit->takedamage || hit->r.solid != SOLID_BBOX)
            continue;
        if (hit->client && !(mask & CONTENTS_PLAYER))
            continue;
        if (bsp_clipping && !G_BrushModelClip(ent, hit))
            continue;

        // [Paril-KEX] don't allow telefragging of friends in coop.
        // the player that is about to be telefragged will have collision
        // disabled until another time.
        if (ent->client && hit->client && coop.integer) {
            hit->clipmask &= ~CONTENTS_PLAYER;
            ent->clipmask &= ~CONTENTS_PLAYER;
            continue;
        }

        if (allow_safety && G_FixStuckObject(hit, hit->s.origin) != NO_GOOD_POSITION)
            continue;

        T_Damage(hit, ent, ent, vec3_origin, ent->s.origin, 0, 100000, 0, DAMAGE_NO_PROTECTION, (mod_t) { mod });
    }

    return true; // all clear
}

void G_PositionedSound(const vec3_t origin, soundchan_t channel, int index, float volume, float attenuation)
{
    G_TempEntity(origin, EV_SOUND, G_EncodeSound(channel, index, volume, attenuation));
}

void G_StartSound(edict_t *ent, soundchan_t channel, int index, float volume, float attenuation)
{
    G_AddEvent(ent, EV_SOUND, G_EncodeSound(channel, index, volume, attenuation));
}

void G_LocalSound(edict_t *ent, soundchan_t channel, int index, float volume, float attenuation)
{
    trap_ClientCommand(ent, va("sound %d %d %d %g %g", ent->s.number, channel, index, volume, attenuation), false);
}

void G_ReliableSound(edict_t *ent, soundchan_t channel, int index, float volume, float attenuation)
{
    trap_ClientCommand(NULL, va("sound %d %d %d %g %g", ent->s.number, channel, index, volume, attenuation), true);
}

uint32_t G_EncodeSound(soundchan_t channel, int index, float volume, float attenuation)
{
    uint32_t vol, att;

    if (!index)
        return 0;

    Q_assert(index < MAX_SOUNDS);
    Q_assert(channel < 32);

    vol = Q_clip(volume * 255, 1, 255);
    if (vol == 255)
        vol = 0;

    if (attenuation == ATTN_NONE) {
        att = ATTN_ESCAPE_CODE;
    } else {
        att = Q_clip(attenuation * 64, 1, 255);
        if (att == ATTN_ESCAPE_CODE)
            att = 0;
    }

    return vol << 24 | att << 16 | channel << 11 | index;
}

// returns true if event should be added to PHS
static bool G_IsHearableEvent(entity_event_t event)
{
    switch (event) {
    case EV_ITEM_RESPAWN:
    case EV_FOOTSTEP:
    case EV_OTHER_FOOTSTEP:
    case EV_LADDER_STEP:
    case EV_STAIR_STEP:
    case EV_PLAYER_TELEPORT:
    case EV_OTHER_TELEPORT:
    case EV_SPLASH_UNKNOWN ... EV_TUNNEL_SPARKS:
    case EV_CHAINFIST_SMOKE:
        return false;
    default:
        return true;
    }
}

void G_AddEvent(edict_t *ent, entity_event_t event, uint32_t param)
{
    if (!event)
        return;

    if (G_IsHearableEvent(event))
        ent->r.svflags |= SVF_PHS;

    for (int i = 0; i < MAX_EVENTS; i++) {
        if (ent->s.event[i] == event && ent->s.event_param[i] == param)
            return;
        if (!ent->s.event[i]) {
            ent->s.event[i] = event;
            ent->s.event_param[i] = param;
            return;
        }
    }

    for (int i = 0; i < MAX_EVENTS; i++) {
        if (ent->s.event[i] == EV_FOOTSTEP || ent->s.event[i] == EV_OTHER_FOOTSTEP) {
            ent->s.event[i] = event;
            ent->s.event_param[i] = param;
            return;
        }
    }

    if (sv_running.integer >= 2) {
        G_Printf("Too many events for %s: ", etos(ent));
        for (int i = 0; i < MAX_EVENTS; i++)
            G_Printf("%s, ", BG_EventName(ent->s.event[i]));
        G_Printf("%s\n", BG_EventName(event));
    }
}

edict_t *G_TempEntity(const vec3_t origin, entity_event_t event, uint32_t param)
{
    edict_t *ent;

    ent = G_Spawn();
    if (G_IsHearableEvent(event))
        ent->r.svflags |= SVF_PHS;
    G_SnapVector(origin, ent->s.origin);
    ent->s.event[0] = event;
    ent->s.event_param[0] = param;
    ent->free_after_event = true;
    trap_LinkEntity(ent);

    return ent;
}

edict_t *G_SpawnTrail(const vec3_t start, const vec3_t end, entity_event_t event)
{
    edict_t *ent;

    ent = G_Spawn();
    ent->s.renderfx = RF_BEAM;
    G_SnapVector(start, ent->s.old_origin);
    G_SnapVector(end, ent->s.origin);
    ent->s.event[0] = event;
    ent->free_after_event = true;
    trap_LinkEntity(ent);

    return ent;
}

void G_BecomeEvent(edict_t *ent, entity_event_t event, uint32_t param)
{
    ent->r.solid = SOLID_NOT;
    ent->r.svflags = SVF_NONE;
    VectorClear(ent->r.mins);
    VectorClear(ent->r.maxs);

    ent->s.modelindex = 0;
    ent->s.modelindex2 = 0;
    ent->s.effects = 0;
    ent->s.renderfx = 0;
    ent->s.sound = 0;
    ent->s.morefx = 0;
    ent->think = NULL;
    ent->nextthink = 0;
    ent->use = NULL;
    ent->targetname = NULL;

    G_AddEvent(ent, event, param);

    ent->free_after_event = true;
    trap_LinkEntity(ent);
}

void G_SnapVectorTowards(const vec3_t v, const vec3_t to, vec3_t out)
{
    for (int i = 0; i < 3; i++) {
        float t = truncf(v[i]);

        if (v[i] >= to[i]) {
            if (v[i] < 0)
               out[i] = t - 1;
            else
               out[i] = t;
        } else {
            if (v[i] < 0)
                out[i] = t;
            else
                out[i] = t + 1;
        }
    }
}

void G_SnapVector(const vec3_t v, vec3_t out)
{
    out[0] = rintf(v[0]);
    out[1] = rintf(v[1]);
    out[2] = rintf(v[2]);
}

int G_ModelIndex(const char *name)
{
    return trap_FindConfigstring(name, CS_MODELS, MAX_MODELS, true);
}

int G_SoundIndex(const char *name)
{
    return trap_FindConfigstring(name, CS_SOUNDS, MAX_SOUNDS, true);
}

int G_ImageIndex(const char *name)
{
    return trap_FindConfigstring(name, CS_IMAGES, MAX_IMAGES, true);
}

void G_ClientPrintf(edict_t *ent, print_level_t level, const char *fmt, ...)
{
    va_list     argptr;
    char        text[MAX_STRING_CHARS];
    const char  *cmd;

    switch (level) {
    case PRINT_CHAT:
        cmd = "chat";
        break;
    case PRINT_CENTER:
        cmd = "cp";
        break;
    case PRINT_TYPEWRITER:
        cmd = "tw";
        break;
    default:
        cmd = "print";
        break;
    }

    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    for (int i = 0; text[i]; i++)
        if (text[i] == '\"')
            text[i] = '`';

    trap_ClientCommand(ent, va("%s \"%s\"", cmd, text), true);

    // echo to console
    if (!ent && sv_dedicated.integer) {
        if (level == PRINT_CHAT)
            trap_Print(PRINT_TALK, text);
        else if (level < PRINT_CHAT)
            trap_Print(PRINT_ALL, text);
    }
}
