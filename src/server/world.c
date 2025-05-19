/*
Copyright (C) 1997-2001 Id Software, Inc.

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/
// world.c -- world query functions

#include "server.h"

/*
===============================================================================

ENTITY AREA CHECKING

FIXME: this use of "area" is different from the bsp file use
===============================================================================
*/

typedef struct areanode_s {
    int     axis;       // -1 = leaf node
    float   dist;
    struct areanode_s   *children[2];
    list_t  trigger_edicts;
    list_t  solid_edicts;
} areanode_t;

#define    AREA_DEPTH    4
#define    AREA_NODES    32

static areanode_t   sv_areanodes[AREA_NODES];
static int          sv_numareanodes;

static const vec_t  *area_mins, *area_maxs;
static int          *area_list;
static int          area_count, area_maxcount;
static int          area_type;

/*
===============
SV_CreateAreaNode

Builds a uniformly subdivided tree for the given world size
===============
*/
static areanode_t *SV_CreateAreaNode(int depth, const vec3_t mins, const vec3_t maxs)
{
    areanode_t  *anode;
    vec3_t      size;
    vec3_t      mins1, maxs1, mins2, maxs2;

    anode = &sv_areanodes[sv_numareanodes];
    sv_numareanodes++;

    List_Init(&anode->trigger_edicts);
    List_Init(&anode->solid_edicts);

    if (depth == AREA_DEPTH) {
        anode->axis = -1;
        anode->children[0] = anode->children[1] = NULL;
        return anode;
    }

    VectorSubtract(maxs, mins, size);
    if (size[0] > size[1])
        anode->axis = 0;
    else
        anode->axis = 1;

    anode->dist = 0.5f * (maxs[anode->axis] + mins[anode->axis]);
    VectorCopy(mins, mins1);
    VectorCopy(mins, mins2);
    VectorCopy(maxs, maxs1);
    VectorCopy(maxs, maxs2);

    maxs1[anode->axis] = mins2[anode->axis] = anode->dist;

    anode->children[0] = SV_CreateAreaNode(depth + 1, mins2, maxs2);
    anode->children[1] = SV_CreateAreaNode(depth + 1, mins1, maxs1);

    return anode;
}

/*
===============
SV_ClearWorld

===============
*/
void SV_ClearWorld(void)
{
    memset(sv_areanodes, 0, sizeof(sv_areanodes));
    sv_numareanodes = 0;

    if (sv.cm.cache) {
        const mmodel_t *cm = &sv.cm.cache->models[0];
        SV_CreateAreaNode(0, cm->mins, cm->maxs);
    }

    // make sure all entities are unlinked
    for (int i = 0; i < ge->max_edicts; i++) {
        edict_t *ent = SV_EdictForNum(i);
        ent->r.linked = false;
        ent->r.linkcount = 0;

        server_entity_t *sent = &sv.entities[i];
        List_Init(&sent->area);
        sent->edict = ent;
    }
}

/*
===============
SV_LinkEdict

Links entity to PVS leafs.
===============
*/
static void SV_LinkEdict(const cm_t *cm, edict_t *ent, server_entity_t *sent)
{
    const mleaf_t   *leafs[MAX_TOTAL_ENT_LEAFS];
    int             clusters[MAX_TOTAL_ENT_LEAFS];
    int             i, j, area, num_leafs;
    const mnode_t   *topnode;

    // set the size
    VectorSubtract(ent->r.maxs, ent->r.mins, ent->r.size);

    // set the abs box
    if (ent->r.solid == SOLID_BSP && !VectorEmpty(ent->s.angles)) {
        // expand for rotation
        float   max, v;

        max = 0;
        for (i = 0; i < 3; i++) {
            v = fabsf(ent->r.mins[i]);
            if (v > max)
                max = v;
            v = fabsf(ent->r.maxs[i]);
            if (v > max)
                max = v;
        }
        for (i = 0; i < 3; i++) {
            ent->r.absmin[i] = ent->s.origin[i] - max;
            ent->r.absmax[i] = ent->s.origin[i] + max;
        }
    } else if (ent->r.solid == SOLID_NOT && ent->s.renderfx & RF_BEAM) {
        ClearBounds(ent->r.absmin, ent->r.absmax);
        AddPointToBounds(ent->s.origin, ent->r.absmin, ent->r.absmax);
        AddPointToBounds(ent->s.old_origin, ent->r.absmin, ent->r.absmax);
    } else {
        // normal
        VectorAdd(ent->s.origin, ent->r.mins, ent->r.absmin);
        VectorAdd(ent->s.origin, ent->r.maxs, ent->r.absmax);
    }

    // because movement is clipped an epsilon away from an actual edge,
    // we must fully check even when bounding boxes don't quite touch
    ent->r.absmin[0] -= 1;
    ent->r.absmin[1] -= 1;
    ent->r.absmin[2] -= 1;
    ent->r.absmax[0] += 1;
    ent->r.absmax[1] += 1;
    ent->r.absmax[2] += 1;

// link to PVS leafs
    sent->num_clusters = 0;
    ent->r.areanum = 0;
    ent->r.areanum2 = 0;

    // get all leafs, including solids
    num_leafs = CM_BoxLeafs(cm, ent->r.absmin, ent->r.absmax,
                            leafs, q_countof(leafs), &topnode);

    // set areas
    for (i = 0; i < num_leafs; i++) {
        clusters[i] = leafs[i]->cluster;
        area = leafs[i]->area;
        if (area) {
            // doors may legally straggle two areas,
            // but nothing should evern need more than that
            if (ent->r.areanum && ent->r.areanum != area) {
                if (ent->r.areanum2 && ent->r.areanum2 != area && sv.state == ss_loading)
                    Com_DPrintf("Object touching 3 areas at %s\n", vtos(ent->r.absmin));
                ent->r.areanum2 = area;
            } else
                ent->r.areanum = area;
        }
    }

    if (num_leafs == q_countof(leafs)) {
        // assume we missed some leafs, and mark by headnode
        sent->num_clusters = -1;
        sent->headnode = CM_NumNode(cm, topnode);
    } else {
        sent->num_clusters = 0;
        for (i = 0; i < num_leafs; i++) {
            if (clusters[i] == -1)
                continue;        // not a visible leaf
            for (j = 0; j < i; j++)
                if (clusters[j] == clusters[i])
                    break;
            if (j == i) {
                if (sent->num_clusters == MAX_ENT_CLUSTERS) {
                    // assume we missed some leafs, and mark by headnode
                    sent->num_clusters = -1;
                    sent->headnode = CM_NumNode(cm, topnode);
                    break;
                }

                sent->clusternums[sent->num_clusters++] = clusters[i];
            }
        }
    }
}

void PF_UnlinkEdict(edict_t *ent)
{
    if (!ent)
        Com_Error(ERR_DROP, "%s: NULL", __func__);

    server_entity_t *sent = SV_SentForEdict(ent);
    if (sent->edict)
        List_Delete(&sent->area);

    ent->r.linked = false;
}

static uint32_t SV_PackSolid(const edict_t *ent)
{
    uint32_t solid;

    solid = MSG_PackSolid(ent->r.mins, ent->r.maxs);

    if (solid == PACKED_BSP)
        solid = 0;  // can happen in pathological case if z mins > maxs

#if USE_DEBUG
    if (developer->integer) {
        vec3_t mins, maxs;

        MSG_UnpackSolid(solid, mins, maxs);

        if (!VectorCompare(ent->r.mins, mins) || !VectorCompare(ent->r.maxs, maxs))
            Com_LPrintf(PRINT_DEVELOPER, "Bad mins/maxs on entity %d: %s %s\n",
                        SV_NumForEdict(ent), vtos(ent->r.mins), vtos(ent->r.maxs));
    }
#endif

    return solid;
}

void PF_LinkEdict(edict_t *ent)
{
    server_entity_t *sent;
    areanode_t *node;
    int entnum;

    if (!ent)
        Com_Error(ERR_DROP, "%s: NULL", __func__);

    PF_UnlinkEdict(ent);    // unlink from old position

    if (!ent->r.inuse)
        return;

    if (!sv.cm.cache)
        return;

    entnum = SV_NumForEdict(ent);

    if (entnum >= ENTITYNUM_WORLD)
        return;        // don't add the world

    Q_assert_soft(ent->s.number == entnum);

    // encode the size into the entity_state for client prediction
    switch (ent->r.solid) {
    case SOLID_BBOX:
        if ((ent->r.svflags & SVF_DEADMONSTER) || VectorCompare(ent->r.mins, ent->r.maxs))
            ent->s.solid = 0;
        else
            ent->s.solid = SV_PackSolid(ent);
        break;
    case SOLID_BSP:
        ent->s.solid = PACKED_BSP;      // a SOLID_BBOX will never create this value
        break;
    default:
        ent->s.solid = 0;
        break;
    }

    sent = sv.entities + entnum;

    SV_LinkEdict(&sv.cm, ent, sent);

    // if first time, make sure old_origin is valid
    if (!ent->r.linkcount && !(ent->s.renderfx & RF_BEAM))
        VectorCopy(ent->s.origin, ent->s.old_origin);

    ent->r.linked = true;
    ent->r.linkcount++;

    if (ent->r.solid == SOLID_NOT)
        return;

// find the first node that the ent's box crosses
    node = sv_areanodes;
    while (1) {
        if (node->axis == -1)
            break;
        if (ent->r.absmin[node->axis] > node->dist)
            node = node->children[0];
        else if (ent->r.absmax[node->axis] < node->dist)
            node = node->children[1];
        else
            break;        // crosses the node
    }

    // link it in
    if (ent->r.solid == SOLID_TRIGGER)
        List_Append(&node->trigger_edicts, &sent->area);
    else
        List_Append(&node->solid_edicts, &sent->area);
}


/*
====================
SV_AreaEdicts_r

====================
*/
static void SV_AreaEdicts_r(areanode_t *node)
{
    list_t          *start;
    server_entity_t *sent;

    // touch linked edicts
    if (area_type == AREA_SOLID)
        start = &node->solid_edicts;
    else
        start = &node->trigger_edicts;

    LIST_FOR_EACH(server_entity_t, sent, start, area) {
        edict_t *check = sent->edict;

        if (check->r.solid == SOLID_NOT)
            continue;        // deactivated
        if (check->r.absmin[0] > area_maxs[0]
            || check->r.absmin[1] > area_maxs[1]
            || check->r.absmin[2] > area_maxs[2]
            || check->r.absmax[0] < area_mins[0]
            || check->r.absmax[1] < area_mins[1]
            || check->r.absmax[2] < area_mins[2])
            continue;        // not touching

        if (area_count == area_maxcount) {
            Com_WPrintf("SV_AreaEdicts: MAXCOUNT\n");
            return;
        }

        area_list[area_count] = sent - sv.entities;
        area_count++;
    }

    if (node->axis == -1)
        return;        // terminal node

    // recurse down both sides
    if (area_maxs[node->axis] > node->dist)
        SV_AreaEdicts_r(node->children[0]);
    if (area_mins[node->axis] < node->dist)
        SV_AreaEdicts_r(node->children[1]);
}

/*
================
SV_AreaEdicts
================
*/
int SV_AreaEdicts(const vec3_t mins, const vec3_t maxs,
                  int *list, int maxcount, int areatype)
{
    area_mins = mins;
    area_maxs = maxs;
    area_list = list;
    area_count = 0;
    area_maxcount = maxcount;
    area_type = areatype;

    SV_AreaEdicts_r(sv_areanodes);

    return area_count;
}


//===========================================================================

/*
================
SV_HullForEntity

Returns a headnode that can be used for testing or clipping an
object of mins/maxs size.
================
*/
static const mnode_t *SV_HullForEntity(const edict_t *ent, int mask)
{
    if (ent->r.solid == SOLID_BSP || (ent->r.svflags & mask)) {
        const bsp_t *bsp = sv.cm.cache;

        if (!bsp)
            Com_Error(ERR_DROP, "%s: no map loaded", __func__);

        if (ent->s.modelindex <= 0 || ent->s.modelindex >= bsp->nummodels)
            Com_Error(ERR_DROP, "%s: inline model %d out of range", __func__, ent->s.modelindex);

        // explicit hulls in the BSP model
        return bsp->models[ent->s.modelindex].headnode;
    }

    // create a temp hull from bounding box sizes
    return CM_HeadnodeForBox(ent->r.mins, ent->r.maxs);
}

/*
=============
SV_WorldNodes
=============
*/
static const mnode_t *SV_WorldNodes(void)
{
    return sv.cm.cache ? sv.cm.cache->nodes : NULL;
}

/*
=============
SV_PointContents
=============
*/
contents_t SV_PointContents(const vec3_t p)
{
    int         touch[MAX_EDICTS_OLD];
    edict_t     *hit;
    int         i, num;
    contents_t  contents;

    // get base contents from world
    contents = CM_PointContents(p, SV_WorldNodes());

    // or in contents from all the other entities
    num = SV_AreaEdicts(p, p, touch, q_countof(touch), AREA_SOLID);

    for (i = 0; i < num; i++) {
        hit = SV_EdictForNum(touch[i]);

        // might intersect, so do an exact clip
        contents |= CM_TransformedPointContents(p, SV_HullForEntity(hit, SVF_NONE),
                                                hit->s.origin, hit->s.angles);
    }

    return contents;
}

/*
====================
SV_ClipMoveToEntities
====================
*/
static void SV_ClipMoveToEntities(trace_t *tr,
                                  const vec3_t start, const vec3_t end,
                                  const vec3_t mins, const vec3_t maxs,
                                  int passent, contents_t contentmask)
{
    vec3_t      boxmins, boxmaxs;
    int         i, num, ownernum = ENTITYNUM_NONE;
    int         touchlist[MAX_EDICTS];
    edict_t     *touch;
    trace_t     trace;

    // create the bounding box of the entire move
    for (i = 0; i < 3; i++) {
        if (end[i] > start[i]) {
            boxmins[i] = start[i] + mins[i] - 1;
            boxmaxs[i] = end[i] + maxs[i] + 1;
        } else {
            boxmins[i] = end[i] + mins[i] - 1;
            boxmaxs[i] = start[i] + maxs[i] + 1;
        }
    }

    num = SV_AreaEdicts(boxmins, boxmaxs, touchlist, q_countof(touchlist), AREA_SOLID);

    if (passent != ENTITYNUM_NONE)
        ownernum = SV_EdictForNum(passent)->r.ownernum;

    // be careful, it is possible to have an entity in this
    // list removed before we get to it (killtriggered)
    for (i = 0; i < num; i++) {
        touch = SV_EdictForNum(touchlist[i]);
        if (touch->r.solid == SOLID_NOT)
            continue;
        if (tr->allsolid)
            return;
        if (passent != ENTITYNUM_NONE) {
            if (touchlist[i] == passent)
                continue;
            if (touch->r.ownernum == passent)
                continue;    // don't clip against own missiles
            if (touchlist[i] == ownernum)
                continue;    // don't clip against owner
        }

        if (!(contentmask & CONTENTS_DEADMONSTER)
            && (touch->r.svflags & SVF_DEADMONSTER))
            continue;

        if (!(contentmask & CONTENTS_PROJECTILE)
            && (touch->r.svflags & SVF_PROJECTILE))
            continue;

        if (!(contentmask & CONTENTS_PLAYER)
            && (touch->r.svflags & SVF_PLAYER))
            continue;

        // might intersect, so do an exact clip
        CM_TransformedBoxTrace(&trace, start, end, mins, maxs,
                               SV_HullForEntity(touch, SVF_NONE), contentmask,
                               touch->s.origin, touch->s.angles);

        CM_ClipEntity(tr, &trace, touchlist[i]);
    }
}

/*
==================
SV_Trace

Moves the given mins/maxs volume through the world from start to end.
Passedict and edicts owned by passedict are explicitly not checked.
==================
*/
void SV_Trace(trace_t *trace, const vec3_t start, const vec3_t mins,
              const vec3_t maxs, const vec3_t end, int passent, contents_t contentmask)
{
    if (!mins)
        mins = vec3_origin;
    if (!maxs)
        maxs = vec3_origin;

    // clip to world
    CM_BoxTrace(trace, start, end, mins, maxs, SV_WorldNodes(), contentmask);
    trace->entnum = ENTITYNUM_WORLD;
    if (trace->fraction == 0)
        return;     // blocked by the world

    // clip to other solid entities
    SV_ClipMoveToEntities(trace, start, end, mins, maxs, passent, contentmask);
}

/*
==================
SV_Clip

Like SV_Trace(), but clip to specified entity only.
Can be used to clip to SOLID_TRIGGER by its BSP tree.
==================
*/
void SV_Clip(trace_t *trace, const vec3_t start, const vec3_t mins,
             const vec3_t maxs, const vec3_t end, int clipent, contents_t contentmask)
{
    if (!mins)
        mins = vec3_origin;
    if (!maxs)
        maxs = vec3_origin;

    if (clipent == ENTITYNUM_WORLD) {
        CM_BoxTrace(trace, start, end, mins, maxs, SV_WorldNodes(), contentmask);
    } else {
        edict_t *clip = SV_EdictForNum(clipent);
        CM_TransformedBoxTrace(trace, start, end, mins, maxs,
                               SV_HullForEntity(clip, SVF_HULL), contentmask,
                               clip->s.origin, clip->s.angles);
    }
    trace->entnum = clipent;
}
