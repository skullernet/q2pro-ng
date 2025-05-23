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
static int PF_FindIndex(const char *name, int start, int max, int skip)
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
        Com_Error(ERR_DROP, "%s(%s): overflow", __func__, name);

    PF_configstring(i + start, name);

    return i;
}

static client_t *get_client(edict_t *ent, const char *func)
{
    int clientNum = SV_NumForEdict(ent);
    if (clientNum < 0 || clientNum >= svs.maxclients) {
        Com_DWPrintf("%s to a non-client %d\n", func, clientNum);
        return NULL;
    }

    client_t *client = svs.client_pool + clientNum;
    if (client->state <= cs_zombie) {
        Com_DWPrintf("%s to a free/zombie client %d\n", func, clientNum);
        return NULL;
    }

    return client;
}


static void PF_ClientLayout(edict_t *ent, const char *str, bool reliable)
{
    client_t *client = NULL;
    int flags = reliable ? MSG_RELIABLE : 0;

    if (ent) {
        client = get_client(ent, __func__);
        if (!client)
            return;
    }

    MSG_WriteByte(svc_layout);
    MSG_WriteString(str);

    if (client) {
        SV_ClientAddMessage(client, flags | MSG_CLEAR);
        return;
    }

    FOR_EACH_CLIENT(client)
        if (client->state == cs_spawned)
            SV_ClientAddMessage(client, flags);

    SZ_Clear(&msg_write);
}

static void PF_ClientStuffText(edict_t *ent, const char *str)
{
    client_t *client = get_client(ent, __func__);
    if (!client)
        return;

    MSG_WriteByte(svc_stufftext);
    MSG_WriteString(str);

    SV_ClientAddMessage(client, MSG_RELIABLE | MSG_CLEAR);
}

static void PF_ClientInventory(edict_t *ent, int *inventory, int count)
{
    client_t *client = get_client(ent, __func__);
    if (!client)
        return;

    MSG_WriteByte(svc_inventory);
    MSG_WriteByte(count);
    for (int i = 0; i < count; i++)
         MSG_WriteShort(inventory[i]);

    SV_ClientAddMessage(client, MSG_RELIABLE | MSG_CLEAR);
}

/*
===============
PF_dprintf

Debug print to server console.
===============
*/
static void PF_dprint(print_type_t type, const char *msg)
{
    Con_SkipNotify(true);
    Com_LPrintf(type, "%s", msg);
    Con_SkipNotify(false);
}

/*
===============
PF_cprintf

Print to a single client if the level passes.
===============
*/
static void PF_cprint(edict_t *ent, print_level_t level, const char *msg)
{
    client_t *client = NULL;

    if (ent) {
        client = get_client(ent, __func__);
        if (!client || level < client->messagelevel)
            return;
    }

    MSG_WriteByte(svc_print);
    MSG_WriteByte(level);
    MSG_WriteString(msg);

    if (client) {
        SV_ClientAddMessage(client, MSG_RELIABLE | MSG_CLEAR);
        return;
    }

    // echo to console
    if (COM_DEDICATED)
        Com_Printf("%s", msg);

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
PF_error

Abort the server with a game error
===============
*/
static q_noreturn void PF_error(const char *msg)
{
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
    const bsp_t *bsp = sv.cm.cache;
    const mmodel_t *mod;
    int num;

    if (!ent || !name)
        Com_Error(ERR_DROP, "%s: NULL", __func__);

    if (name[0] != '*')
        Com_Error(ERR_DROP, "%s: not an inline model: %s", __func__, name);

    if (!bsp)
        Com_Error(ERR_DROP, "%s: no map loaded", __func__);

    num = Q_atoi(name + 1);
    if (num < 1 || num >= bsp->nummodels)
        Com_Error(ERR_DROP, "%s: bad inline model: %d", __func__, num);

    ent->s.modelindex = num;
    mod = &bsp->models[num];

// if it is an inline model, get the size information for it
    VectorCopy(mod->mins, ent->r.mins);
    VectorCopy(mod->maxs, ent->r.maxs);
    PF_LinkEdict(ent);
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

static unsigned PF_GetConfigstring(int index, char *buf, unsigned size)
{
    if (index < 0 || index >= MAX_CONFIGSTRINGS)
        Com_Error(ERR_DROP, "%s: bad index: %d", __func__, index);

    return Q_strlcpy(buf, sv.configstrings[index], size);
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

static bool PF_Cvar_Register(vm_cvar_t *var, const char *name, const char *value, int flags)
{
    if (flags & CVAR_EXTENDED_MASK) {
        Com_WPrintf("Game attempted to set extended flags on '%s', masked out.\n", name);
        flags &= ~CVAR_EXTENDED_MASK;
    }

    cvar_t *cv = Cvar_Get(name, value, flags | CVAR_GAME);
    if (!cv)
        return false;
    if (!var)
        return true;

    var->integer = cv->integer;
    var->value = cv->value;
    Q_strlcpy(var->string, cv->string, sizeof(var->string));
    return true;
}

static void PF_Cvar_Set(const char *name, const char *value)
{
    Cvar_UserSet(name, value);
}

static void PF_Cvar_ForceSet(const char *name, const char *value)
{
    Cvar_Set(name, value);
}

static unsigned PF_Cvar_VariableString(const char *name, char *buf, unsigned size)
{
    return Q_strlcpy(buf, Cvar_VariableString(name), size);
}

static void PF_AddCommandString(const char *string)
{
#if USE_CLIENT
    if (!strcmp(string, "menu_loadgame\n"))
        string = "pushmenu loadgame\n";
#endif
    Cbuf_AddText(&cmd_buffer, string);
}

static unsigned PF_Argv(int arg, char *buf, unsigned size)
{
    return Q_strlcpy(buf, Cmd_Argv(arg), size);
}

static unsigned PF_Args(char *buf, unsigned size)
{
    return Q_strlcpy(buf, Cmd_RawArgs(), size);
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

    .dprint = PF_dprint,
    .cprint = PF_cprint,
    .error = PF_error,

    .linkentity = PF_LinkEdict,
    .unlinkentity = PF_UnlinkEdict,
    .BoxEdicts = SV_AreaEdicts,
    .trace = SV_Trace,
    .clip = SV_Clip,
    .pointcontents = SV_PointContents,
    .setmodel = PF_setmodel,
    .inVIS = PF_inVIS,

    .findindex = PF_FindIndex,
    .configstring = PF_configstring,
    .get_configstring = PF_GetConfigstring,

    .ClientStuffText = PF_ClientStuffText,
    .ClientLayout = PF_ClientLayout,
    .ClientInventory = PF_ClientInventory,
    .DirToByte = DirToByte,

    .TagMalloc = PF_TagMalloc,
    .TagRealloc = PF_TagRealloc,
    .TagFree = Z_Free,
    .FreeTags = PF_FreeTags,

    .Cvar_Register = PF_Cvar_Register,
    .Cvar_Set = PF_Cvar_Set,
    .Cvar_ForceSet = PF_Cvar_ForceSet,
    .Cvar_VariableInteger = Cvar_VariableInteger,
    .Cvar_VariableValue = Cvar_VariableValue,
    .Cvar_VariableString = PF_Cvar_VariableString,

    .argc = Cmd_Argc,
    .argv = PF_Argv,
    .args = PF_Args,
    .AddCommandString = PF_AddCommandString,

    .DebugGraph = SCR_DebugGraph,
    .SetAreaPortalState = PF_SetAreaPortalState,
    .AreasConnected = PF_AreasConnected,

    .GetExtension = PF_GetExtension,
    .GetPathToGoal = Nav_GetPathToGoal,
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
