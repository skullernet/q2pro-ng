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
    gclient_t       *client;
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
    void (* q_printf(2, 3) bprintf)(int printlevel, const char *fmt, ...);
    void (* q_printf(1, 2) dprintf)(const char *fmt, ...);
    void (* q_printf(3, 4) cprintf)(edict_t *ent, int printlevel, const char *fmt, ...);
    void (* q_printf(2, 3) centerprintf)(edict_t *ent, const char *fmt, ...);
    void (*sound)(edict_t *ent, int channel, int soundindex, float volume, float attenuation, float timeofs);
    void (*positioned_sound)(const vec3_t origin, edict_t *ent, int channel, int soundindex, float volume, float attenuation, float timeofs);
    void (*local_sound)(edict_t *target, const vec3_t origin, edict_t *ent, int channel, int soundindex, float volume, float attenuation, float timeofs);

    // config strings hold all the index strings, the lightstyles,
    // and misc data like the sky definition and cdtrack.
    // All of the current configstrings are sent to clients when
    // they connect, and changes are sent to all connected clients.
    void (*configstring)(int num, const char *string);
    const char *(*get_configstring)(int index);

    void (* q_noreturn_ptr q_printf(1, 2) error)(const char *fmt, ...);

    // the *index functions create configstrings and some internal server state
    int (*modelindex)(const char *name);
    int (*soundindex)(const char *name);
    int (*imageindex)(const char *name);

    void (*setmodel)(edict_t *ent, const char *name);

    // collision detection
    void (*trace)(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int passent, int contentmask);
    void (*clip)(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, int clipent, int contentmask);
    int (*pointcontents)(const vec3_t point);
    bool (*inVIS)(const vec3_t p1, const vec3_t p2, vis_t vis);
    void (*SetAreaPortalState)(int portalnum, bool open);
    bool (*AreasConnected)(int area1, int area2);

    // an entity will never be sent to a client or used for collision
    // if it is not passed to linkentity.  If the size, position, or
    // solidity changes, it must be relinked.
    void (*linkentity)(edict_t *ent);
    void (*unlinkentity)(edict_t *ent);     // call before removing an interactive edict
    int (*BoxEdicts)(const vec3_t mins, const vec3_t maxs, edict_t **list, int maxcount, int areatype);

    // network messaging
    void (*multicast)(const vec3_t origin, multicast_t to);
    void (*unicast)(edict_t *ent, bool reliable);
    void (*WriteChar)(int c);
    void (*WriteByte)(int c);
    void (*WriteShort)(int c);
    void (*WriteLong)(int c);
    void (*WriteFloat)(float f);
    void (*WriteString)(const char *s);
    void (*WritePosition)(const vec3_t pos);    // some fractional bits
    void (*WriteDir)(const vec3_t pos);         // single byte encoded, very coarse
    void (*WriteAngle)(float f);

    // managed memory allocation
    void *(*TagMalloc)(unsigned size, unsigned tag);
    void *(*TagRealloc)(void *ptr, size_t size);
    void (*TagFree)(void *block);
    void (*FreeTags)(unsigned tag);

    // console variable interaction
    cvar_t *(*cvar)(const char *var_name, const char *value, int flags);
    cvar_t *(*cvar_set)(const char *var_name, const char *value);
    cvar_t *(*cvar_forceset)(const char *var_name, const char *value);

    // ClientCommand and ServerCommand parameter access
    int (*argc)(void);
    char *(*argv)(int n);
    char *(*args)(void);     // concatenation of all argv >= 1

    // add commands to the server console as if they were typed in
    // for map changing, etc
    void (*AddCommandString)(const char *text);

    void (*DebugGraph)(float value, int color);
    void *(*GetExtension)(const char *name);
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
    void (*SpawnEntities)(const char *mapname, const char *entstring, const char *spawnpoint);

    // Read/Write Game is for storing persistent cross level information
    // about the world state and the clients.
    // WriteGame is called every time a level is exited.
    // ReadGame is called on a loadgame.
    void (*WriteGame)(const char *filename, bool autosave);
    void (*ReadGame)(const char *filename);

    // ReadLevel is called after the default map information has been
    // loaded with SpawnEntities
    void (*WriteLevel)(const char *filename);
    void (*ReadLevel)(const char *filename);

    bool (*CanSave)(void);

    bool (*ClientConnect)(edict_t *ent, char *userinfo, char *conninfo);
    void (*ClientBegin)(edict_t *ent);
    void (*ClientUserinfoChanged)(edict_t *ent, char *userinfo);
    void (*ClientDisconnect)(edict_t *ent);
    void (*ClientCommand)(edict_t *ent);
    void (*ClientThink)(edict_t *ent, usercmd_t *cmd);
    void (*Pmove)(pmove_t *pmove);

    void (*PrepFrame)(void);
    void (*RunFrame)(void);

    // ServerCommand will be called when an "sv <command>" command is issued on the
    // server console.
    // The game can issue gi.argc() / gi.argv() commands to get the rest
    // of the parameters
    void (*ServerCommand)(void);

    bool (*CustomizeEntityToClient)(edict_t *client, edict_t *ent, entity_state_t *temp); // if true is returned, `temp' must be initialized
    bool (*EntityVisibleToClient)(edict_t *client, edict_t *ent);

    void *(*GetExtension)(const char *name);
    void (*RestartFilesystem)(void); // called when fs_restart is issued

    //
    // global variables shared between game and server
    //

    // The edict array is allocated in the game dll so it
    // can vary in size from one game to another.
    //
    // The size will be fixed when ge->Init() is called
    struct edict_s  *edicts;
    int         edict_size;
    int         num_edicts;     // current number, <= max_edicts
    int         max_edicts;
} game_export_t;

typedef game_export_t *(*game_entry_t)(const game_import_t *);

/*
==============================================================================

SERVER API EXTENSIONS

==============================================================================
*/

#define FILESYSTEM_API_V1 "FILESYSTEM_API_V1"

typedef struct {
    int64_t     (*OpenFile)(const char *path, qhandle_t *f, unsigned mode); // returns file length
    int         (*CloseFile)(qhandle_t f);
    int         (*LoadFile)(const char *path, void **buffer, unsigned flags, unsigned tag);

    int         (*ReadFile)(void *buffer, size_t len, qhandle_t f);
    int         (*WriteFile)(const void *buffer, size_t len, qhandle_t f);
    int         (*FlushFile)(qhandle_t f);
    int64_t     (*TellFile)(qhandle_t f);
    int         (*SeekFile)(qhandle_t f, int64_t offset, int whence);
    int         (*ReadLine)(qhandle_t f, char *buffer, size_t size);

    void        **(*ListFiles)(const char *path, const char *filter, unsigned flags, int *count_p);
    void        (*FreeFileList)(void **list);

    const char  *(*ErrorString)(int error);
} filesystem_api_v1_t;

#define DEBUG_DRAW_API_V1 "DEBUG_DRAW_API_V1"

typedef struct {
    void (*ClearDebugLines)(void);
    void (*AddDebugLine)(const vec3_t start, const vec3_t end, uint32_t color, uint32_t time, bool depth_test);
    void (*AddDebugPoint)(const vec3_t point, float size, uint32_t color, uint32_t time, bool depth_test);
    void (*AddDebugAxis)(const vec3_t origin, const vec3_t angles, float size, uint32_t time, bool depth_test);
    void (*AddDebugBounds)(const vec3_t mins, const vec3_t maxs, uint32_t color, uint32_t time, bool depth_test);
    void (*AddDebugSphere)(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test);
    void (*AddDebugCircle)(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test);
    void (*AddDebugCylinder)(const vec3_t origin, float half_height, float radius, uint32_t color, uint32_t time,
                             bool depth_test);
    void (*AddDebugArrow)(const vec3_t start, const vec3_t end, float size, uint32_t line_color,
                          uint32_t arrow_color, uint32_t time, bool depth_test);
    void (*AddDebugCurveArrow)(const vec3_t start, const vec3_t ctrl, const vec3_t end, float size,
                               uint32_t line_color, uint32_t arrow_color, uint32_t time, bool depth_test);
    void (*AddDebugText)(const vec3_t origin, const vec3_t angles, const char *text,
                         float size, uint32_t color, uint32_t time, bool depth_test);
} debug_draw_api_v1_t;
