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

// client.h -- primary header for client

#pragma once

#include "shared/shared.h"
#include "shared/list.h"
#include "shared/cgame.h"

#include "common/bsp.h"
#include "common/cmd.h"
#include "common/cmodel.h"
#include "common/common.h"
#include "common/cvar.h"
#include "common/field.h"
#include "common/files.h"
#include "common/math.h"
#include "common/msg.h"
#include "common/net/chan.h"
#include "common/net/net.h"
#include "common/prompt.h"
#include "common/protocol.h"
#include "common/sizebuf.h"
#include "common/zone.h"

#include "refresh/refresh.h"
#include "server/server.h"
#include "system/system.h"

#include "client/client.h"
#include "client/input.h"
#include "client/keys.h"
#include "client/sound/sound.h"
#include "client/ui.h"
#include "client/video.h"

#if USE_ZLIB
#include <zlib.h>
#endif

//=============================================================================

typedef struct {
    unsigned    sent;    // time sent, for calculating pings
    unsigned    rcvd;    // time rcvd, for calculating pings
    unsigned    cmdNumber;    // current cmdNumber for this frame
} client_history_t;

typedef struct {
    bool            valid;
    unsigned        servertime;

    int             number;
    int             delta;

    byte            areabits[MAX_MAP_AREA_BYTES];
    int             areabytes;

    player_state_t  ps;

    int             numEntities;
    unsigned        firstEntity;
} server_frame_t;

// locally calculated frame flags for debug display
#define FF_SERVERDROP   BIT(4)
#define FF_BADFRAME     BIT(5)
#define FF_OLDFRAME     BIT(6)
#define FF_OLDENT       BIT(7)
#define FF_NODELTA      BIT(8)

//
// the client_state_t structure is wiped completely at every
// server map change
//
typedef struct {
    int         timeoutcount;

    unsigned    lastTransmitTime;
    unsigned    lastTransmitCmdNumber;
    unsigned    lastTransmitCmdNumberReal;
    bool        sendPacketNow;

    usercmd_t   cmd;
    usercmd_t   cmds[CMD_BACKUP];    // each message will send several old cmds
    unsigned    cmdNumber;
    client_history_t    history[CMD_BACKUP];
    unsigned    initialSeq;

    // rebuilt each valid frame
    entity_state_t  baselines[MAX_EDICTS];

    entity_state_t  entityStates[MAX_PARSE_ENTITIES];
    unsigned        numEntityStates;

    server_frame_t  frames[UPDATE_BACKUP];
    unsigned        frameflags;
    int             suppress_count;

    server_frame_t  frame;                // received from server
    server_frame_t  oldframe;
    int             servertime;
    int             serverdelta;

    size_t          dcs[BC_COUNT(MAX_CONFIGSTRINGS)];

    // the client maintains its own idea of view angles, which are
    // sent to the server each frame.  It is cleared to 0 upon entering each level.
    // the server sends a delta each frame which is added to the locally
    // tracked view angles to account for standing on rotating objects,
    // and teleport direction changes
    vec3_t      viewangles;

    // interpolated movement vector used for local prediction,
    // never sent to server, rebuilt each client frame
    vec3_t      localmove;

    // accumulated mouse forward/side movement, added to both
    // localmove and pending cmd, cleared each time cmd is finalized
    vec2_t      mousemove;

    int         time;           // this is the time value that the client
                                // is rendering at.  always <= cl.servertime
    float       lerpfrac;       // between oldframe and frame

    int         lightlevel;

    //
    // server state information
    //
    int         serverstate;    // ss_* constants
    int         servercount;    // server identification for prespawns
    char        gamedir[MAX_QPATH];
    int         clientNum;            // never changed during gameplay, set by serverdata packet
    int         maxclients;

    char        *baseconfigstrings[MAX_CONFIGSTRINGS];
    char        *configstrings[MAX_CONFIGSTRINGS];

    char        mapname[MAX_QPATH]; // short format - q2dm1, etc

    //
    // locally derived information from server state
    //
    bsp_t        *bsp;
    unsigned    mapchecksum;
} client_state_t;

extern client_state_t   cl;

/*
==================================================================

the client_static_t structure is persistent through an arbitrary number
of server connections

==================================================================
*/

// resend delay for challenge/connect packets
#define CONNECT_DELAY       3000u

#define CONNECT_INSTANT     CONNECT_DELAY
#define CONNECT_FAST        (CONNECT_DELAY - 1000u)

typedef enum {
    ca_uninitialized,
    ca_disconnected,    // not talking to a server
    ca_challenging,     // sending getchallenge packets to the server
    ca_connecting,      // sending connect packets to the server
    ca_connected,       // netchan_t established, waiting for svc_serverdata
    ca_loading,         // loading level data
    ca_precached,       // loaded level data, waiting for svc_frame
    ca_active,          // game views should be displayed
    ca_cinematic        // running a cinematic
} connstate_t;

#define FOR_EACH_DLQ(q) \
    LIST_FOR_EACH(dlqueue_t, q, &cls.download.queue, entry)
#define FOR_EACH_DLQ_SAFE(q, n) \
    LIST_FOR_EACH_SAFE(dlqueue_t, q, n, &cls.download.queue, entry)

typedef enum {
    // generic types
    DL_OTHER,
    DL_MAP,
    DL_MODEL,
#if USE_CURL
    // special types
    DL_LIST,
    DL_PAK
#endif
} dltype_t;

typedef enum {
    DL_FREE,
    DL_PENDING,
    DL_RUNNING,
    DL_DONE
} dlstate_t;

typedef struct {
    list_t      entry;
    dltype_t    type;
    dlstate_t   state;
    char        path[1];
} dlqueue_t;

typedef struct {
    int         framenum;
    unsigned    msglen;
    int64_t     filepos;
    byte        data[1];
} demosnap_t;

typedef struct {
    connstate_t state;
    keydest_t   key_dest;

    active_t    active;

    bool        ref_initialized;

    bool        draw_loading;
    unsigned    disable_screen;

    int         userinfo_modified;
    cvar_t      *userinfo_updates[MAX_PACKET_USERINFOS];
// this is set each time a CVAR_USERINFO variable is changed
// so that the client knows to send it to the server

    unsigned    realtime;           // always increasing, no clamping, etc
    float       frametime;          // seconds since last video frame

// performance measurement
#define C_FPS   cls.measure.fps[0]
#define R_FPS   cls.measure.fps[1]
#define C_MPS   cls.measure.fps[2]
#define C_PPS   cls.measure.fps[3]
#define C_FRAMES    cls.measure.frames[0]
#define R_FRAMES    cls.measure.frames[1]
#define M_FRAMES    cls.measure.frames[2]
#define P_FRAMES    cls.measure.frames[3]
    struct {
        unsigned    time;
        int         frames[4];
        int         fps[4];
        int         ping;
    } measure;

// connection information
    netadr_t    serverAddress;
    char        servername[MAX_OSPATH]; // name of server from original connect
    unsigned    connect_time;           // for connection retransmits
    int         connect_count;
    bool        passive;

#if USE_ZLIB
    z_stream    z;
#endif

    int         quakePort;          // a 16 bit value that allows quake servers
                                    // to work around address translating routers
    netchan_t   netchan;
    int         serverProtocol;     // in case we are doing some kind of version hack
    int         protocolVersion;    // minor version

    int         challenge;          // from the server to use for connecting

#if USE_ICMP
    bool        errorReceived;      // got an ICMP error from server
#endif

#define RECENT_ADDR 4
#define RECENT_MASK (RECENT_ADDR - 1)

    netadr_t    recent_addr[RECENT_ADDR];
    unsigned    recent_head;

    struct {
        list_t      queue;              // queue of paths we need
        int         pending;            // number of non-finished entries in queue
        dlqueue_t   *current;           // path being downloaded
        int         percent;            // how much downloaded
        int64_t     position;           // how much downloaded (in bytes)
        qhandle_t   file;               // UDP file transfer from server
        char        temp[MAX_QPATH + 4];// account 4 bytes for .tmp suffix
#if USE_ZLIB
        z_stream    z;                  // UDP download zlib stream
#endif
        string_entry_t  *ignores;       // list of ignored paths
    } download;

// demo recording info must be here, so it isn't cleared on level change
    struct {
        qhandle_t   playback;
        qhandle_t   recording;
        unsigned    time_start;
        unsigned    time_frames;
        int         last_server_frame;  // number of server frame the last svc_frame was written
        int         frames_written;     // number of frames written to demo file
        int         frames_dropped;     // number of svc_frames that didn't fit
        int         others_dropped;     // number of misc svc_* messages that didn't fit
        int         frames_read;        // number of frames read from demo file
        int         last_snapshot;      // number of demo frame the last snapshot was saved
        int64_t     file_size;
        int64_t     file_offset;
        float       file_progress;
        sizebuf_t   buffer;
        demosnap_t  **snapshots;
        int         numsnapshots;
        bool        paused;
        bool        seeking;
        bool        eof;
        bool        compat;             // demomap compatibility mode
    } demo;
} client_static_t;

extern client_static_t      cls;

//=============================================================================

//
// cvars
//
extern cvar_t   *cl_gun;
extern cvar_t   *cl_gunalpha;
extern cvar_t   *cl_gunfov;
extern cvar_t   *cl_gun_x;
extern cvar_t   *cl_gun_y;
extern cvar_t   *cl_gun_z;
extern cvar_t   *cl_predict;
extern cvar_t   *cl_footsteps;
extern cvar_t   *cl_noskins;
extern cvar_t   *cl_kickangles;
extern cvar_t   *cl_rollhack;
extern cvar_t   *cl_noglow;
extern cvar_t   *cl_nobob;
extern cvar_t   *cl_nolerp;

#if USE_DEBUG
#define SHOWCLAMP(level, ...) \
    do { if (cl_showclamp->integer >= level) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__); } while (0)
#define SHOWMISS(...) \
    do { if (cl_showmiss->integer) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__); } while (0)
extern cvar_t   *cl_showmiss;
extern cvar_t   *cl_showclamp;
#else
#define SHOWCLAMP(...)
#define SHOWMISS(...)
#endif

extern cvar_t   *cl_vwep;

extern cvar_t   *cl_disable_particles;
extern cvar_t   *cl_disable_explosions;
extern cvar_t   *cl_dlight_hacks;
extern cvar_t   *cl_smooth_explosions;

extern cvar_t   *cl_chat_notify;
extern cvar_t   *cl_chat_sound;
extern cvar_t   *cl_chat_filter;

extern cvar_t   *cl_disconnectcmd;
extern cvar_t   *cl_changemapcmd;
extern cvar_t   *cl_beginmapcmd;

extern cvar_t   *cl_gibs;
extern cvar_t   *cl_flares;

extern cvar_t   *cl_thirdperson;
extern cvar_t   *cl_thirdperson_angle;
extern cvar_t   *cl_thirdperson_range;

extern cvar_t   *cl_async;

//
// userinfo
//
extern cvar_t   *info_password;
extern cvar_t   *info_spectator;
extern cvar_t   *info_name;
extern cvar_t   *info_skin;
extern cvar_t   *info_rate;
extern cvar_t   *info_fov;
extern cvar_t   *info_msg;
extern cvar_t   *info_hand;
extern cvar_t   *info_gender;
extern cvar_t   *info_uf;

//=============================================================================

static inline void CL_AdvanceValue(float *restrict val, float target, float speed)
{
    if (*val < target) {
        *val += speed * cls.frametime;
        if (*val > target)
            *val = target;
    } else if (*val > target) {
        *val -= speed * cls.frametime;
        if (*val < target)
            *val = target;
    }
}

//
// main.c
//

void CL_Init(void);
void CL_Quit_f(void);
void CL_Precache_f(void);
void CL_Changing_f(void);
void CL_Reconnect_f(void);
void CL_Disconnect(error_type_t type);
void CL_Begin(void);
void CL_CheckForResend(void);
void CL_ClearState(void);
void CL_RestartFilesystem(bool total);
void CL_RestartRefresh(bool total);
void CL_ClientCommand(const char *string);
void CL_SendRcon(const netadr_t *adr, const char *pass, const char *cmd);
const char *CL_Server_g(const char *partial, int argnum, int state);
void CL_CheckForPause(void);
void CL_UpdateFrameTimes(void);

void cl_timeout_changed(cvar_t *self);


//
// download.c
//
int CL_QueueDownload(const char *path, dltype_t type);
bool CL_IgnoreDownload(const char *path);
void CL_FinishDownload(dlqueue_t *q);
void CL_CleanupDownloads(void);
void CL_HandleDownload(const byte *data, int size, int percent, int decompressed_size);
bool CL_CheckDownloadExtension(const char *ext);
void CL_StartNextDownload(void);
void CL_RequestNextDownload(void);
void CL_ResetPrecacheCheck(void);
void CL_InitDownloads(void);


//
// input.c
//
void IN_Init(void);
void IN_Shutdown(void);
void IN_Frame(void);
void IN_Activate(void);

void CL_RegisterInput(void);
void CL_UpdateCmd(int msec);
void CL_FinalizeCmd(void);
void CL_SendCmd(void);


//
// parse.c
//

void CL_ParseServerMessage(void);
bool CL_SeekDemoMessage(void);

//
// demo.c
//
void CL_InitDemos(void);
void CL_CleanupDemos(void);
void CL_DemoFrame(void);
bool CL_WriteDemoMessage(sizebuf_t *buf);
void CL_EmitDemoFrame(void);
void CL_EmitDemoSnapshot(void);
void CL_FreeDemoSnapshots(void);
void CL_FirstDemoFrame(void);
void CL_Stop_f(void);
bool CL_GetDemoInfo(const char *path, demoInfo_t *info);



//
// console.c
//
void Con_Init(void);
void Con_PostInit(void);
void Con_Shutdown(void);
void Con_DrawConsole(void);
void Con_RunConsole(void);
void Con_Print(const char *txt);
void Con_ClearNotify_f(void);
void Con_ToggleConsole_f(void);
void Con_ClearTyping(void);
void Con_Close(bool force);
void Con_Popup(bool force);
void Con_SkipNotify(bool skip);
void Con_RegisterMedia(void);
void Con_CheckResize(void);
void Con_LoadState(const char *state);

void Key_Console(int key);
void Key_Message(int key);
void Char_Console(int key);
void Char_Message(int key);


//
// refresh.c
//
void    CL_InitRefresh(void);
void    CL_ShutdownRefresh(void);
void    CL_RunRefresh(void);


//
// screen.c
//

void    SCR_Init(void);
void    SCR_Shutdown(void);
void    SCR_UpdateScreen(void);
void    SCR_BeginLoadingPlaque(void);
void    SCR_EndLoadingPlaque(void);
void    SCR_ModeChanged(void);
void    SCR_AddNetgraph(void);

//
// cin.c
//

#if USE_AVCODEC

typedef struct {
    const char *ext;
    const char *fmt;
    int codec_id;
} avformat_t;

void    SCR_InitCinematics(void);
void    SCR_StopCinematic(void);
void    SCR_FinishCinematic(void);
void    SCR_RunCinematic(void);
void    SCR_DrawCinematic(void);
void    SCR_ReloadCinematic(void);
void    SCR_PlayCinematic(const char *name);

#else

static inline void SCR_FinishCinematic(void)
{
    // tell the server to advance to the next map / cinematic
    CL_ClientCommand(va("nextserver %i\n", cl.servercount));
}

#define SCR_InitCinematics()    (void)0
#define SCR_StopCinematic()     (void)0
#define SCR_RunCinematic()      (void)0
#define SCR_DrawCinematic()     (void)0
#define SCR_ReloadCinematic()   (void)0
#define SCR_PlayCinematic(name) SCR_FinishCinematic()

#endif

//
// http.c
//
#if USE_CURL
void HTTP_Init(void);
void HTTP_Shutdown(void);
void HTTP_SetServer(const char *url);
int HTTP_QueueDownload(const char *path, dltype_t type);
void HTTP_RunDownloads(void);
void HTTP_CleanupDownloads(void);
#else
#define HTTP_Init()                     (void)0
#define HTTP_Shutdown()                 (void)0
#define HTTP_SetServer(url)             (void)0
#define HTTP_QueueDownload(path, type)  Q_ERR(ENOSYS)
#define HTTP_RunDownloads()             (void)0
#define HTTP_CleanupDownloads()         (void)0
#endif

//
// cgame.c
//

extern const cgame_export_t *cge;

void CL_InitCGame(void);
void CL_ShutdownCGame(void);
