// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// nav.c -- Kex navigation node support

#include "server.h"
#include <float.h>

#define NAV_DEBUG (USE_REF && USE_DEBUG)

// magic file header
#define NAV_MAGIC   MakeLittleLong('N', 'A', 'V', '3')

// invalid value used for most of the system
#define INVALID_ID  0xffff

enum {
    // changes from 1: edict now contains model index.
    NAV_VERSION_2 = 2,

    // changes from 2: link team flags become general link flags;
    // all prior versions should use NavLinkFlag_AllTeams for link flags.
    // binary compatible with v2.
    NAV_VERSION_3,

    // changes from 3: ladder move plane was added to traversal.
    NAV_VERSION_4,

    // changes from 4: soft limit change for max nodes.
    // binary compatible with v4.
    NAV_VERSION_5,

    // last nav version we support.
    // changes from 5: yellow and green teams were removed;
    // all versions prior to 6 should strip bitflags 2 and 3
    // from link flags.
    // binary comaptible with v5.
    NAV_VERSION_6,
};

// flags that determine conditionals for nodes
typedef enum {
    NodeFlag_Normal             = 0,
    NodeFlag_Teleporter         = BIT(0),
    NodeFlag_Pusher             = BIT(1),
    NodeFlag_Elevator           = BIT(2),
    NodeFlag_Ladder             = BIT(3),
    NodeFlag_UnderWater         = BIT(4),
    NodeFlag_CheckForHazard     = BIT(5),
    NodeFlag_CheckHasFloor      = BIT(6),
    NodeFlag_CheckInSolid       = BIT(7),
    NodeFlag_NoMonsters         = BIT(8),
    NodeFlag_Crouch             = BIT(9),
    NodeFlag_NoPOI              = BIT(10),
    NodeFlag_CheckInLiquid      = BIT(11),
    NodeFlag_CheckDoorLinks     = BIT(12),
    NodeFlag_Disabled           = BIT(13),

    NodeFlag_ConditionalMask    = NodeFlag_CheckDoorLinks | NodeFlag_CheckForHazard |
        NodeFlag_CheckHasFloor | NodeFlag_CheckInLiquid | NodeFlag_CheckInSolid,
} nav_node_flags_t;

typedef struct nav_link_s nav_link_t;

// cached node data
typedef struct {
    nav_node_flags_t    flags;
    int                 num_links;
    nav_link_t          *links;
    int                 id;
    float               radius;
    vec3_t              origin;
} nav_node_t;

// link type
typedef enum {
    NavLinkType_Walk,
    NavLinkType_LongJump,
    NavLinkType_Teleport,
    NavLinkType_WalkOffLedge,
    NavLinkType_Pusher,
    NavLinkType_BarrierJump,
    NavLinkType_Elevator,
    NavLinkType_Train,
    NavLinkType_Manual_LongJump,
    NavLinkType_Crouch,
    NavLinkType_Ladder,
    NavLinkType_Manual_BarrierJump,
    NavLinkType_PivotAndJump,
    NavLinkType_RocketJump,
    NavLinkType_Unknown
} nav_link_type_t;

// link flags
typedef enum {
    NavLinkFlag_TeamRed         = BIT(0),
    NavLinkFlag_TeamBlue        = BIT(1),
    NavLinkFlag_ExitAtTarget    = BIT(2),
    NavLinkFlag_WalkOnly        = BIT(3),
    NavLinkFlag_EaseIntoTarget  = BIT(4),
    NavLinkFlag_InstantTurn     = BIT(5),
    NavLinkFlag_Disabled        = BIT(6),

    NavLinkFlag_AllTeams        = (NavLinkFlag_TeamRed | NavLinkFlag_TeamBlue)
} nav_link_flags_t;

// cached traversal data
typedef struct {
    vec3_t  funnel;
    vec3_t  start;
    vec3_t  end;
    vec3_t  ladder_plane;
} nav_traversal_t;

typedef struct nav_edict_s nav_edict_t;

// cached link data
typedef struct nav_link_s {
    nav_node_t          *target;
    nav_link_type_t     type;
    nav_link_flags_t    flags;
    nav_traversal_t     *traversal;
    nav_edict_t         *edict;
} nav_link_t;

// cached entity data
typedef struct nav_edict_s {
    nav_link_t      *link;
    int             model;
    vec3_t          mins;
    vec3_t          maxs;
    const edict_t   *game_edict;
} nav_edict_t;

// navigation context; holds data specific to pathing.
// these can be re-used between level loads, but are
// automatically freed when the level changes.
// a NULL context can be passed to any functions expecting
// one, which will refer to a built-in context instead.
typedef struct {
    const nav_node_t  *node;
    float             f_score;
    list_t            entry;
} nav_open_t;

typedef struct {
    // TODO: min-heap or priority queue ordered by f_score?
    // currently using linked list which is a bit slow for insertion
    nav_open_t  *open_set;
    list_t      open_set_head, open_set_free;

    // TODO: figure out a way to get rid of "came_from"
    // and track start -> end off the bat
    uint16_t    *came_from, *went_to;
    float       *g_score;
} nav_ctx_t;

// wrapper for PathRequest that includes our
// additional data
typedef struct {
    const PathRequest   *request;
    PathInfo            *info;
    nav_ctx_t           *ctx;
    vec3_t              *points;
    int                 maxPoints;
    const nav_node_t    *start, *goal;
} nav_path_t;

static struct {
    uint32_t    num_nodes;
    uint32_t    num_links;
    uint32_t    num_traversals;
    uint32_t    num_edicts;
    float       heuristic;

    nav_node_t      *nodes;
    nav_link_t      *links;
    nav_traversal_t *traversals;
    nav_edict_t     *edicts;

    uint32_t    num_conditional_nodes;
    nav_node_t  **conditional_nodes;

    // built-in context
    nav_ctx_t   ctx;
    bool        setup_entities;
} nav_data;

static const float NavFloorDistance = 96.0f;

static cvar_t *nav_enable;

#if NAV_DEBUG
static cvar_t *nav_debug;
static cvar_t *nav_debug_range;
#endif

static void Nav_AllocContext(nav_ctx_t *ctx)
{
    ctx->g_score   = Z_TagMalloc(sizeof(ctx->g_score  [0]) * nav_data.num_nodes, TAG_NAV);
    ctx->came_from = Z_TagMalloc(sizeof(ctx->came_from[0]) * nav_data.num_nodes, TAG_NAV);
    ctx->went_to   = Z_TagMalloc(sizeof(ctx->went_to  [0]) * nav_data.num_nodes, TAG_NAV);
    ctx->open_set  = Z_TagMalloc(sizeof(ctx->open_set [0]) * nav_data.num_nodes, TAG_NAV);
}

static void Nav_ReadVector(sizebuf_t *b, vec3_t v)
{
    v[0] = SZ_ReadFloat(b);
    v[1] = SZ_ReadFloat(b);
    v[2] = SZ_ReadFloat(b);
}

#define NAV_VERIFY(condition, error) \
    if (!(condition)) { err = error; goto fail; }

bool Nav_Load(void)
{
    if (nav_data.nodes)
        return true;
    if (!sv.cm.cache)
        return false; // no real map
    if (!nav_enable->integer)
        return false;

    const char *err = NULL;

    char filename[MAX_QPATH];
    if (Q_snprintf(filename, sizeof(filename), "bots/navigation/%s.nav", sv.name) >= sizeof(filename))
        return false;

    void *data;
    int len = FS_LoadFile(filename, &data);
    if (!data)
        return false;

    NAV_VERIFY(len >= 7*4, "File too small");

    sizebuf_t b;
    SZ_InitRead(&b, data, len + 1); // +1 for terminating NUL

    uint32_t v = SZ_ReadLong(&b);
    NAV_VERIFY(v == NAV_MAGIC, "Bad magic");

    v = SZ_ReadLong(&b);
    NAV_VERIFY(v >= NAV_VERSION_2 && v <= NAV_VERSION_6, "Bad version");

    nav_data.num_nodes      = SZ_ReadLong(&b);
    nav_data.num_links      = SZ_ReadLong(&b);
    nav_data.num_traversals = SZ_ReadLong(&b);
    nav_data.heuristic      = SZ_ReadFloat(&b);
    NAV_VERIFY(nav_data.num_nodes > 0, "No nodes");
    NAV_VERIFY(nav_data.num_links > 0, "No links");
    NAV_VERIFY(nav_data.num_nodes <= INVALID_ID, "Too many nodes");
    NAV_VERIFY(nav_data.num_links <= INVALID_ID, "Too many links");
    NAV_VERIFY(nav_data.num_traversals <= INVALID_ID, "Too many traversals");

    nav_data.nodes      = Z_TagMalloc(sizeof(nav_data.nodes[0]) * nav_data.num_nodes, TAG_NAV);
    nav_data.links      = Z_TagMalloc(sizeof(nav_data.links[0]) * nav_data.num_links, TAG_NAV);
    nav_data.traversals = Z_TagMalloc(sizeof(nav_data.traversals[0]) * nav_data.num_traversals, TAG_NAV);

    nav_data.num_conditional_nodes = 0;

    for (int i = 0; i < nav_data.num_nodes; i++) {
        nav_node_t *node = nav_data.nodes + i;

        node->id        = i;
        node->flags     = SZ_ReadShort(&b);
        node->num_links = SZ_ReadShort(&b);
        int first_link  = SZ_ReadShort(&b);
        NAV_VERIFY(first_link != -1, "Read past end of file");
        NAV_VERIFY(first_link + node->num_links <= nav_data.num_links, "Bad node links");
        node->links     = &nav_data.links[first_link];
        node->radius    = SZ_ReadShort(&b);

        if (node->flags & NodeFlag_ConditionalMask)
            nav_data.num_conditional_nodes++;
    }

    nav_data.conditional_nodes = Z_TagMalloc(sizeof(nav_data.conditional_nodes[0]) * nav_data.num_conditional_nodes, TAG_NAV);

    for (int i = 0, c = 0; i < nav_data.num_nodes; i++) {
        nav_node_t *node = nav_data.nodes + i;

        Nav_ReadVector(&b, node->origin);

        if (node->flags & NodeFlag_ConditionalMask)
            nav_data.conditional_nodes[c++] = node;
    }

    for (int i = 0; i < nav_data.num_links; i++) {
        nav_link_t *link = nav_data.links + i;

        int target   = SZ_ReadShort(&b);
        NAV_VERIFY(target < nav_data.num_nodes, "Bad link target");
        link->target = &nav_data.nodes[target];
        link->type   = SZ_ReadByte(&b);
        link->flags  = SZ_ReadByte(&b);

        if (v < NAV_VERSION_3)
            link->flags = NavLinkFlag_AllTeams;
        // strip old green/yellow team flags
        else if (v < NAV_VERSION_6)
            link->flags &= ~(BIT(2) | BIT(3));

        link->traversal = NULL;
        link->edict     = NULL;
        int traversal   = SZ_ReadShort(&b);
        if (traversal != INVALID_ID) {
            NAV_VERIFY(traversal < nav_data.num_traversals, "Bad link traversal");
            link->traversal = &nav_data.traversals[traversal];
        }
    }

    for (int i = 0; i < nav_data.num_traversals; i++) {
        nav_traversal_t *traversal = nav_data.traversals + i;

        Nav_ReadVector(&b, traversal->funnel);
        Nav_ReadVector(&b, traversal->start);
        Nav_ReadVector(&b, traversal->end);

        if (v >= NAV_VERSION_4)
            Nav_ReadVector(&b, traversal->ladder_plane);
    }

    nav_data.num_edicts = SZ_ReadLong(&b);
    NAV_VERIFY(nav_data.num_edicts <= MAX_EDICTS, "Too many edicts");

    nav_data.edicts = Z_TagMalloc(sizeof(nav_data.edicts[0]) * nav_data.num_edicts, TAG_NAV);

    for (int i = 0; i < nav_data.num_edicts; i++) {
        nav_edict_t *edict = nav_data.edicts + i;

        int link = SZ_ReadShort(&b);
        NAV_VERIFY(link < nav_data.num_links, "Bad edict link");
        edict->link = &nav_data.links[link];
        edict->link->edict = edict;
        edict->game_edict = NULL;
        edict->model = SZ_ReadLong(&b) - 1; // inline models start at modelindex 1 now
        Nav_ReadVector(&b, edict->mins);
        Nav_ReadVector(&b, edict->maxs);
    }

    // must not have consumed terminating NUL
    NAV_VERIFY(b.readcount < b.cursize, "Read past end of file");

    Com_Printf("Loaded %s (version %d): %u nodes, %u links, %u traversals, %u edicts\n",
               filename, v, nav_data.num_nodes, nav_data.num_links, nav_data.num_traversals, nav_data.num_edicts);

    FS_FreeFile(data);
    Nav_AllocContext(&nav_data.ctx);
    return true;

fail:
    FS_FreeFile(data);
    Com_Printf("Couldn't load %s: %s\n", filename, err);
    Nav_Unload();
    return false;
}

void Nav_Unload(void)
{
    Z_FreeTags(TAG_NAV);
    memset(&nav_data, 0, sizeof(nav_data));
}

// built-in path functions
static float Nav_Heuristic(const nav_path_t *path, const nav_node_t *node)
{
    return DistanceSquared(path->goal->origin, node->origin);
}

static float Nav_Weight(const nav_path_t *path, const nav_node_t *node, const nav_link_t *link)
{
    if (link->type == NavLinkType_Teleport)
        return 1.0f;

    return DistanceSquared(node->origin, link->target->origin);
}

static bool Nav_NodeAccessible(const nav_path_t *path, const nav_node_t *node)
{
    if (node->flags & NodeFlag_Disabled)
        return false;

    if (path->request->nodeSearch.ignoreNodeFlags)
        return !(node->flags & NodeFlag_NoPOI);

    if (node->flags & (NodeFlag_NoMonsters | NodeFlag_Crouch | NodeFlag_Ladder | NodeFlag_Elevator | NodeFlag_Pusher | NodeFlag_Teleporter))
        return false;

    if ((node->flags & NodeFlag_UnderWater) && !(path->request->pathFlags & PathFlags_Water))
        return false;

    if (!(node->flags & NodeFlag_UnderWater) && !(path->request->pathFlags & PathFlags_Walk))
        return false;

    return true;
}

static bool Nav_LinkAccessible(const nav_path_t *path, const nav_node_t *node, const nav_link_t *link)
{
    const PathRequest *req = path->request;
    const nav_traversal_t *trv = link->traversal;

    if (!Nav_NodeAccessible(path, link->target))
        return false;

    if (req->nodeSearch.ignoreNodeFlags)
        return true;

    if (link->edict) {
        const edict_t *e = link->edict->game_edict;
        if (e && e->r.inuse && e->s.modelindex == link->edict->model && !(e->r.svflags & SVF_DOOR))
            return false;
    }

    switch (link->type) {
    case NavLinkType_Walk:
        return true;

    case NavLinkType_LongJump:
        return req->pathFlags & PathFlags_LongJump;

    case NavLinkType_WalkOffLedge:
        if (!(req->pathFlags & PathFlags_WalkOffLedge))
            return false;
        if (trv && trv->start[2] - trv->end[2] > req->traversals.dropHeight)
            return false;
        return true;

    case NavLinkType_BarrierJump:
        if (!(req->pathFlags & PathFlags_BarrierJump))
            return false;
        if (trv && trv->end[2] - trv->start[2] > req->traversals.jumpHeight)
            return false;
        return true;

    default:
        return false;
    }
}

static const nav_node_t *Nav_ClosestNodeTo(nav_path_t *path, const vec3_t p)
{
    const PathRequest *req = path->request;
    float w = INFINITY;
    const nav_node_t *c = NULL;

    float min_z = p[2] - req->nodeSearch.minHeight;
    float max_z = p[2] + req->nodeSearch.maxHeight;

    for (int i = 0; i < nav_data.num_nodes; i++) {
        const nav_node_t *node = &nav_data.nodes[i];
        if (!Nav_NodeAccessible(path, node))
            continue;

        if (node->origin[2] < min_z || node->origin[2] > max_z)
            continue;

        vec2_t d;
        Vector2Subtract(node->origin, p, d);

        float l = Vector2Length(d);
        if (l > req->nodeSearch.radius)
            continue;

        vec3_t end = { 0, 0, 32 };
        VectorAdd(end, node->origin, end);
        trace_t tr;
        SV_Trace(&tr, p, NULL, NULL, end, ENTITYNUM_NONE,
                 MASK_SOLID | CONTENTS_PLAYERCLIP | CONTENTS_MONSTERCLIP);
        if (tr.fraction < 1.0f)
            continue;

        if (l < w) {
            w = l;
            c = node;
        }
    }

    return c;
}

static const nav_link_t *Nav_GetLink(const nav_node_t *a, const nav_node_t *b)
{
    for (const nav_link_t *link = a->links; link != a->links + a->num_links; link++)
        if (link->target == b)
            return link;

    return NULL;
}

static bool Nav_TouchingNode(const vec3_t pos, float move_dist, const nav_node_t *node)
{
    return Distance(node->origin, pos) <= move_dist;
}

static bool Nav_NodeReached(const vec3_t pos, const nav_node_t *node)
{
    vec3_t d;
    VectorSubtract(node->origin, pos, d);

    return Vector2Length(d) <= node->radius && fabsf(d[2]) <= 64;
}

static void Nav_PushOpenSet(nav_ctx_t *ctx, const nav_node_t *node, float f)
{
    if (LIST_EMPTY(&ctx->open_set_free))
        return;

    // grab free entry
    nav_open_t *o = LIST_FIRST(nav_open_t, &ctx->open_set_free, entry);
    List_Remove(&o->entry);

    o->node = node;
    o->f_score = f;

    nav_open_t *open_where;
    LIST_FOR_EACH(open_where, &ctx->open_set_head, entry)
        if (f < open_where->f_score)
            break;

    List_Append(&open_where->entry, &o->entry);
}

#define PATH_POINT_TOO_CLOSE (64 * 64)

static void Nav_ReachedGoal(nav_path_t *path, int current)
{
    const PathRequest *request = path->request;
    PathInfo *info = path->info;
    nav_ctx_t *ctx = path->ctx;
    int num_points = 0;

    // reverse the order of came_from into went_to
    // to make stuff below a bit easier to work with
    int n = current;
    while (ctx->came_from[n] != INVALID_ID) {
        num_points++;
        n = ctx->came_from[n];
    }

    n = current;
    int p = 0;
    while (ctx->came_from[n] != INVALID_ID) {
        n = ctx->went_to[num_points - p - 1] = ctx->came_from[n];
        p++;
    }

    // num_points now contains points between start
    // and current; it will be at least 1, since start can't
    // be the same as end, but may be less once we start clipping.
    Q_assert(num_points >= 1);
    Q_assert(ctx->went_to[0] != INVALID_ID);

    int first_point = 0;
    const nav_link_t *link = NULL;

    if (num_points > 1) {
        link = Nav_GetLink(&nav_data.nodes[ctx->went_to[0]], &nav_data.nodes[ctx->went_to[1]]);
        Q_assert(link);

        if (!request->nodeSearch.ignoreNodeFlags) {
            // if the node isn't a traversal, we may want
            // to skip the first node if we're either past it
            // or touching it
            if (!link->traversal) {
                if (Nav_NodeReached(request->start, &nav_data.nodes[ctx->went_to[0]])) {
                    first_point++;
                }
            }
        }
    }

    // store resulting path for compass, etc
    if (path->maxPoints) {
        // if we're too far from the first node, add in our current position.
        float dist = DistanceSquared(request->start, nav_data.nodes[ctx->went_to[first_point]].origin);

        if (dist > PATH_POINT_TOO_CLOSE) {
            if (info->numPathPoints < path->maxPoints)
                VectorCopy(request->start, path->points[info->numPathPoints]);
            info->numPathPoints++;
        }

        // crawl forwards and add nodes
        for (p = first_point; p < num_points; p++) {
            if (info->numPathPoints < path->maxPoints)
                VectorCopy(nav_data.nodes[ctx->went_to[p]].origin, path->points[info->numPathPoints]);
            info->numPathPoints++;
        }

        // add the end point if we have room
        dist = DistanceSquared(request->goal, nav_data.nodes[ctx->went_to[current]].origin);

        if (dist > PATH_POINT_TOO_CLOSE) {
            if (info->numPathPoints < path->maxPoints)
                VectorCopy(request->goal, path->points[info->numPathPoints]);
            info->numPathPoints++;
        }
    }

    if (request->nodeSearch.ignoreNodeFlags) {
        info->returnCode = PathReturnCode_RawPathFound;
        return;
    }

    // store move point info
    if (link && link->traversal) {
        VectorCopy(link->traversal->start, info->firstMovePoint);
        VectorCopy(link->traversal->end, info->secondMovePoint);
        info->returnCode = PathReturnCode_TraversalPending;
    } else {
        VectorCopy(nav_data.nodes[ctx->went_to[first_point]].origin, info->firstMovePoint);
        if (first_point + 1 < num_points)
            VectorCopy(nav_data.nodes[ctx->went_to[first_point + 1]].origin, info->secondMovePoint);
        else
            VectorCopy(request->goal, info->secondMovePoint);
        info->returnCode = PathReturnCode_InProgress;
    }
}

static void Nav_Path(nav_path_t *path)
{
    const PathRequest *request = path->request;
    PathInfo *info = path->info;
    nav_ctx_t *ctx = path->ctx;

    memset(info, 0, sizeof(*info));

    if (!nav_data.nodes) {
        info->returnCode = PathReturnCode_NoNavAvailable;
        return;
    }

    if (!(request->pathFlags & (PathFlags_Walk | PathFlags_Water))) {
        info->returnCode = PathReturnCode_MissingWalkOrSwimFlag;
        return;
    }

    path->start = Nav_ClosestNodeTo(path, request->start);
    if (!path->start) {
        info->returnCode = PathReturnCode_NoStartNode;
        return;
    }

    path->goal = Nav_ClosestNodeTo(path, request->goal);
    if (!path->goal) {
        info->returnCode = PathReturnCode_NoGoalNode;
        return;
    }

    if (path->start == path->goal || Nav_TouchingNode(request->start, request->moveDist, path->goal)) {
        info->returnCode = PathReturnCode_ReachedGoal;
        VectorCopy(request->goal, info->firstMovePoint);
        VectorCopy(request->goal, info->secondMovePoint);
        return;
    }

    if (!request->nodeSearch.ignoreNodeFlags) {
        if (SV_PointContents(request->start) & MASK_SOLID) {
            info->returnCode = PathReturnCode_InvalidStart;
            return;
        }
        if (SV_PointContents(request->goal) & MASK_SOLID) {
            info->returnCode = PathReturnCode_InvalidGoal;
            return;
        }
    }

    int start_id = path->start->id;
    int goal_id = path->goal->id;

    for (int i = 0; i < nav_data.num_nodes; i++)
        ctx->g_score[i] = INFINITY;

    List_Init(&ctx->open_set_head);
    List_Init(&ctx->open_set_free);

    for (int i = 0; i < nav_data.num_nodes; i++)
        List_Append(&ctx->open_set_free, &ctx->open_set[i].entry);

    ctx->came_from[start_id] = INVALID_ID;
    ctx->g_score[start_id] = 0;
    Nav_PushOpenSet(ctx, path->start, Nav_Heuristic(path, path->start));

    while (true) {
        nav_open_t *cursor = LIST_FIRST(nav_open_t, &ctx->open_set_head, entry);

        // end of open set
        if (LIST_TERM(cursor, &ctx->open_set_head, entry))
            break;

        // shift off the head, insert into free
        List_Remove(&cursor->entry);
        List_Insert(&ctx->open_set_free, &cursor->entry);

        int current = cursor->node->id;

        if (current == goal_id) {
            Nav_ReachedGoal(path, current);
            return;
        }

        const nav_node_t *current_node = &nav_data.nodes[current];

        for (int i = 0; i < current_node->num_links; i++) {
            const nav_link_t *link = &current_node->links[i];

            if (!Nav_LinkAccessible(path, current_node, link))
                continue;

            int target_id = link->target->id;

            float temp_g_score = ctx->g_score[current] + Nav_Weight(path, current_node, link);

            if (temp_g_score >= ctx->g_score[target_id])
                continue;

            ctx->came_from[target_id] = current;
            ctx->g_score[target_id] = temp_g_score;

            Nav_PushOpenSet(ctx, link->target, temp_g_score + Nav_Heuristic(path, link->target));
        }
    }

    info->returnCode = PathReturnCode_NoPathFound;
}

static void Nav_DebugPath(const nav_path_t *path)
{
#if NAV_DEBUG
    const PathRequest *request = path->request;
    const PathInfo *info = path->info;
    uint32_t time = request->debugging.drawTime * 1000;

    R_AddDebugSphere(request->start, 8.0f, U32_YELLOW, time, false);
    R_AddDebugSphere(request->goal, 8.0f, U32_YELLOW, time, false);

    int count = min(info->numPathPoints, path->maxPoints);

    if (count > 0) {
        R_AddDebugArrow(request->start, path->points[0],
                        8.0f, U32_YELLOW, U32_YELLOW, time, false);

        for (int i = 0; i < count - 1; i++)
            R_AddDebugArrow(path->points[i], path->points[i + 1],
                            8.0f, U32_YELLOW, U32_YELLOW, time, false);

        R_AddDebugArrow(path->points[count - 1], request->goal,
                        8.0f, U32_YELLOW, U32_YELLOW, time, false);
    } else {
        R_AddDebugArrow(request->start, request->goal, 8.0f, U32_YELLOW, U32_YELLOW, time, false);
    }

    if (info->returnCode == PathReturnCode_TraversalPending || info->returnCode == PathReturnCode_InProgress) {
        R_AddDebugSphere(info->firstMovePoint, 16.0f, U32_RED, time, false);
        R_AddDebugArrow(info->firstMovePoint, info->secondMovePoint, 16.0f, U32_RED, U32_RED, time, false);
    }
#endif
}

bool Nav_GetPathToGoal(const PathRequest *request, PathInfo *info, vec3_t *points, int maxPoints)
{
    nav_path_t path = {
        .request = request,
        .info = info,
        .ctx = &nav_data.ctx,
        .points = points,
        .maxPoints = maxPoints,
    };
    Nav_Path(&path);

    if (request->debugging.drawTime > 0)
        Nav_DebugPath(&path);

    return info->returnCode < PathReturnCode_StartPathErrors;
}

static void Nav_GetNodeBounds(const nav_node_t *node, vec3_t mins, vec3_t maxs)
{
    VectorSet(mins, -16, -16, -24);
    VectorSet(maxs, 16, 16, 32);

    if (node->flags & NodeFlag_Crouch)
        maxs[2] = 4.0f;
}

static void Nav_GetNodeTraceOrigin(const nav_node_t *node, vec3_t origin)
{
    VectorCopy(node->origin, origin);
    origin[2] += 24.0f;
}

#if NAV_DEBUG

#define A_RED       MakeColor(255,   0,   0, alpha)
#define A_GREEN     MakeColor(  0, 255,   0, alpha)
#define A_BLUE      MakeColor(  0,   0, 255, alpha)
#define A_YELLOW    MakeColor(255, 255,   0, alpha)
#define A_CYAN      MakeColor(  0, 255, 255, alpha)
#define A_WHITE     MakeColor(255, 255, 255, alpha)

static void Nav_DrawLink(const nav_node_t *node, const nav_link_t *link, int alpha)
{
    const nav_link_t *other_link = Nav_GetLink(link->target, node);
    const bool link_disabled = (node->flags | link->target->flags) & NodeFlag_Disabled;

    vec3_t s, e;
    Nav_GetNodeTraceOrigin(node, s);
    Nav_GetNodeTraceOrigin(link->target, e);

    if (other_link) {
        // two-way link
        if (node->id < link->target->id)
            return;

        // simple link
        if (!link->traversal && !other_link->traversal) {
            R_AddDebugLine(s, e, link_disabled ? A_RED : A_WHITE, sv.frametime, true);
        } else {
            // one or both are traversals
            // render a->b
            if (!link->traversal) {
                R_AddDebugArrow(s, e, 8.0f, link_disabled ? A_RED : A_WHITE, A_RED, sv.frametime, true);
            } else {
                vec3_t ctrl;

                if (s[2] > e[2]) {
                    VectorCopy(e, ctrl);
                    ctrl[2] = s[2];
                } else {
                    VectorCopy(s, ctrl);
                    ctrl[2] = e[2];
                }

                R_AddDebugCurveArrow(s, ctrl, e, 8.0f, link_disabled ? A_RED : A_BLUE, A_RED, sv.frametime, true);
            }

            // render b->a
            if (!other_link->traversal) {
                R_AddDebugArrow(e, s, 8.0f, link_disabled ? A_RED : A_WHITE, A_RED, sv.frametime, true);
            } else {
                vec3_t ctrl;

                if (s[2] > e[2]) {
                    VectorCopy(e, ctrl);
                    ctrl[2] = s[2];
                } else {
                    VectorCopy(s, ctrl);
                    ctrl[2] = e[2];
                }

                // raise the other side's points slightly
                s[2] += 32;
                ctrl[2] += 32;
                e[2] += 32;

                R_AddDebugCurveArrow(e, ctrl, s, 8.0f, link_disabled ? A_RED : A_BLUE, A_RED, sv.frametime, true);

                s[2] -= 32;
                e[2] -= 32;
            }
        }
    } else {
        // one-way link
        if (link->traversal) {
            vec3_t ctrl;

            if (s[2] > e[2]) {
                VectorCopy(e, ctrl);
                ctrl[2] = s[2];
            } else {
                VectorCopy(s, ctrl);
                ctrl[2] = e[2];
            }

            R_AddDebugCurveArrow(s, ctrl, e, 8.0f, link_disabled ? A_RED : A_BLUE, A_RED, sv.frametime, true);
        } else {
            R_AddDebugArrow(s, e, 8.0f, link_disabled ? A_RED : A_CYAN, A_RED, sv.frametime, true);
        }
    }

    if (link->edict) {
        const edict_t *e = link->edict->game_edict;
        if (e && e->r.inuse) {
            vec3_t mid;
            VectorAvg(e->r.absmin, e->r.absmax, mid);
            R_AddDebugArrow(s, mid, 8.0f, A_YELLOW, A_CYAN, sv.frametime, true);
        }
    }
}

static const char *const nodeflags[] = {
    "TELEPORTER", "PUSHER", "ELEVATOR", "LADDER", "UNDERWATER", "CHECK HAZARD",
    "CHECK FLOOR", "CHECK SOLID", "NO MOBS", "CROUCH", "NO POI", "CHECK LIQUID",
    "CHECK DOORS", "DISABLED"
};

static void Nav_DrawNode(const nav_node_t *node)
{
    float dist = Distance(node->origin, svs.edicts[0].s.origin);

    if (dist > nav_debug_range->value)
        return;

    int alpha = Q_clip_uint8((1.0f - ((dist - 32) / (nav_debug_range->value - 32))) * 255);

    R_AddDebugCircle(node->origin, node->radius, A_CYAN, sv.frametime, true);

    vec3_t mins, maxs, origin;
    Nav_GetNodeBounds(node, mins, maxs);
    Nav_GetNodeTraceOrigin(node, origin);

    VectorAdd(mins, origin, mins);
    VectorAdd(maxs, origin, maxs);

    R_AddDebugBounds(mins, maxs, (node->flags & NodeFlag_Disabled) ? A_RED : A_YELLOW, sv.frametime, true);

    if (node->flags & NodeFlag_CheckHasFloor) {
        vec3_t floormins, floormaxs;
        VectorCopy(mins, floormins);
        VectorCopy(maxs, floormaxs);

        float mins_z = floormins[2];
        floormins[2] = origin[2] - NavFloorDistance;
        floormaxs[2] = mins_z;

        R_AddDebugBounds(floormins, floormaxs, A_RED, sv.frametime, true);
    }

    R_AddDebugLine(node->origin, origin, A_CYAN, sv.frametime, true);

    origin[2] += 40;
    R_AddDebugText(origin, NULL, va("%d", node->id), 5.0f, A_CYAN, sv.frametime, true);

    char node_text_buffer[128];
    *node_text_buffer = 0;

    for (int i = 0; i < q_countof(nodeflags); i++)
        if (node->flags & BIT(i))
            Q_strlcat(node_text_buffer, va("%s\n", nodeflags[i]), sizeof(node_text_buffer));

    if (*node_text_buffer) {
        origin[2] -= 18;
        R_AddDebugText(origin, NULL, node_text_buffer, 2.5f, A_GREEN, sv.frametime, true);
    }

    for (int i = 0; i < node->num_links; i++)
        Nav_DrawLink(node, &node->links[i], alpha);
}

static void Nav_Debug(void)
{
    if (!nav_debug->integer)
        return;

    R_ClearDebugLines();

    for (int i = 0; i < nav_data.num_nodes; i++)
        Nav_DrawNode(&nav_data.nodes[i]);
}

#endif

static bool line_intersects_box(const vec3_t start, const vec3_t end, const vec3_t mins, const vec3_t maxs)
{
    float enter = 0.0f;
    float leave = 1.0f;

    for (int i = 0; i < 3; i++) {
        float rd = 1.0f / (end[i] - start[i]);
        float t0 = (mins[i] - start[i]) * rd;
        float t1 = (maxs[i] - start[i]) * rd;
        float tmin = min(t0, t1);
        float tmax = max(t0, t1);
        enter = max(enter, tmin);
        leave = min(leave, tmax);
    }

    return enter < leave;
}

static bool boxes_intersect(const vec3_t mins1, const vec3_t maxs1, const vec3_t mins2, const vec3_t maxs2)
{
    for (int i = 0; i < 3; i++) {
        if (mins1[i] > maxs2[i])
            return false;
        if (maxs1[i] < mins2[i])
            return false;
    }

    return true;
}

static void Nav_UpdateConditionalNode(nav_node_t *node)
{
    trace_t tr;

    node->flags &= ~NodeFlag_Disabled;

    vec3_t mins, maxs, origin;
    Nav_GetNodeBounds(node, mins, maxs);
    Nav_GetNodeTraceOrigin(node, origin);

    if (node->flags & NodeFlag_CheckInSolid) {
        SV_Trace(&tr, origin, mins, maxs, origin, ENTITYNUM_NONE, MASK_SOLID);

        if (tr.startsolid || tr.allsolid) {
            node->flags |= NodeFlag_Disabled;
            return;
        }
    }

    if (node->flags & NodeFlag_CheckInLiquid) {
        SV_Trace(&tr, origin, mins, maxs, origin, ENTITYNUM_NONE, MASK_WATER);

        if (!(tr.startsolid || tr.allsolid)) {
            node->flags |= NodeFlag_Disabled;
            return;
        }
    }

    if (node->flags & NodeFlag_CheckForHazard) {
        SV_Trace(&tr, origin, mins, maxs, origin, ENTITYNUM_NONE, CONTENTS_SLIME | CONTENTS_LAVA);

        if (tr.startsolid || tr.allsolid) {
            node->flags |= NodeFlag_Disabled;
            return;
        }

        vec3_t absmin, absmax;
        VectorAdd(origin, mins, absmin);
        VectorAdd(origin, maxs, absmax);

        for (int i = svs.maxclients; i < svs.num_edicts; i++) {
            const edict_t *e = SV_EdictForNum(i);

            if (!e->r.inuse)
                continue;

            if (e->r.svflags & SVF_LASER_FIELD) {
                if (e->r.svflags & SVF_NOCLIENT)
                    continue;

                if (line_intersects_box(e->s.old_origin, e->s.origin, absmin, absmax)) {
                    node->flags |= NodeFlag_Disabled;
                    return;
                }
            } else if (e->r.svflags & SVF_TRAP) {
                if (boxes_intersect(e->r.absmin, e->r.absmax, absmin, absmax)) {
                    node->flags |= NodeFlag_Disabled;
                    return;
                }
            }
        }
    }

    if (node->flags & NodeFlag_CheckHasFloor) {
        vec3_t flat_mins = { mins[0], mins[1] };
        vec3_t flat_maxs = { maxs[0], maxs[1] };

        vec3_t floor_end;
        VectorCopy(origin, floor_end);
        floor_end[2] -= NavFloorDistance;

        SV_Trace(&tr, origin, flat_mins, flat_maxs, floor_end, ENTITYNUM_NONE, MASK_SOLID);

        if (tr.fraction == 1.0f) {
            node->flags |= NodeFlag_Disabled;
            return;
        }
    }

    if (node->flags & NodeFlag_CheckDoorLinks) {
        for (int i = 0; i < node->num_links; i++) {
            const nav_link_t *link = &node->links[i];

            if (!link->edict)
                continue;

            const edict_t *edict = link->edict->game_edict;
            if (!edict)
                continue;
            if (!edict->r.inuse)
                continue;
            if (edict->s.modelindex != link->edict->model)
                continue;

            if (edict->r.svflags & SVF_LOCKED) {
                node->flags |= NodeFlag_Disabled;
                return;
            }
        }
    }
}

static void Nav_SetupEntities(void)
{
    for (int i = 0; i < nav_data.num_edicts; i++) {
        nav_edict_t *e = &nav_data.edicts[i];

        for (int n = svs.maxclients; n < svs.num_edicts; n++) {
            const edict_t *game_e = SV_EdictForNum(n);

            if (!game_e->r.inuse)
                continue;
            if (game_e->r.solid != SOLID_TRIGGER && game_e->r.solid != SOLID_BSP)
                continue;

            if (game_e->s.modelindex == e->model) {
                e->game_edict = game_e;
                break;
            }
        }

        if (!e->game_edict)
            Com_Printf("Nav entity %i appears to be missing (needs entity with model %i)\n", i, e->model);
    }
}

void Nav_Frame(void)
{
    if (!nav_data.setup_entities && sv.time >= 1000) {
        Nav_SetupEntities();
        nav_data.setup_entities = true;
    }

    for (int i = 0; i < nav_data.num_conditional_nodes; i++)
        Nav_UpdateConditionalNode(nav_data.conditional_nodes[i]);

#if NAV_DEBUG
    Nav_Debug();
#endif
}

void Nav_Register(void)
{
    nav_enable = Cvar_Get("nav_enable", "1", 0);
#if NAV_DEBUG
    nav_debug = Cvar_Get("nav_debug", "0", 0);
    nav_debug_range = Cvar_Get("nav_debug_range", "512", 0);
#endif
}
