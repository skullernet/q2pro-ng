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

static box3_t       area_box;
static int          *area_list;
static int          area_count, area_maxcount;
static int          area_type;

/*
===============
SV_CreateAreaNode

Builds a uniformly subdivided tree for the given world size
===============
*/
static areanode_t *SV_CreateAreaNode(int depth, box3_t box)
{
    areanode_t  *anode;
    vec3_t      size;
    box3_t      box2;

    anode = &sv_areanodes[sv_numareanodes];
    sv_numareanodes++;

    List_Init(&anode->trigger_edicts);
    List_Init(&anode->solid_edicts);

    if (depth == AREA_DEPTH) {
        anode->axis = -1;
        anode->children[0] = anode->children[1] = NULL;
        return anode;
    }

    size = Box3_Size(box);
    if (size.x > size.y)
        anode->axis = 0;
    else
        anode->axis = 1;

    anode->dist = 0.5f * (box.maxs.xyz[anode->axis] + box.mins.xyz[anode->axis]);

    box2 = box;
    box2.mins.xyz[anode->axis] = box2.maxs.xyz[anode->axis] = anode->dist;

    anode->children[0] = SV_CreateAreaNode(depth + 1, Box3(box2.mins, box.maxs));
    anode->children[1] = SV_CreateAreaNode(depth + 1, Box3(box.mins,  box2.maxs));

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

    if (sv.cm.cache)
        SV_CreateAreaNode(0, sv.cm.cache->models[0].box);

    // make sure all entities are unlinked
    for (int i = 0; i < MAX_EDICTS; i++) {
        edict_t *ent = SV_EdictForNum(i);
        ent->r.linked = false;
        ent->r.linkcount = 0;

        server_entity_t *sent = &sv.entities[i];
        List_Init(&sent->area);
        sent->number = i;
    }
}

/*
===============
SV_LinkEdict

Links entity to PVS leafs.
===============
*/

#define MAX_TOTAL_ENT_LEAFS     4096

static void SV_LinkEdict(const cm_t *cm, edict_t *ent, server_entity_t *sent)
{
    const mleaf_t  *leafs[MAX_TOTAL_ENT_LEAFS];
    int             clusters[MAX_TOTAL_ENT_LEAFS];
    int             i, j, num_leafs, num_clusters;

    // set the size
    ent->r.size = Box3_Size(ent->r.box);

    // set the abs box
    if (ent->r.solid == SOLID_BSP && !Vec3_IsEmpty(ent->s.angles)) {
        // expand for rotation
        ent->r.absbox = Box3_Translate(Box3_FromRotated(ent->r.box), ent->s.origin);
    } else if (ent->r.solid == SOLID_NOT && ent->s.renderfx & RF_BEAM) {
        ent->r.absbox = Box3_AddPoint(Box3_FromPoint(ent->s.old_origin), ent->s.origin);
    } else { // normal
        ent->r.absbox = Box3_Translate(ent->r.box, ent->s.origin);
    }

    // because movement is clipped an epsilon away from an actual edge,
    // we must fully check even when bounding boxes don't quite touch
    ent->r.absbox = Box3_Expand(ent->r.absbox, 1);

// link to PVS leafs
    ent->r.areanum = 0;
    ent->r.areanum2 = 0;

    // get all leafs, including solids
    num_leafs = CM_BoxLeafs(cm, ent->r.absbox, leafs, q_countof(leafs));
    num_clusters = 0;

    if (num_leafs == q_countof(leafs) && sv.state == ss_loading)
        Com_DPrintf("Object %d touching %d leafs at %s\n", SV_NumForEdict(ent), num_leafs, btos(ent->r.absbox));

    // set areas
    for (i = 0; i < num_leafs; i++) {
        int area, cluster;

        area = leafs[i]->area;
        if (area) {
            // doors may legally straggle two areas,
            // but nothing should evern need more than that
            if (ent->r.areanum && ent->r.areanum != area) {
                if (ent->r.areanum2 && ent->r.areanum2 != area && sv.state == ss_loading)
                    Com_DPrintf("Object %d touching 3 areas at %s\n", SV_NumForEdict(ent), btos(ent->r.absbox));
                ent->r.areanum2 = area;
            } else
                ent->r.areanum = area;
        }

        // find unique clusters
        cluster = leafs[i]->cluster;
        if (cluster != -1) {
            for (j = 0; j < num_clusters; j++)
                if (clusters[j] == cluster)
                    break;
            if (j == num_clusters)
                clusters[num_clusters++] = cluster;
        }
    }

    if (num_clusters > sent->num_clusters)
        sent->clusternums = Z_TagRealloc(sent->clusternums, sizeof(sent->clusternums[0]) * Q_ALIGN(num_clusters, 64), TAG_SERVER);
    sent->num_clusters = num_clusters;
    for (i = 0; i < num_clusters; i++)
        sent->clusternums[i] = clusters[i];
}

void PF_UnlinkEdict(edict_t *ent)
{
    if (!ent)
        Com_Error(ERR_DROP, "%s: NULL", __func__);

    server_entity_t *sent = SV_SentForEdict(ent);
    if (sent->area.next)
        List_Delete(&sent->area);

    ent->r.linked = false;
}

static uint32_t SV_PackSolid(const edict_t *ent)
{
    box3_t box = ent->r.box;

    // undo scaling before packing
    if (ent->s.scale)
        box = Box3_Scale(box, 1.0f / ent->s.scale);

    uint32_t solid = MSG_PackSolid(box);

#if USE_DEBUG
    // check if bbox is symmetrical
    if (developer->integer && sv.state == ss_loading) {
        box3_t box2 = MSG_UnpackSolid(solid);

        if (!Box3_IsEqual(box, box2))
            Com_LPrintf(PRINT_DEVELOPER, "Entity %d has bad bbox: %s\n", SV_NumForEdict(ent), btos(ent->r.box));
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
        if ((ent->r.svflags & SVF_DEADMONSTER) || Box3_IsPoint(ent->r.box))
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
        ent->s.old_origin = ent->s.origin;

    ent->r.linked = true;
    ent->r.linkcount++;

    if (ent->r.solid == SOLID_NOT)
        return;

// find the first node that the ent's box crosses
    node = sv_areanodes;
    while (1) {
        if (node->axis == -1)
            break;
        if (ent->r.absbox.mins.xyz[node->axis] > node->dist)
            node = node->children[0];
        else if (ent->r.absbox.maxs.xyz[node->axis] < node->dist)
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

static void SV_TouchAreaEdicts(const list_t *list)
{
    const server_entity_t *sent;

    LIST_FOR_EACH(sent, list, area) {
        const edict_t *check = SV_EdictForNum(sent->number);

        if (check->r.solid == SOLID_NOT)
            continue;       // deactivated
        if (!Box3_Intersects(check->r.absbox, area_box))
            continue;       // not touching

        if (area_count == area_maxcount) {
            Com_WPrintf("SV_AreaEdicts: MAXCOUNT\n");
            return;
        }

        area_list[area_count] = sent->number;
        area_count++;
    }
}

/*
====================
SV_AreaEdicts_r

====================
*/
static void SV_AreaEdicts_r(const areanode_t *node)
{
    // touch linked edicts
    if (q_likely(area_type & AREA_SOLID))
        SV_TouchAreaEdicts(&node->solid_edicts);

    if (q_likely(area_type & AREA_TRIGGERS))
        SV_TouchAreaEdicts(&node->trigger_edicts);

    if (area_count == area_maxcount)
        return;     // no free space

    if (node->axis == -1)
        return;     // terminal node

    // recurse down both sides
    if (area_box.maxs.xyz[node->axis] > node->dist)
        SV_AreaEdicts_r(node->children[0]);
    if (area_box.mins.xyz[node->axis] < node->dist)
        SV_AreaEdicts_r(node->children[1]);
}

/*
================
SV_AreaEdicts
================
*/
int SV_AreaEdicts(box3_t box, int *list, int maxcount, int areatype)
{
    area_box = box;
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
    return CM_HeadnodeForBox(ent->r.box);
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
contents_t SV_PointContents(vec3_t p)
{
    int         touch[MAX_EDICTS_OLD];
    edict_t     *hit;
    int         i, num;
    contents_t  contents;

    // get base contents from world
    contents = CM_PointContents(p, SV_WorldNodes());

    // or in contents from all the other entities
    num = SV_AreaEdicts(Box3_FromPoint(p), touch, q_countof(touch), AREA_SOLID);

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
static void SV_ClipMoveToEntities(trace_t *tr, const trace_args_t *args)
{
    box3_t      box;
    int         i, num;
    unsigned    ownernum = ENTITYNUM_NONE;
    unsigned    passent = args->entnum;
    contents_t  contentmask = args->mask;
    int         touchlist[MAX_EDICTS];
    edict_t     *touch;
    trace_t     trace;

    // create the bounding box of the entire move
    for (i = 0; i < 3; i++) {
        if (args->end.xyz[i] > args->start.xyz[i]) {
            box.mins.xyz[i] = args->start.xyz[i] + args->box.mins.xyz[i] - 1;
            box.maxs.xyz[i] = args->end.xyz[i]   + args->box.maxs.xyz[i] + 1;
        } else {
            box.mins.xyz[i] = args->end.xyz[i]   + args->box.mins.xyz[i] - 1;
            box.maxs.xyz[i] = args->start.xyz[i] + args->box.maxs.xyz[i] + 1;
        }
    }

    num = SV_AreaEdicts(box, touchlist, q_countof(touchlist), AREA_SOLID);

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
        CM_TransformedBoxTrace(&trace, args,
                               SV_HullForEntity(touch, SVF_NONE),
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
void SV_Trace(trace_t *trace, const trace_args_t *args)
{
    Q_assert_soft(args->entnum < MAX_EDICTS);

    // clip to world
    CM_BoxTrace(trace, args, SV_WorldNodes());
    trace->entnum = ENTITYNUM_WORLD;
    if (trace->fraction == 0)
        return;     // blocked by the world

    // clip to other solid entities
    SV_ClipMoveToEntities(trace, args);
}

/*
==================
SV_Clip

Like SV_Trace(), but clip to specified entity only.
Can be used to clip to SOLID_TRIGGER by its BSP tree.
==================
*/
void SV_Clip(trace_t *trace, const trace_args_t *args)
{
    Q_assert_soft(args->entnum < MAX_EDICTS);

    if (args->entnum == ENTITYNUM_WORLD) {
        CM_BoxTrace(trace, args, SV_WorldNodes());
    } else {
        edict_t *clip = SV_EdictForNum(args->entnum);
        CM_TransformedBoxTrace(trace, args,
                               SV_HullForEntity(clip, SVF_HULL),
                               clip->s.origin, clip->s.angles);
    }
    trace->entnum = args->entnum;
}
