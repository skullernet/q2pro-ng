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
// server.h

#pragma once

#include "shared/shared.h"
#include "shared/list.h"
#include "shared/game.h"

#include "common/bsp.h"
#include "common/cmd.h"
#include "common/cmodel.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/error.h"
#include "common/files.h"
#include "common/intreadwrite.h"
#include "common/msg.h"
#include "common/net/chan.h"
#include "common/net/net.h"
#include "common/prompt.h"
#include "common/protocol.h"
#include "common/zone.h"

#include "client/client.h"
#include "server/server.h"
#include "system/system.h"

#if USE_ZLIB
#include <zlib.h>
#endif

//=============================================================================

#define SV_Malloc(size)         Z_TagMalloc(size, TAG_SERVER)
#define SV_Mallocz(size)        Z_TagMallocz(size, TAG_SERVER)
#define SV_CopyString(s)        Z_TagCopyString(s, TAG_SERVER)

#if USE_DEBUG
#define SV_DPrintf(level,...) \
    do { if (sv_debug && sv_debug->integer >= level) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__); } while (0)
#else
#define SV_DPrintf(...)
#endif

#define SV_BASELINES_SHIFT      6
#define SV_BASELINES_PER_CHUNK  (1 << SV_BASELINES_SHIFT)
#define SV_BASELINES_MASK       (SV_BASELINES_PER_CHUNK - 1)
#define SV_BASELINES_CHUNKS     (MAX_EDICTS >> SV_BASELINES_SHIFT)

#define SV_InfoSet(var, val) \
    Cvar_FullSet(var, val, CVAR_SERVERINFO|CVAR_ROM, FROM_CODE)

#if USE_CLIENT
#define SV_PAUSED (sv_paused->integer != 0)
#else
#define SV_PAUSED 0
#endif

typedef struct {
    int         number;
    int         num_entities;
    unsigned    first_entity;
    player_state_t ps;
    int         areabytes;
    byte        areabits[MAX_MAP_AREA_BYTES];  // portalarea visibility bits
    unsigned    sentTime;                   // for ping calculations
    int         latency;
} client_frame_t;

#define MAX_TOTAL_ENT_LEAFS        128

#define MAX_ENT_CLUSTERS    16

typedef struct {
    list_t      area;               // linked to a division node or leaf
    int         num_clusters;       // if -1, use headnode instead
    int         clusternums[MAX_ENT_CLUSTERS];
    int         headnode;           // unused if num_clusters != -1
    edict_t     *edict;
} server_entity_t;

typedef struct {
    server_state_t  state;      // precache commands are only valid during load
    int             spawncount; // random number generated each server spawn
    bool            nextserver_pending;

    unsigned    time;
    unsigned    frametime;
    unsigned    frameresidual;

    unsigned    last_calcping_time;
    unsigned    last_givemsec_time;

    char        mapcmd[MAX_QPATH];          // ie: *intro.cin+base

    char        name[MAX_QPATH];            // map name, or cinematic name
    char        spawnpoint[MAX_QPATH];
    cm_t        cm;

    const char  *entitystring;
    char        *configstrings[MAX_CONFIGSTRINGS];

    server_entity_t entities[MAX_EDICTS];
} server_t;

typedef enum {
    cs_free,        // can be reused for a new connection
    cs_zombie,      // client has been disconnected, but don't reuse
                    // connection for a couple seconds
    cs_assigned,    // client_t assigned, but no data received from client yet
    cs_connected,   // netchan fully established, but not in game yet
    cs_primed,      // sent serverdata, client is precaching
    cs_spawned      // client is fully in game
} clstate_t;

#define MSG_RELIABLE        BIT(0)
#define MSG_CLEAR           BIT(1)
#define MSG_COMPRESS        BIT(2)
#define MSG_COMPRESS_AUTO   BIT(3)

#define ZPACKET_HEADER      5

#define RATE_MESSAGES   10

#define FOR_EACH_CLIENT(client) \
    LIST_FOR_EACH(client_t, client, &sv_clientlist, entry)

#define CLIENT_ACTIVE(cl) \
    ((cl)->state == cs_spawned && !(cl)->download && !(cl)->nodata)

#define PL_S2C(cl) (cl->frames_sent ? \
    (1.0f - (float)cl->frames_acked / cl->frames_sent) * 100.0f : 0.0f)
#define PL_C2S(cl) (cl->netchan.total_received ? \
    ((float)cl->netchan.total_dropped / cl->netchan.total_received) * 100.0f : 0.0f)
#define AVG_PING(cl) (cl->avg_ping_count ? \
    cl->avg_ping_time / cl->avg_ping_count : cl->ping)

typedef struct {
    unsigned    time;
    unsigned    credit;
    unsigned    credit_cap;
    unsigned    cost;
} ratelimit_t;

typedef struct client_s {
    list_t          entry;

    // core info
    clstate_t       state;
    edict_t         *edict;     // EDICT_NUM(clientnum+1)
    gclient_t       *client;
    int             number;     // client slot number

    // client flags
    bool            reconnected: 1;
    bool            nodata: 1;
    bool            has_zlib: 1;
    bool            drop_hack: 1;
#if USE_ICMP
    bool            unreachable: 1;
#endif
    bool            http_download: 1;

    // userinfo
    char            userinfo[MAX_INFO_STRING];  // name, etc
    char            name[MAX_CLIENT_NAME];      // extracted from userinfo, high bits masked
    int             messagelevel;               // for filtering printed messages
    unsigned        rate;
    ratelimit_t     ratelimit_namechange;       // for suppressing "foo changed name" flood

    // console var probes
    char            *version_string;
    int             console_queries;

    // usercmd stuff
    unsigned        lastmessage;    // svs.realtime when packet was last received
    unsigned        lastactivity;   // svs.realtime when user activity was last seen
    int             lastframe;      // for delta compression
    usercmd_t       lastcmd;        // for filling in big drops
    int             command_msec;   // every seconds this is reset, if user
                                    // commands exhaust it, assume time cheating
    int             num_moves;      // reset every 10 seconds
    int             moves_per_sec;  // average movement FPS
    int             cmd_msec_used;
    float           timescale;

    int             ping, min_ping, max_ping;
    int             avg_ping_time, avg_ping_count;

    // frame encoding
    client_frame_t  frames[UPDATE_BACKUP];    // updates can be delta'd from here
    unsigned        frames_sent, frames_acked, frames_nodelta;
    int             framenum;
    unsigned        frameflags;

    // rate dropping
    unsigned        message_size[RATE_MESSAGES];    // used to rate drop normal packets
    int             suppress_count;                 // number of messages rate suppressed
    unsigned        send_time, send_delta;          // used to rate drop async packets

    // current download
    byte            *download;      // file being downloaded
    int             downloadsize;   // total bytes (can't use EOF because of paks)
    int             downloadcount;  // bytes sent
    char            *downloadname;  // name of the file
    int             downloadcmd;    // svc_(z)download
    bool            downloadpending;

    // protocol stuff
    int             challenge;  // challenge of this user, randomly generated
    int             protocol;   // major version
    int             version;    // minor version
    int             settings[CLS_MAX];

    // per-client baseline chunks
    entity_state_t      *baselines[SV_BASELINES_CHUNKS];

    // per-client packet entities
    unsigned            num_entities;   // UPDATE_BACKUP*MAX_PACKET_ENTITIES(_OLD)
    unsigned            next_entity;    // next state to use
    entity_state_t      *entities;      // [num_entities]

    // The datagram is written to by sound calls, prints, temp ents, etc.
    // It can be harmlessly overflowed.
    sizebuf_t       datagram;

    // netchan
    netchan_t       netchan;
    int             numpackets; // for that nasty packetdup hack

    // misc
    time_t          connect_time; // time of initial connect
} client_t;

// a client can leave the server in one of four ways:
// dropping properly by quitting or disconnecting
// timing out if no valid messages are received for timeout.value seconds
// getting kicked off by the server operator
// a program error, like an overflowed reliable buffer

//=============================================================================

// MAX_CHALLENGES is made large to prevent a denial
// of service attack that could cycle all of them
// out before legitimate users connected
#define    MAX_CHALLENGES    1024

typedef struct {
    netadr_t    adr;
    unsigned    challenge;
    unsigned    time;
} challenge_t;

typedef struct {
    list_t      entry;
    netadr_t    addr;
    netadr_t    mask;
    unsigned    hits;
    time_t      time;   // time of the last hit
    char        comment[1];
} addrmatch_t;

typedef struct {
    list_t  entry;
    char    string[1];
} stuffcmd_t;

#define MAX_MASTERS         8       // max recipients for heartbeat packets
#define HEARTBEAT_SECONDS   300

typedef struct {
    netadr_t        adr;
    unsigned        last_ack;
    time_t          last_resolved;
    char            *name;
} master_t;

typedef struct {
    char            buffer[MAX_QPATH];  // original mapcmd
    char            server[MAX_QPATH];  // parsed map name
    char            *spawnpoint;
    server_state_t  state;
    int             loadgame;
    bool            endofunit;
    cm_t            cm;
} mapcmd_t;

typedef struct {
    bool        initialized;        // sv_init has completed
    unsigned    realtime;           // always increasing, no clamping, etc

    int         maxclients_soft;    // minus reserved slots
    int         maxclients;
    client_t    *client_pool;       // [maxclients]

    edict_t     *edicts;
    int         edict_size;
    int         num_edicts;

    uint32_t    vm_edicts_minptr;
    uint32_t    vm_edicts_maxptr;

    gclient_t   *clients;
    int         client_size;

#if USE_ZLIB
    z_stream        z;  // for compressing messages at once
    byte            *z_buffer;
    unsigned        z_buffer_size;
#endif

#if USE_SAVEGAMES
    qhandle_t       savefile;
#endif

    unsigned        last_heartbeat;
    unsigned        last_timescale_check;

    unsigned        heartbeat_index;

    ratelimit_t     ratelimit_status;
    ratelimit_t     ratelimit_auth;
    ratelimit_t     ratelimit_rcon;

    challenge_t     challenges[MAX_CHALLENGES]; // to prevent invalid IPs from connecting
} server_static_t;

//=============================================================================

extern master_t     sv_masters[MAX_MASTERS];    // address of the master server

extern list_t       sv_banlist;
extern list_t       sv_blacklist;
extern list_t       sv_cmdlist_connect;
extern list_t       sv_cmdlist_begin;
extern list_t       sv_lrconlist;
extern list_t       sv_filterlist;
extern list_t       sv_cvarbanlist;
extern list_t       sv_infobanlist;
extern list_t       sv_clientlist;  // linked list of non-free clients

extern server_static_t      svs;        // persistent server info
extern server_t             sv;         // local server

extern cvar_t       *sv_hostname;
extern cvar_t       *sv_maxclients;
extern cvar_t       *sv_password;
extern cvar_t       *sv_reserved_slots;
extern cvar_t       *sv_enforcetime;
extern cvar_t       *sv_fps;
extern cvar_t       *sv_iplimit;

#if USE_DEBUG
extern cvar_t       *sv_debug;
extern cvar_t       *sv_pad_packets;
#endif
extern cvar_t       *sv_novis;
extern cvar_t       *sv_lan_force_rate;
extern cvar_t       *sv_calcpings_method;
extern cvar_t       *sv_changemapcmd;
extern cvar_t       *sv_max_download_size;

extern cvar_t       *sv_strafejump_hack;
#if USE_PACKETDUP
extern cvar_t       *sv_packetdup_hack;
#endif
extern cvar_t       *sv_allow_map;
extern cvar_t       *sv_cinematics;
#if USE_SERVER
extern cvar_t       *sv_recycle;
#endif
extern cvar_t       *sv_enhanced_setplayer;

extern cvar_t       *sv_status_limit;
extern cvar_t       *sv_status_show;
extern cvar_t       *sv_auth_limit;
extern cvar_t       *sv_rcon_limit;
extern cvar_t       *sv_uptime;

extern cvar_t       *sv_allow_unconnected_cmds;

extern cvar_t       *sv_timeout;
extern cvar_t       *sv_zombietime;
extern cvar_t       *sv_ghostime;

extern client_t     *sv_client;
extern edict_t      *sv_player;


//===========================================================

//
// sv_main.c
//
void SV_DropClient(client_t *drop, const char *reason);
void SV_RemoveClient(client_t *client);
void SV_CleanClient(client_t *client);

void SV_InitOperatorCommands(void);

void SV_UserinfoChanged(client_t *cl);

bool SV_RateLimited(ratelimit_t *r);
void SV_RateRecharge(ratelimit_t *r);
void SV_RateInit(ratelimit_t *r, const char *s);

addrmatch_t *SV_MatchAddress(const list_t *list, const netadr_t *address);

int SV_CountClients(void);

#if USE_ZLIB
voidpf SV_zalloc(voidpf opaque, uInt items, uInt size);
void SV_zfree(voidpf opaque, voidpf address);
#endif

void sv_sec_timeout_changed(cvar_t *self);
void sv_min_timeout_changed(cvar_t *self);

//
// sv_init.c
//

void SV_ClientReset(client_t *client);
void SV_SetState(server_state_t state);
void SV_SpawnServer(const mapcmd_t *cmd);
bool SV_ParseMapCmd(mapcmd_t *cmd);
void SV_InitGame(void);

//
// sv_send.c
//
typedef enum {RD_NONE, RD_CLIENT, RD_PACKET} redirect_t;
#define SV_OUTPUTBUF_LENGTH     (MAX_PACKETLEN_DEFAULT - 16)

#define SV_ClientRedirect() \
    Com_BeginRedirect(RD_CLIENT, sv_outputbuf, MAX_STRING_CHARS - 1, SV_FlushRedirect)

#define SV_PacketRedirect() \
    Com_BeginRedirect(RD_PACKET, sv_outputbuf, SV_OUTPUTBUF_LENGTH, SV_FlushRedirect)

extern char sv_outputbuf[SV_OUTPUTBUF_LENGTH];

void SV_FlushRedirect(int redirected, const char *outputbuf, size_t len);

void SV_SendClientMessages(void);
void SV_SendAsyncPackets(void);

void SV_ClientCommand(client_t *cl, const char *fmt, ...) q_printf(2, 3);
void SV_BroadcastCommand(const char *fmt, ...) q_printf(1, 2);
void SV_ClientAddMessage(client_t *client, int flags);
void SV_ShutdownClientSend(client_t *client);
void SV_InitClientSend(client_t *newcl);

//
// sv_user.c
//
void SV_New_f(void);
void SV_Begin_f(void);
void SV_ExecuteClientMessage(client_t *cl);
void SV_CloseDownload(client_t *client);

//
// sv_ccmds.c
//
client_t *SV_GetPlayer(const char *s, bool partial);
void SV_PrintMiscInfo(void);

//
// sv_ents.c
//

#define HAS_EFFECTS(ent) \
    ((ent)->s.modelindex || (ent)->s.effects || (ent)->s.sound || (ent)->s.event)

void SV_BuildClientFrame(client_t *client);
void SV_WriteFrameToClient(client_t *client);

//
// sv_game.c
//
extern const game_export_t      *ge;

static inline int SV_NumForEdict(const edict_t *e)
{
    return ((byte *)e - (byte *)svs.edicts) / svs.edict_size;
}

static inline edict_t *SV_EdictForNum(int n)
{
    return (edict_t *)((byte *)svs.edicts + svs.edict_size * n);
}

static inline gclient_t *SV_ClientForNum(int n)
{
    return (gclient_t *)((byte *)svs.clients + svs.client_size * n);
}

static inline server_entity_t *SV_SentForEdict(const edict_t *e)
{
    return &sv.entities[SV_NumForEdict(e)];
}

void SV_InitGameProgs(void);
void SV_ShutdownGameProgs(void);

//
// sv_save.c
//
#if USE_SAVEGAMES
void SV_AutoSaveBegin(const mapcmd_t *cmd);
void SV_AutoSaveEnd(void);
void SV_CheckForSavegame(const mapcmd_t *cmd);
void SV_CheckForEnhancedSavegames(void);
void SV_RegisterSavegames(void);
#else
#define SV_AutoSaveBegin(cmd)           (void)0
#define SV_AutoSaveEnd()                (void)0
#define SV_CheckForSavegame(cmd)        (void)0
#define SV_CheckForEnhancedSavegames()  (void)0
#define SV_RegisterSavegames()          (void)0
#endif

//
// sv_nav.c
//
void Nav_Register(void);
void Nav_Load(void);
void Nav_Unload(void);
void Nav_Frame(void);
bool Nav_GetPathToGoal(const PathRequest *request, PathInfo *info);

//
// ugly gclient_(old|new)_t accessors
//

static inline void SV_GetClient_ViewOrg(const client_t *client, vec3_t org)
{
    const gclient_t *cl = client->client;
    VectorAdd(cl->ps.pmove.origin, cl->ps.viewoffset, org);
}

static inline int SV_GetClient_Stat(const client_t *client, int stat)
{
    return client->client->ps.stats[stat];
}

static inline void SV_SetClient_Ping(const client_t *client, int ping)
{
    client->client->ping = ping;
}

//============================================================

//
// high level object sorting to reduce interaction tests
//

void SV_ClearWorld(void);
// called after the world model has been loaded, before linking any entities

void PF_UnlinkEdict(edict_t *ent);
// call before removing an entity, and before trying to move one,
// so it doesn't clip against itself

void PF_LinkEdict(edict_t *ent);
// Needs to be called any time an entity changes origin, mins, maxs,
// or solid.  Automatically unlinks if needed.
// sets ent->v.absmin and ent->v.absmax
// sets ent->leafnums[] for pvs determination even if the entity
// is not solid

int SV_AreaEdicts(const vec3_t mins, const vec3_t maxs, int *list, int maxcount, int areatype);
// fills in a table of edict pointers with edicts that have bounding boxes
// that intersect the given area.  It is possible for a non-axial bmodel
// to be returned that doesn't actually intersect the area on an exact
// test.
// returns the number of pointers filled in
// ??? does this always return the world?

//===================================================================

//
// functions that interact with everything appropriate
//
contents_t SV_PointContents(const vec3_t p);
// returns the CONTENTS_* value from the world at the given point.
// Quake 2 extends this to also check entities, to allow moving liquids

void SV_Trace(trace_t *trace, const vec3_t start, const vec3_t mins,
              const vec3_t maxs, const vec3_t end, unsigned passent, contents_t contentmask);
// mins and maxs are relative

// if the entire move stays in a solid volume, trace.allsolid will be set,
// trace.startsolid will be set, and trace.fraction will be 0

// if the starting point is in a solid, it will be allowed to move out
// to an open area

// passedict is explicitly excluded from clipping checks (normally NULL)

void SV_Clip(trace_t *trace, const vec3_t start, const vec3_t mins,
             const vec3_t maxs, const vec3_t end, unsigned clipent, contents_t contentmask);
