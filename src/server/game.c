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
// sv_game.c -- interface to the game dll

#include "server.h"
#include "common/math.h"

const game_export_t     *ge;

static void PF_configstring(int index, const char *val);

/*
================
PF_FindIndex

================
*/
static int PF_FindIndex(const char *name, int start, int max, int skip, const char *func)
{
    char *string;
    int i;

    if (!name || !name[0])
        return 0;

    for (i = 1; i < max; i++) {
        if (i == skip) {
            continue;
        }
        string = sv.configstrings[start + i];
        if (!string[0]) {
            break;
        }
        if (!strcmp(string, name)) {
            return i;
        }
    }

    if (i == max)
        Com_Error(ERR_DROP, "%s(%s): overflow", func, name);

    PF_configstring(i + start, name);

    return i;
}

static int PF_ModelIndex(const char *name)
{
    return PF_FindIndex(name, CS_MODELS, MAX_MODELS, MODELINDEX_PLAYER, __func__);
}

static int PF_SoundIndex(const char *name)
{
    return PF_FindIndex(name, CS_SOUNDS, MAX_SOUNDS, 0, __func__);
}

static int PF_ImageIndex(const char *name)
{
    return PF_FindIndex(name, CS_IMAGES, MAX_IMAGES, 0, __func__);
}

/*
===============
PF_Unicast

Sends the contents of the mutlicast buffer to a single client.
===============
*/
static void PF_Unicast(edict_t *ent, bool reliable)
{
    client_t    *client;
    int         cmd, flags, clientNum;

    if (!ent) {
        goto clear;
    }

    if (msg_write.overflowed)
        Com_Error(ERR_DROP, "%s: message buffer overflowed", __func__);

    clientNum = SV_NumForEdict(ent);
    if (clientNum < 0 || clientNum >= svs.maxclients) {
        Com_DWPrintf("%s to a non-client %d\n", __func__, clientNum);
        goto clear;
    }

    client = svs.client_pool + clientNum;
    if (client->state <= cs_zombie) {
        Com_DWPrintf("%s to a free/zombie client %d\n", __func__, clientNum);
        goto clear;
    }

    if (!msg_write.cursize) {
        Com_DPrintf("%s with empty data\n", __func__);
        goto clear;
    }

    cmd = msg_write.data[0];

    flags = 0;
    if (reliable) {
        flags |= MSG_RELIABLE;
    }

    if (cmd == svc_layout || (cmd == svc_configstring && RL16(&msg_write.data[1]) == CS_STATUSBAR)) {
        flags |= MSG_COMPRESS_AUTO;
    }

    SV_ClientAddMessage(client, flags);

    // fix anti-kicking exploit for broken mods
    if (cmd == svc_disconnect) {
        client->drop_hack = true;
    }

clear:
    SZ_Clear(&msg_write);
}

/*
=================
PF_bprintf

Sends text to all active clients.
=================
*/
static void PF_bprintf(int level, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    client_t    *client;
    size_t      len;
    int         i;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_DWPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_print);
    MSG_WriteByte(level);
    MSG_WriteData(string, len + 1);

    // echo to console
    if (COM_DEDICATED) {
        // mask off high bits
        for (i = 0; i < len; i++)
            string[i] &= 127;
        Com_Printf("%s", string);
    }

    FOR_EACH_CLIENT(client) {
        if (client->state != cs_spawned)
            continue;
        if (level >= client->messagelevel) {
            SV_ClientAddMessage(client, MSG_RELIABLE);
        }
    }

    SZ_Clear(&msg_write);
}

/*
===============
PF_dprintf

Debug print to server console.
===============
*/
static void PF_dprintf(const char *fmt, ...)
{
    char        msg[MAXPRINTMSG];
    va_list     argptr;

#if USE_SAVEGAMES
    // detect YQ2 game lib by unique first two messages
    if (!svs.gamedetecthack)
        svs.gamedetecthack = 1 + !strcmp(fmt, "Game is starting up.\n");
    else if (svs.gamedetecthack == 2)
        svs.gamedetecthack = 3 + !strcmp(fmt, "Game is %s built on %s.\n");
#endif

    va_start(argptr, fmt);
    Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    Con_SkipNotify(true);
    Com_Printf("%s", msg);
    Con_SkipNotify(false);
}

/*
===============
PF_cprintf

Print to a single client if the level passes.
===============
*/
static void PF_cprintf(edict_t *ent, int level, const char *fmt, ...)
{
    char        msg[MAX_STRING_CHARS];
    va_list     argptr;
    int         clientNum;
    size_t      len;
    client_t    *client;

    va_start(argptr, fmt);
    len = Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(msg)) {
        Com_DWPrintf("%s: overflow\n", __func__);
        return;
    }

    if (!ent) {
        Com_LPrintf(level == PRINT_CHAT ? PRINT_TALK : PRINT_ALL, "%s", msg);
        return;
    }

    clientNum = SV_NumForEdict(ent);
    if (clientNum < 0 || clientNum >= svs.maxclients) {
        Com_DWPrintf("%s to a non-client %d\n", __func__, clientNum);
        return;
    }

    client = svs.client_pool + clientNum;
    if (client->state <= cs_zombie) {
        Com_DWPrintf("%s to a free/zombie client %d\n", __func__, clientNum);
        return;
    }

    MSG_WriteByte(svc_print);
    MSG_WriteByte(level);
    MSG_WriteData(msg, len + 1);

    if (level >= client->messagelevel) {
        SV_ClientAddMessage(client, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);
}

/*
===============
PF_centerprintf

Centerprint to a single client.
===============
*/
static void PF_centerprintf(edict_t *ent, const char *fmt, ...)
{
    char        msg[MAX_STRING_CHARS];
    va_list     argptr;
    int         n;
    size_t      len;

    if (!ent) {
        return;
    }

    n = SV_NumForEdict(ent);
    if (n < 0 || n >= svs.maxclients) {
        Com_DWPrintf("%s to a non-client %d\n", __func__, n);
        return;
    }

    va_start(argptr, fmt);
    len = Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(msg)) {
        Com_DWPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_centerprint);
    MSG_WriteData(msg, len + 1);

    PF_Unicast(ent, true);
}

/*
===============
PF_error

Abort the server with a game error
===============
*/
static q_noreturn void PF_error(const char *fmt, ...)
{
    char        msg[MAXERRORMSG];
    va_list     argptr;

    va_start(argptr, fmt);
    Q_vsnprintf(msg, sizeof(msg), fmt, argptr);
    va_end(argptr);

    Com_Error(ERR_DROP, "Game Error: %s", msg);
}

/*
=================
PF_setmodel

Also sets mins and maxs for inline bmodels
=================
*/
static void PF_setmodel(edict_t *ent, const char *name)
{
    if (!ent || !name)
        Com_Error(ERR_DROP, "PF_setmodel: NULL");

    ent->s.modelindex = PF_ModelIndex(name);

// if it is an inline model, get the size information for it
    if (name[0] == '*') {
        const mmodel_t *mod = CM_InlineModel(&sv.cm, name);
        VectorCopy(mod->mins, ent->r.mins);
        VectorCopy(mod->maxs, ent->r.maxs);
        PF_LinkEdict(ent);
    }
}

/*
===============
PF_configstring

If game is actively running, broadcasts configstring change.
===============
*/
static void PF_configstring(int index, const char *val)
{
    size_t len, maxlen;
    client_t *client;
    char *dst;

    if (index < 0 || index >= MAX_CONFIGSTRINGS)
        Com_Error(ERR_DROP, "%s: bad index: %d", __func__, index);

    if (sv.state == ss_dead) {
        Com_DWPrintf("%s: not yet initialized\n", __func__);
        return;
    }

    if (!val)
        val = "";

    // error out entirely if it exceedes array bounds
    len = strlen(val);
    maxlen = (MAX_CONFIGSTRINGS - index) * MAX_QPATH;
    if (len >= maxlen) {
        Com_Error(ERR_DROP,
                  "%s: index %d overflowed: %zu > %zu",
                  __func__, index, len, maxlen - 1);
    }

    // print a warning and truncate everything else
    maxlen = Com_ConfigstringSize(index);
    if (len >= maxlen) {
        Com_DWPrintf(
            "%s: index %d overflowed: %zu > %zu\n",
            __func__, index, len, maxlen - 1);
        len = maxlen - 1;
    }

    dst = sv.configstrings[index];
    if (!strncmp(dst, val, maxlen)) {
        return;
    }

    // change the string in sv
    memcpy(dst, val, len);
    dst[len] = 0;

    if (sv.state == ss_loading) {
        return;
    }

    // send the update to everyone
    MSG_WriteByte(svc_configstring);
    MSG_WriteShort(index);
    MSG_WriteData(val, len);
    MSG_WriteByte(0);

    FOR_EACH_CLIENT(client) {
        if (client->state < cs_primed) {
            continue;
        }
        SV_ClientAddMessage(client, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);
}

static const char *PF_GetConfigstring(int index)
{
    if (index < 0 || index >= MAX_CONFIGSTRINGS)
        Com_Error(ERR_DROP, "%s: bad index: %d", __func__, index);

    return sv.configstrings[index];
}

static void PF_WriteFloat(float f)
{
    Com_Error(ERR_DROP, "PF_WriteFloat not implemented");
}

static bool PF_inVIS(const vec3_t p1, const vec3_t p2, vis_t vis)
{
    const mleaf_t *leaf1, *leaf2;
    visrow_t mask;

    leaf1 = CM_PointLeaf(&sv.cm, p1);
    BSP_ClusterVis(sv.cm.cache, &mask, leaf1->cluster, vis & VIS_PHS);

    leaf2 = CM_PointLeaf(&sv.cm, p2);
    if (leaf2->cluster == -1)
        return false;
    if (!Q_IsBitSet(mask.b, leaf2->cluster))
        return false;
    if (vis & VIS_NOAREAS)
        return true;
    if (!CM_AreasConnected(&sv.cm, leaf1->area, leaf2->area))
        return false;       // a door blocks it
    return true;
}

static cvar_t *PF_cvar(const char *name, const char *value, int flags)
{
    if (flags & CVAR_EXTENDED_MASK) {
        Com_WPrintf("Game attempted to set extended flags on '%s', masked out.\n", name);
        flags &= ~CVAR_EXTENDED_MASK;
    }

    return Cvar_Get(name, value, flags | CVAR_GAME);
}

static void PF_AddCommandString(const char *string)
{
#if USE_CLIENT
    if (!strcmp(string, "menu_loadgame\n"))
        string = "pushmenu loadgame\n";
#endif
    Cbuf_AddText(&cmd_buffer, string);
}

static void PF_SetAreaPortalState(int portalnum, bool open)
{
    CM_SetAreaPortalState(&sv.cm, portalnum, open);
}

static bool PF_AreasConnected(int area1, int area2)
{
    return CM_AreasConnected(&sv.cm, area1, area2);
}

static void *PF_TagMalloc(unsigned size, unsigned tag)
{
    if (tag > UINT16_MAX - TAG_MAX) {
        Com_Error(ERR_DROP, "%s: bad tag", __func__);
    }
    return Z_TagMallocz(size, tag + TAG_MAX);
}

static void PF_FreeTags(unsigned tag)
{
    if (tag > UINT16_MAX - TAG_MAX) {
        Com_Error(ERR_DROP, "%s: bad tag", __func__);
    }
    Z_FreeTags(tag + TAG_MAX);
}

static int PF_LoadFile(const char *path, void **buffer, unsigned flags, unsigned tag)
{
    if (tag > UINT16_MAX - TAG_MAX) {
        Com_Error(ERR_DROP, "%s: bad tag", __func__);
    }
    return FS_LoadFileEx(path, buffer, flags, tag + TAG_MAX);
}

static void *PF_TagRealloc(void *ptr, size_t size)
{
    if (!ptr && size) {
        Com_Error(ERR_DROP, "%s: untagged allocation not allowed", __func__);
    }
    return Z_Realloc(ptr, size);
}

//==============================================

static const filesystem_api_v1_t filesystem_api_v1 = {
    .OpenFile = FS_OpenFile,
    .CloseFile = FS_CloseFile,
    .LoadFile = PF_LoadFile,

    .ReadFile = FS_Read,
    .WriteFile = FS_Write,
    .FlushFile = FS_Flush,
    .TellFile = FS_Tell,
    .SeekFile = FS_Seek,
    .ReadLine = FS_ReadLine,

    .ListFiles = FS_ListFiles,
    .FreeFileList = FS_FreeList,

    .ErrorString = Q_ErrorString,
};

#if USE_REF && USE_DEBUG
static const debug_draw_api_v1_t debug_draw_api_v1 = {
    .ClearDebugLines = R_ClearDebugLines,
    .AddDebugLine = R_AddDebugLine,
    .AddDebugPoint = R_AddDebugPoint,
    .AddDebugAxis = R_AddDebugAxis,
    .AddDebugBounds = R_AddDebugBounds,
    .AddDebugSphere = R_AddDebugSphere,
    .AddDebugCircle = R_AddDebugCircle,
    .AddDebugCylinder = R_AddDebugCylinder,
    .AddDebugArrow = R_AddDebugArrow,
    .AddDebugCurveArrow = R_AddDebugCurveArrow,
    .AddDebugText = R_AddDebugText,
};
#endif

static void *PF_GetExtension(const char *name)
{
    if (!name)
        return NULL;

    if (!strcmp(name, FILESYSTEM_API_V1))
        return (void *)&filesystem_api_v1;

#if USE_REF && USE_DEBUG
    if (!strcmp(name, DEBUG_DRAW_API_V1) && !dedicated->integer)
        return (void *)&debug_draw_api_v1;
#endif

    return NULL;
}

static const game_import_t game_import = {
    .apiversion = GAME_API_VERSION,
    .structsize = sizeof(game_import_t),

    .multicast = SV_Multicast,
    .unicast = PF_Unicast,
    .bprintf = PF_bprintf,
    .dprintf = PF_dprintf,
    .cprintf = PF_cprintf,
    .centerprintf = PF_centerprintf,
    .error = PF_error,

    .linkentity = PF_LinkEdict,
    .unlinkentity = PF_UnlinkEdict,
    .BoxEdicts = SV_AreaEdicts,
    .trace = SV_Trace,
    .clip = SV_Clip,
    .pointcontents = SV_PointContents,
    .setmodel = PF_setmodel,
    .inVIS = PF_inVIS,

    .modelindex = PF_ModelIndex,
    .soundindex = PF_SoundIndex,
    .imageindex = PF_ImageIndex,

    .configstring = PF_configstring,
    .get_configstring = PF_GetConfigstring,

    .WriteChar = MSG_WriteChar,
    .WriteByte = MSG_WriteByte,
    .WriteShort = MSG_WriteShort,
    .WriteLong = MSG_WriteLong,
    .WriteFloat = PF_WriteFloat,
    .WriteString = MSG_WriteString,
    .WritePosition = MSG_WritePos,
    .WriteDir = MSG_WriteDir,
    .WriteAngle = MSG_WriteAngle,
    .DirToByte = DirToByte,

    .TagMalloc = PF_TagMalloc,
    .TagRealloc = PF_TagRealloc,
    .TagFree = Z_Free,
    .FreeTags = PF_FreeTags,

    .cvar = PF_cvar,
    .cvar_set = Cvar_UserSet,
    .cvar_forceset = Cvar_Set,

    .argc = Cmd_Argc,
    .argv = Cmd_Argv,
    .args = Cmd_RawArgs,
    .AddCommandString = PF_AddCommandString,

    .DebugGraph = SCR_DebugGraph,
    .SetAreaPortalState = PF_SetAreaPortalState,
    .AreasConnected = PF_AreasConnected,

    .GetExtension = PF_GetExtension,
};

static void *game_library;

/*
===============
SV_ShutdownGameProgs

Called when either the entire server is being killed, or
it is changing to a different game directory.
===============
*/
void SV_ShutdownGameProgs(void)
{
    if (ge) {
        ge->Shutdown();
        ge = NULL;
    }
    if (game_library) {
        Sys_FreeLibrary(game_library);
        game_library = NULL;
    }

    Z_LeakTest(TAG_FREE);
}

/*
===============
SV_InitGameProgs

Init the game subsystem for a new map
===============
*/
void SV_InitGameProgs(void)
{
    game_entry_t    entry;

    // unload anything we have now
    SV_ShutdownGameProgs();

    game_library = Sys_LoadGameLibrary();
    if (!game_library)
        Com_Error(ERR_DROP, "Failed to load game library");

    entry = Sys_GetProcAddress(game_library, "GetGameAPI");
    if (!entry)
        Com_Error(ERR_DROP, "No game library entry point found");

    // load a new game dll
    ge = entry(&game_import);
    if (!ge) {
        Com_Error(ERR_DROP, "Game library returned NULL exports");
    }

    Com_DPrintf("Game API version: %d\n", ge->apiversion);

    if (ge->apiversion != GAME_API_VERSION) {
        Com_Error(ERR_DROP, "Game library is version %d, expected %d",
                  ge->apiversion, GAME_API_VERSION);
    }

    // initialize
    ge->Init();

    // sanitize maxclients
    if (sv_maxclients->integer != svs.maxclients || sv_maxclients->value != svs.maxclients) {
        Com_Error(ERR_DROP, "Game library corrupted maxclients value");
    }

    // sanitize edict_size
    if (ge->edict_size < sizeof(edict_t) || ge->edict_size > INT_MAX / MAX_EDICTS || ge->edict_size % q_alignof(edict_t)) {
        Com_Error(ERR_DROP, "Game library returned bad size of edict_t");
    }

    // sanitize max_edicts
    if (ge->max_edicts <= svs.maxclients || ge->max_edicts > MAX_EDICTS) {
        Com_Error(ERR_DROP, "Game library returned bad number of max_edicts");
    }
}
