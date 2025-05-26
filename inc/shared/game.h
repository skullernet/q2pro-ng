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

#pragma once

//
// game.h -- game dll information visible to server
//

#define GAME_API_VERSION    4000

// edict->svflags

#define SVF_NONE                0U
#define SVF_NOCLIENT            BIT(0)      // don't send entity to clients, even if it has effects
#define SVF_DEADMONSTER         BIT(1)      // treat as CONTENTS_DEADMONSTER for collision
#define SVF_MONSTER             BIT(2)      // only used by server as entity priority hint
#define SVF_PLAYER              BIT(3)      // treat as CONTENTS_PLAYER for collision
#define SVF_BOT                 BIT(4)
#define SVF_NOBOTS              BIT(5)
#define SVF_RESPAWNING          BIT(6)
#define SVF_PROJECTILE          BIT(7)      // treat as CONTENTS_PROJECTILE for collision
#define SVF_INSTANCED           BIT(8)
#define SVF_DOOR                BIT(9)
#define SVF_NOCULL              BIT(10)     // always send entity to clients (no PVS checks)
#define SVF_HULL                BIT(11)
#define SVF_LOCKED              BIT(12)
#define SVF_LASER_FIELD         BIT(13)
#define SVF_TRAP                BIT(14)

// edict->solid values
typedef enum {
    SOLID_NOT,          // no interaction with other objects
    SOLID_TRIGGER,      // only touch when inside, after moving
    SOLID_BBOX,         // touch on edge
    SOLID_BSP           // bsp clip, touch on edge
} solid_t;

// flags for inVIS()
typedef enum {
    VIS_PVS     = 0,
    VIS_PHS     = 1,
    VIS_NOAREAS = 2     // can be OR'ed with one of above
} vis_t;

//===============================================================

typedef enum {
    PathReturnCode_ReachedGoal,             // we're at our destination
    PathReturnCode_ReachedPathEnd,          // we're as close to the goal as we can get with a path
    PathReturnCode_TraversalPending,        // the upcoming path segment is a traversal
    PathReturnCode_RawPathFound,            // user wanted ( and got ) just a raw path ( no processing )
    PathReturnCode_InProgress,              // pathing in progress
    PathReturnCode_StartPathErrors,         // any code after this one indicates an error of some kind.
    PathReturnCode_InvalidStart,            // start position is invalid.
    PathReturnCode_InvalidGoal,             // goal position is invalid.
    PathReturnCode_NoNavAvailable,          // no nav file available for this map.
    PathReturnCode_NoStartNode,             // can't find a nav node near the start position
    PathReturnCode_NoGoalNode,              // can't find a nav node near the goal position
    PathReturnCode_NoPathFound,             // can't find a path from the start to the goal
    PathReturnCode_MissingWalkOrSwimFlag    // MUST have at least Walk or Water path flags set!
} PathReturnCode;

typedef enum {
    PathLinkType_Walk,          // can walk between the path points
    PathLinkType_WalkOffLedge,  // will walk off a ledge going between path points
    PathLinkType_LongJump,      // will need to perform a long jump between path points
    PathLinkType_BarrierJump,   // will need to jump over a low barrier between path points
    PathLinkType_Elevator       // will need to use an elevator between path points
} PathLinkType;

typedef enum {
    PathFlags_All             = -1,
    PathFlags_Water           = BIT(0), // swim to your goal ( useful for fish/gekk/etc. )
    PathFlags_Walk            = BIT(1), // walk to your goal
    PathFlags_WalkOffLedge    = BIT(2), // allow walking over ledges
    PathFlags_LongJump        = BIT(3), // allow jumping over gaps
    PathFlags_BarrierJump     = BIT(4), // allow jumping over low barriers
    PathFlags_Elevator        = BIT(5)  // allow using elevators
} PathFlags;

typedef struct {
    vec3_t      start;
    vec3_t      goal;
    PathFlags   pathFlags;
    float       moveDist;

    struct DebugSettings {
        float   drawTime; // if > 0, how long ( in seconds ) to draw path in world
    } debugging;

    struct NodeSettings {
        bool    ignoreNodeFlags; // true = ignore node flags when considering nodes
        float   minHeight;       // 0 <= use default values
        float   maxHeight;       // 0 <= use default values
        float   radius;          // 0 <= use default values
    } nodeSearch;

    struct TraversalSettings {
        float dropHeight;   // 0 = don't drop down
        float jumpHeight;   // 0 = don't jump up
    } traversals;

#if 0
    struct PathArray {
        vec3_t  *posArray;  // array to store raw path points
        int     count;      // number of elements in array
    } pathPoints;
#endif
} PathRequest;

typedef struct {
    int             numPathPoints;
    float           pathDistSqr;
    vec3_t          firstMovePoint;
    vec3_t          secondMovePoint;
    PathLinkType    pathLinkType;
    PathReturnCode  returnCode;
} PathInfo;

//===============================================================

typedef struct edict_s edict_t;
typedef struct gclient_s gclient_t;

typedef struct {
    bool        inuse;
    bool        linked;
    int         linkcount;
    int         areanum, areanum2;
    int         svflags;            // SVF_NOCLIENT, SVF_DEADMONSTER, SVF_MONSTER, etc
    vec3_t      mins, maxs;
    vec3_t      absmin, absmax, size;
    solid_t     solid;
    int         ownernum;
} entity_shared_t;

#ifndef GAME_INCLUDE

struct gclient_s {
    player_state_t      ps;     // communicated by server to clients
    int                 ping;
};

struct edict_s {
    entity_state_t  s;
    entity_shared_t r;
};

#endif  // GAME_INCLUDE

//===============================================================

//
// functions provided by the main engine
//
typedef struct {
    uint32_t    apiversion;
    uint32_t    structsize;

    // special messages
    void (*Print)(print_type_t type, const char *msg);
    void (* q_noreturn_ptr Error)(const char *msg);

    // config strings hold all the index strings, the lightstyles,
    // and misc data like the sky definition and cdtrack.
    // All of the current configstrings are sent to clients when
    // they connect, and changes are sent to all connected clients.
    void (*SetConfigstring)(unsigned index, const char *str);
    unsigned (*GetConfigstring)(unsigned index, char *buf, unsigned size);
    int (*FindConfigstring)(const char *name, int start, int max, int skip);

    // collision detection
    void (*Trace)(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passent, contents_t contentmask);
    void (*Clip)(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int clipent, contents_t contentmask);
    contents_t (*PointContents)(const vec3_t point);
    int (*BoxEdicts)(const vec3_t mins, const vec3_t maxs, int *list, int maxcount, int areatype);

    bool (*InVis)(const vec3_t p1, const vec3_t p2, vis_t vis);
    void (*SetAreaPortalState)(unsigned portalnum, bool open);
    bool (*AreasConnected)(int area1, int area2);

    // an entity will never be sent to a client or used for collision
    // if it is not passed to linkentity.  If the size, position, or
    // solidity changes, it must be relinked.
    void (*LinkEntity)(edict_t *ent);
    void (*UnlinkEntity)(edict_t *ent);     // call before removing an interactive edict
    void (*SetBrushModel)(edict_t *ent, const char *name);

    // network messaging
    void (*ClientPrint)(edict_t *ent, print_level_t printlevel, const char *msg);
    void (*ClientLayout)(edict_t *ent, const char *str, bool reliable);
    void (*ClientStuffText)(edict_t *ent, const char *str);
    void (*ClientInventory)(edict_t *ent, int *inventory, int count);

    int (*DirToByte)(const vec3_t dir);

    void (*LocateGameData)(edict_t *edicts, unsigned edict_size, unsigned num_edicts, gclient_t *clients, unsigned client_size);
    bool (*ParseEntityString)(char *buf, unsigned size);
    unsigned (*GetLevelName)(char *buf, unsigned size);
    unsigned (*GetSpawnPoint)(char *buf, unsigned size);
    unsigned (*GetUserinfo)(unsigned clientnum, char *buf, unsigned size);
    void (*GetUsercmd)(unsigned clientnum, usercmd_t *ucmd);
    bool (*GetPathToGoal)(const PathRequest *request, PathInfo *info);

    int64_t (*RealTime)(void);
    bool (*LocalTime)(int64_t time, vm_time_t *localtime);

    // console variable interaction
    bool (*Cvar_Register)(vm_cvar_t *var, const char *name, const char *value, int flags);
    void (*Cvar_Set)(const char *name, const char *value);
    void (*Cvar_ForceSet)(const char *name, const char *value);
    int (*Cvar_VariableInteger)(const char *name);
    float (*Cvar_VariableValue)(const char *name);
    unsigned (*Cvar_VariableString)(const char *name, char *buf, unsigned size);

    // ClientCommand and ServerCommand parameter access
    int (*Argc)(void);
    unsigned (*Argv)(int arg, char *buf, unsigned size);
    unsigned (*Args)(char *buf, unsigned size);     // concatenation of all argv >= 1

    // add commands to the server console as if they were typed in
    // for map changing, etc
    void (*AddCommandString)(const char *text);

    void (*DebugGraph)(float value, int color);

    int64_t     (*FS_OpenFile)(const char *path, qhandle_t *f, unsigned mode); // returns file length
    int         (*FS_CloseFile)(qhandle_t f);
    int         (*FS_ReadFile)(void *buffer, size_t len, qhandle_t f);
    int         (*FS_WriteFile)(const void *buffer, size_t len, qhandle_t f);
    int         (*FS_FlushFile)(qhandle_t f);
    int64_t     (*FS_TellFile)(qhandle_t f);
    int         (*FS_SeekFile)(qhandle_t f, int64_t offset, int whence);
    int         (*FS_ReadLine)(qhandle_t f, char *buffer, size_t size);
    unsigned    (*FS_ErrorString)(int error, char *buf, unsigned size);

    void (*R_ClearDebugLines)(void);
    void (*R_AddDebugLine)(const vec3_t start, const vec3_t end, uint32_t color, uint32_t time, bool depth_test);
    void (*R_AddDebugPoint)(const vec3_t point, float size, uint32_t color, uint32_t time, bool depth_test);
    void (*R_AddDebugAxis)(const vec3_t origin, const vec3_t angles, float size, uint32_t time, bool depth_test);
    void (*R_AddDebugBounds)(const vec3_t mins, const vec3_t maxs, uint32_t color, uint32_t time, bool depth_test);
    void (*R_AddDebugSphere)(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test);
    void (*R_AddDebugCircle)(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test);
    void (*R_AddDebugCylinder)(const vec3_t origin, float half_height, float radius, uint32_t color, uint32_t time,
                               bool depth_test);
    void (*R_AddDebugArrow)(const vec3_t start, const vec3_t end, float size, uint32_t line_color,
                            uint32_t arrow_color, uint32_t time, bool depth_test);
    void (*R_AddDebugCurveArrow)(const vec3_t start, const vec3_t ctrl, const vec3_t end, float size,
                                 uint32_t line_color, uint32_t arrow_color, uint32_t time, bool depth_test);
    void (*R_AddDebugText)(const vec3_t origin, const vec3_t angles, const char *text,
                           float size, uint32_t color, uint32_t time, bool depth_test);
} game_import_t;

//
// functions exported by the game subsystem
//
typedef struct {
    uint32_t    apiversion;
    uint32_t    structsize;

    // the init function will only be called when a game starts,
    // not each time a level is loaded.  Persistent data for clients
    // and the server can be allocated in init
    void (*Init)(void);
    void (*Shutdown)(void);

    // each new level entered will cause a call to SpawnEntities
    void (*SpawnEntities)(void);

    // Read/Write Game is for storing persistent cross level information
    // about the world state and the clients.
    // WriteGame is called every time a level is exited.
    // ReadGame is called on a loadgame.
    void (*WriteGame)(qhandle_t handle, bool autosave);
    void (*ReadGame)(qhandle_t handle);

    // ReadLevel is called after the default map information has been
    // loaded with SpawnEntities
    void (*WriteLevel)(qhandle_t handle);
    void (*ReadLevel)(qhandle_t handle);

    bool (*CanSave)(void);

    const char *(*ClientConnect)(int clientnum);
    void (*ClientBegin)(int clientnum);
    void (*ClientUserinfoChanged)(int clientnum);
    void (*ClientDisconnect)(int clientnum);
    void (*ClientCommand)(int clientnum);
    void (*ClientThink)(int clientnum);

    void (*PrepFrame)(void);
    void (*RunFrame)(void);

    // ServerCommand will be called when an "sv <command>" command is issued on the
    // server console.
    // The game can issue gi.argc() / gi.argv() commands to get the rest
    // of the parameters
    void (*ServerCommand)(void);

    void (*RestartFilesystem)(void); // called when fs_restart is issued
} game_export_t;

typedef game_export_t *(*game_entry_t)(const game_import_t *);
