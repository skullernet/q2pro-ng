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
#include "common/vm.h"

const game_export_t     *ge;

static void PF_SetConfigstring(int index, const char *val);

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

    PF_SetConfigstring(i + start, name);

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
static void PF_Print(print_type_t type, const char *msg)
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
static void PF_ClientPrint(edict_t *ent, print_level_t level, const char *msg)
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
static q_noreturn void PF_Error(const char *msg)
{
    Com_Error(ERR_DROP, "Game Error: %s", msg);
}

/*
=================
PF_setmodel

Also sets mins and maxs for inline bmodels
=================
*/
static void PF_SetBrushModel(edict_t *ent, const char *name)
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
static void PF_SetConfigstring(int index, const char *val)
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

static bool PF_InVis(const vec3_t p1, const vec3_t p2, vis_t vis)
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

static unsigned PF_GetUserinfo(unsigned clientnum, char *buf, unsigned size)
{
    Q_assert_soft(clientnum < svs.maxclients);
    const client_t *cl = &svs.client_pool[clientnum];
    Q_assert_soft(cl->state > cs_zombie);
    return Q_strlcpy(buf, cl->userinfo, size);
}

static void PF_GetUsercmd(unsigned clientnum, usercmd_t *ucmd)
{
    Q_assert_soft(clientnum < svs.maxclients);
    const client_t *cl = &svs.client_pool[clientnum];
    Q_assert_soft(cl->state > cs_zombie);
    *ucmd = cl->lastcmd;
}

static unsigned PF_GetLevelName(char *buf, unsigned size)
{
    return Q_strlcpy(buf, sv.name, size);
}

static unsigned PF_GetSpawnPoint(char *buf, unsigned size)
{
    return Q_strlcpy(buf, sv.spawnpoint, size);
}

static bool PF_ParseEntityString(char *buf, unsigned size)
{
    COM_ParseToken(&sv.entitystring, buf, size);
    return sv.entitystring;
}

static void PF_LocateGameData(edict_t *edicts, unsigned edict_size, unsigned num_edicts, gclient_t *clients, unsigned client_size)
{
    Q_assert_soft(edict_size >= sizeof(edict_t));
    Q_assert_soft(edict_size <= INT_MAX / MAX_EDICTS);
    Q_assert_soft(!(edict_size % q_alignof(edict_t)));

    Q_assert_soft(num_edicts >= svs.maxclients);
    Q_assert_soft(num_edicts <= ENTITYNUM_WORLD);

    Q_assert_soft(client_size >= sizeof(gclient_t));
    Q_assert_soft(client_size <= INT_MAX / MAX_CLIENTS);
    Q_assert_soft(!(client_size % q_alignof(gclient_t)));

    svs.edicts = edicts;
    svs.edict_size = edict_size;
    svs.num_edicts = num_edicts;
    svs.clients = clients;
    svs.client_size = client_size;
}

static unsigned PF_ErrorString(int error, char *buf, unsigned size)
{
    return Q_strlcpy(buf, Q_ErrorString(error), size);
}

static int64_t PF_RealTime(void)
{
    return time(NULL);
}

static bool PF_LocalTime(int64_t in, vm_time_t *out)
{
    time_t t = in;
    if (t != in)
        return false;

    struct tm *tm = localtime(&t);
    if (!tm)
        return false;

    out->tm_sec = tm->tm_sec;
    out->tm_min = tm->tm_min;
    out->tm_hour = tm->tm_hour;
    out->tm_mday = tm->tm_mday;
    out->tm_mon = tm->tm_mon;
    out->tm_year = tm->tm_year;
    out->tm_wday = tm->tm_wday;
    out->tm_yday = tm->tm_yday;
    out->tm_isdst = tm->tm_isdst;
    return true;
}

#if 0
static void PF_GetConnectinfo(int clientnum, char *buf, unsigned size)
{
    Q_snprintf(buf, size,
               "\\challenge\\%d\\ip\\%s"
               "\\major\\%d\\minor\\%d\\netchan\\%d"
               "\\packetlen\\%d\\qport\\%d\\zlib\\%d",
               params->challenge, NET_AdrToString(&net_from),
               params->protocol, params->version, params->nctype,
               params->maxlength, params->qport, params->has_zlib);
}
#endif

#define VM_ENT(arg) VM_PTR(arg, edict_t)
#define VM_ENT_NULL(arg) VM_PTR_NULL(arg, edict_t, 1)

VM_THUNK(Print) {
    PF_Print(VM_U32(0), VM_STR(1));
}

VM_THUNK(Error) {
    PF_Error(VM_STR(0));
}

VM_THUNK(SetConfigstring) {
    PF_SetConfigstring(VM_U32(0), VM_STR_NULL(1));
}

VM_THUNK(GetConfigstring) {
    VM_U32(0) = PF_GetConfigstring(VM_U32(0), VM_STR_BUF(1, 2), VM_U32(2));
}

VM_THUNK(FindIndex) {
    VM_U32(0) = PF_FindIndex(VM_STR(0), VM_U32(1), VM_U32(2), VM_U32(3));
}

VM_THUNK(Trace) {
    SV_Trace(VM_PTR(0, trace_t), VM_VEC3(1), VM_VEC3_NULL(2), VM_VEC3_NULL(3), VM_VEC3(4), VM_U32(5), VM_U32(6));
}

VM_THUNK(Clip) {
    SV_Clip(VM_PTR(0, trace_t), VM_VEC3(1), VM_VEC3_NULL(2), VM_VEC3_NULL(3), VM_VEC3(4), VM_U32(5), VM_U32(6));
}

VM_THUNK(PointContents) {
    VM_U32(0) = SV_PointContents(VM_VEC3(0));
}

VM_THUNK(InVis) {
    VM_U32(0) = PF_InVis(VM_VEC3(0), VM_VEC3(1), VM_U32(2));
}

VM_THUNK(SetAreaPortalState) {
    PF_SetAreaPortalState(VM_U32(0), VM_U32(1));
}

VM_THUNK(AreasConnected) {
    VM_U32(0) = PF_AreasConnected(VM_U32(0), VM_U32(1));
}

VM_THUNK(SetBrushModel) {
    PF_SetBrushModel(VM_ENT(0), VM_STR(1));
}

VM_THUNK(LinkEntity) {
    PF_LinkEdict(VM_ENT(0));
}

VM_THUNK(UnlinkEntity) {
    PF_UnlinkEdict(VM_ENT(0));
}

VM_THUNK(BoxEdicts) {
    VM_U32(0) = SV_AreaEdicts(VM_VEC3(0), VM_VEC3(1), VM_PTR_CNT(2, int, VM_U32(3)), VM_U32(3), VM_U32(4));
}

VM_THUNK(ClientPrint) {
    PF_ClientPrint(VM_ENT_NULL(0), VM_U32(1), VM_STR(2));
}

VM_THUNK(ClientLayout) {
    PF_ClientLayout(VM_ENT_NULL(0), VM_STR(1), VM_U32(2));
}

VM_THUNK(ClientStuffText) {
    PF_ClientStuffText(VM_ENT(0), VM_STR(1));
}

VM_THUNK(ClientInventory) {
    PF_ClientInventory(VM_ENT(0), VM_PTR_CNT(1, int, VM_U32(2)), VM_U32(2));
}

VM_THUNK(DirToByte) {
    VM_U32(0) = DirToByte(VM_VEC3(0));
}

VM_THUNK(Cvar_Register) {
    VM_U32(0) = PF_Cvar_Register(VM_PTR_NULL(0, vm_cvar_t, 1), VM_STR(1), VM_STR_NULL(2), VM_U32(3));
}

VM_THUNK(Cvar_Set) {
    PF_Cvar_Set(VM_STR(0), VM_STR(1));
}

VM_THUNK(Cvar_ForceSet) {
    PF_Cvar_Set(VM_STR(0), VM_STR(1));
}

VM_THUNK(Cvar_VariableInteger) {
    VM_U32(0) = Cvar_VariableInteger(VM_STR(0));
}

VM_THUNK(Cvar_VariableValue) {
    VM_F32(0) = Cvar_VariableValue(VM_STR(0));
}

VM_THUNK(Cvar_VariableString) {
    VM_U32(0) = PF_Cvar_VariableString(VM_STR(0), VM_STR_BUF(1, 2), VM_U32(2));
}

VM_THUNK(Argc) {
    VM_U32(0) = Cmd_Argc();
}

VM_THUNK(Argv) {
    VM_U32(0) = PF_Argv(VM_U32(0), VM_STR_BUF(1, 2), VM_U32(2));
}

VM_THUNK(Args) {
    VM_U32(0) = PF_Args(VM_STR_BUF(0, 1), VM_U32(1));
}

VM_THUNK(AddCommandString) {
    PF_AddCommandString(VM_STR(0));
}

VM_THUNK(GetPathToGoal) {
    VM_U32(0) = Nav_GetPathToGoal(VM_PTR(0, PathRequest), VM_PTR(1, PathInfo));
}

VM_THUNK(LocateGameData) {
    edict_t *edicts = VM_GetPointer(m, VM_U32(0), VM_U32(1), VM_U32(2), q_alignof(*edicts));
    gclient_t *clients = VM_GetPointer(m, VM_U32(3), VM_U32(4), svs.maxclients, q_alignof(*clients));
    PF_LocateGameData(edicts, VM_U32(1), VM_U32(2), clients, VM_U32(4));
}

VM_THUNK(ParseEntityString) {
    VM_U32(0) = PF_ParseEntityString(VM_STR_BUF(0, 1), VM_U32(1));
}

VM_THUNK(GetLevelName) {
    VM_U32(0) = PF_GetLevelName(VM_STR_BUF(0, 1), VM_U32(1));
}

VM_THUNK(GetSpawnPoint) {
    VM_U32(0) = PF_GetSpawnPoint(VM_STR_BUF(0, 1), VM_U32(1));
}

VM_THUNK(GetUserinfo) {
    VM_U32(0) = PF_GetUserinfo(VM_U32(0), VM_STR_BUF(1, 2), VM_U32(2));
}

VM_THUNK(GetUsercmd) {
    PF_GetUsercmd(VM_U32(0), VM_PTR(1, usercmd_t));
}

VM_THUNK(FS_OpenFile) {
    VM_U64(0) = FS_OpenFile(VM_STR(0), VM_PTR(1, qhandle_t), VM_U32(2));
}

VM_THUNK(FS_CloseFile) {
    VM_I32(0) = FS_CloseFile(VM_U32(0));
}

VM_THUNK(FS_ReadFile) {
    VM_I32(0) = FS_Read(VM_STR_BUF(0, 1), VM_U32(1), VM_U32(2));
}

VM_THUNK(FS_WriteFile) {
    VM_I32(0) = FS_Write(VM_STR_BUF(0, 1), VM_U32(1), VM_U32(2));
}

VM_THUNK(FS_ReadLine) {
    VM_I32(0) = FS_ReadLine(VM_U32(0), VM_STR_BUF(1, 2), VM_U32(2));
}

VM_THUNK(FS_ErrorString) {
    VM_U32(0) = PF_ErrorString(VM_U32(0), VM_STR_BUF(1, 2), VM_U32(2));
}

VM_THUNK(R_ClearDebugLines) {
    R_ClearDebugLines();
}

VM_THUNK(R_AddDebugBounds) {
    R_AddDebugBounds(VM_VEC3(0), VM_VEC3(1), VM_U32(2), VM_U32(3), VM_U32(4));
}

VM_THUNK(R_AddDebugText) {
    R_AddDebugText(VM_VEC3(0), VM_VEC3(1), VM_STR(2), VM_F32(3), VM_U32(4), VM_U32(5), VM_U32(6));
}

VM_THUNK(RealTime) {
    VM_I64(0) = PF_RealTime();
}

VM_THUNK(LocalTime) {
    VM_U32(0) = PF_LocalTime(VM_I64(0), VM_PTR(1, vm_time_t));
}

VM_THUNK(sinf) {
    VM_F32(0) = sinf(VM_F32(0));
}

VM_THUNK(cosf) {
    VM_F32(0) = cosf(VM_F32(0));
}

VM_THUNK(tanf) {
    VM_F32(0) = tanf(VM_F32(0));
}

VM_THUNK(asinf) {
    VM_F32(0) = asinf(VM_F32(0));
}

VM_THUNK(acosf) {
    VM_F32(0) = acosf(VM_F32(0));
}

VM_THUNK(atan2f) {
    VM_F32(0) = atan2f(VM_F32(0), VM_F32(1));
}

VM_THUNK(memcmp) {
    uint32_t p1   = VM_U32(0);
    uint32_t p2   = VM_U32(1);
    uint32_t size = VM_U32(2);

    ASSERT((uint64_t)p1 + size <= m->pages * VM_PAGE_SIZE &&
           (uint64_t)p2 + size <= m->pages * VM_PAGE_SIZE, "Memory compare out of bounds");

    VM_I32(0) = memcmp(m->bytes + p1, m->bytes + p2, size);
}

static const vm_import_t game_vm_import[] = {
    VM_IMPORT(Print, "ii"),
    VM_IMPORT(Error, "i"),
    VM_IMPORT(SetConfigstring, "ii"),
    VM_IMPORT(GetConfigstring, "i iiii"),
    VM_IMPORT(FindIndex, "i iiii"),
    VM_IMPORT(Trace, "iiiiiii"),
    VM_IMPORT(Clip, "iiiiiii"),
    VM_IMPORT(PointContents, "i i"),
    VM_IMPORT(InVis, "i iii"),
    VM_IMPORT(SetAreaPortalState, "ii"),
    VM_IMPORT(AreasConnected, "i ii"),
    VM_IMPORT(SetBrushModel, "ii"),
    VM_IMPORT(LinkEntity, "i"),
    VM_IMPORT(UnlinkEntity, "i"),
    VM_IMPORT(BoxEdicts, "i iiiii"),
    VM_IMPORT(ClientPrint, "iii"),
    VM_IMPORT(ClientLayout, "iii"),
    VM_IMPORT(ClientStuffText, "ii"),
    VM_IMPORT(ClientInventory, "iii"),
    VM_IMPORT(DirToByte, "i i"),
    VM_IMPORT(Cvar_Register, "i iiii"),
    VM_IMPORT(Cvar_Set, "ii"),
    VM_IMPORT(Cvar_ForceSet, "ii"),
    VM_IMPORT(Cvar_VariableInteger, "i i"),
    VM_IMPORT(Cvar_VariableValue, "f i"),
    VM_IMPORT(Cvar_VariableString, "i iii"),
    VM_IMPORT(Argc, "i "),
    VM_IMPORT(Argv, "i iii"),
    VM_IMPORT(Args, "i ii"),
    VM_IMPORT(AddCommandString, "i"),
    VM_IMPORT(GetPathToGoal, "i ii"),
    VM_IMPORT(LocateGameData, "iiiii"),
    VM_IMPORT(ParseEntityString, "i ii"),
    VM_IMPORT(GetLevelName, "i ii"),
    VM_IMPORT(GetSpawnPoint, "i ii"),
    VM_IMPORT(GetUserinfo, "i iii"),
    VM_IMPORT(GetUsercmd, "ii"),
    VM_IMPORT(FS_OpenFile, "I iii"),
    VM_IMPORT(FS_CloseFile, "i i"),
    VM_IMPORT(FS_ReadFile, "i iii"),
    VM_IMPORT(FS_WriteFile, "i iii"),
    VM_IMPORT(FS_ReadLine, "i iii"),
    VM_IMPORT(FS_ErrorString, "i iii"),
    VM_IMPORT(R_ClearDebugLines, ""),
    VM_IMPORT(R_AddDebugBounds, "iiiii"),
    VM_IMPORT(R_AddDebugText, "iiifiii"),
    VM_IMPORT(RealTime, "I "),
    VM_IMPORT(LocalTime, "i Ii"),

    VM_IMPORT_RAW(thunk_sinf, "sinf", "f f"),
    VM_IMPORT_RAW(thunk_cosf, "cosf", "f f"),
    VM_IMPORT_RAW(thunk_tanf, "tanf", "f f"),
    VM_IMPORT_RAW(thunk_asinf, "asinf", "f f"),
    VM_IMPORT_RAW(thunk_acosf, "acosf", "f f"),
    VM_IMPORT_RAW(thunk_atan2f, "atan2f", "f ff"),
    VM_IMPORT_RAW(thunk_memcmp, "memcmp", "i iii"),

    { 0 }
};

//==============================================

typedef enum {
    G_Init,
    G_Shutdown,
    G_SpawnEntities,
    G_WriteGame,
    G_ReadGame,
    G_WriteLevel,
    G_ReadLevel,
    G_CanSave,
    G_ClientConnect,
    G_ClientBegin,
    G_ClientUserinfoChanged,
    G_ClientDisconnect,
    G_ClientCommand,
    G_ClientThink,
    G_PrepFrame,
    G_RunFrame,
    G_ServerCommand,
    G_RestartFilesystem,
    G_NumEntries
} game_entry_enum_t;

typedef struct {
    const char *name;
    const char *type;
} vm_export_t;

#define VM_EXPORT(name, type) \
    { #name, type }

static const vm_export_t game_vm_exports[G_NumEntries] = {
    VM_EXPORT(G_Init, ""),
    VM_EXPORT(G_Shutdown, ""),
    VM_EXPORT(G_SpawnEntities, ""),
    VM_EXPORT(G_WriteGame, "ii"),
    VM_EXPORT(G_ReadGame, "i"),
    VM_EXPORT(G_WriteLevel, "i"),
    VM_EXPORT(G_ReadLevel, "i"),
    VM_EXPORT(G_CanSave, "i "),
    VM_EXPORT(G_ClientConnect, "i i"),
    VM_EXPORT(G_ClientBegin, "i"),
    VM_EXPORT(G_ClientUserinfoChanged, "i"),
    VM_EXPORT(G_ClientDisconnect, "i"),
    VM_EXPORT(G_ClientCommand, "i"),
    VM_EXPORT(G_ClientThink, "i"),
    VM_EXPORT(G_PrepFrame, ""),
    VM_EXPORT(G_RunFrame, ""),
    VM_EXPORT(G_ServerCommand, ""),
    VM_EXPORT(G_RestartFilesystem, ""),
};

static int game_exports[G_NumEntries];
static vm_t *game_vm;

static void vm_G_Init(void) {
    VM_Call(game_vm, game_exports[G_Init]);
}

static void vm_G_Shutdown(void) {
    VM_Call(game_vm, game_exports[G_Shutdown]);
}

static void vm_G_SpawnEntities(void) {
    VM_Call(game_vm, game_exports[G_SpawnEntities]);
}

static void vm_G_WriteGame(qhandle_t handle, bool autosave) {
    vm_value_t *stack = VM_StackTop(game_vm);
    VM_U32(0) = handle;
    VM_U32(1) = autosave;
    VM_Call(game_vm, game_exports[G_WriteGame]);
}

static void call_single(game_entry_enum_t entry, uint32_t arg)
{
    vm_value_t *stack = VM_StackTop(game_vm);
    VM_U32(0) = arg;
    VM_Call(game_vm, game_exports[entry]);
}

static void vm_G_ReadGame(qhandle_t handle) {
    call_single(G_ReadGame, handle);
}

static void vm_G_WriteLevel(qhandle_t handle) {
    call_single(G_WriteLevel, handle);
}

static void vm_G_ReadLevel(qhandle_t handle) {
    call_single(G_ReadLevel, handle);
}

static bool vm_G_CanSave(void) {
    VM_Call(game_vm, game_exports[G_CanSave]);
    vm_value_t *stack = VM_StackTop(game_vm);
    return VM_U32(0);
}

static const char *vm_G_ClientConnect(int clientnum) {
    call_single(G_ClientConnect, clientnum);
    vm_value_t *stack = VM_StackTop(game_vm);
    vm_memory_t *m = VM_Memory(game_vm);
    return VM_STR_NULL(0);
}

static void vm_G_ClientBegin(int clientnum) {
    call_single(G_ClientBegin, clientnum);
}

static void vm_G_ClientUserinfoChanged(int clientnum) {
    call_single(G_ClientUserinfoChanged, clientnum);
}

static void vm_G_ClientDisconnect(int clientnum) {
    call_single(G_ClientDisconnect, clientnum);
}

static void vm_G_ClientCommand(int clientnum) {
    call_single(G_ClientCommand, clientnum);
}

static void vm_G_ClientThink(int clientnum) {
    call_single(G_ClientThink, clientnum);
}

static void vm_G_PrepFrame(void) {
    VM_Call(game_vm, game_exports[G_PrepFrame]);
}

static void vm_G_RunFrame(void) {
    VM_Call(game_vm, game_exports[G_RunFrame]);
}

static void vm_G_ServerCommand(void) {
    VM_Call(game_vm, game_exports[G_ServerCommand]);
}

static void vm_G_RestartFilesystem(void) {
    VM_Call(game_vm, game_exports[G_RestartFilesystem]);
}

static const game_export_t game_vm_export = {
    .Init = vm_G_Init,
    .Shutdown = vm_G_Shutdown,
    .SpawnEntities = vm_G_SpawnEntities,
    .WriteGame = vm_G_WriteGame,
    .ReadGame = vm_G_ReadGame,
    .WriteLevel = vm_G_WriteLevel,
    .ReadLevel = vm_G_ReadLevel,
    .CanSave = vm_G_CanSave,
    .ClientConnect = vm_G_ClientConnect,
    .ClientBegin = vm_G_ClientBegin,
    .ClientUserinfoChanged = vm_G_ClientUserinfoChanged,
    .ClientDisconnect = vm_G_ClientDisconnect,
    .ClientCommand = vm_G_ClientCommand,
    .ClientThink = vm_G_ClientThink,
    .PrepFrame = vm_G_PrepFrame,
    .RunFrame = vm_G_RunFrame,
    .ServerCommand = vm_G_ServerCommand,
    .RestartFilesystem = vm_G_RestartFilesystem,
};

//==============================================

static const game_import_t game_import = {
    .apiversion = GAME_API_VERSION,
    .structsize = sizeof(game_import_t),

    .Print = PF_Print,
    .Error = PF_Error,

    .LinkEntity = PF_LinkEdict,
    .UnlinkEntity = PF_UnlinkEdict,
    .BoxEdicts = SV_AreaEdicts,
    .Trace = SV_Trace,
    .Clip = SV_Clip,
    .PointContents = SV_PointContents,
    .SetBrushModel = PF_SetBrushModel,
    .InVis = PF_InVis,

    .FindIndex = PF_FindIndex,
    .SetConfigstring = PF_SetConfigstring,
    .GetConfigstring = PF_GetConfigstring,

    .ClientPrint = PF_ClientPrint,
    .ClientStuffText = PF_ClientStuffText,
    .ClientLayout = PF_ClientLayout,
    .ClientInventory = PF_ClientInventory,
    .DirToByte = DirToByte,

    .Cvar_Register = PF_Cvar_Register,
    .Cvar_Set = PF_Cvar_Set,
    .Cvar_ForceSet = PF_Cvar_ForceSet,
    .Cvar_VariableInteger = Cvar_VariableInteger,
    .Cvar_VariableValue = Cvar_VariableValue,
    .Cvar_VariableString = PF_Cvar_VariableString,

    .Argc = Cmd_Argc,
    .Argv = PF_Argv,
    .Args = PF_Args,
    .AddCommandString = PF_AddCommandString,

    .SetAreaPortalState = PF_SetAreaPortalState,
    .AreasConnected = PF_AreasConnected,

    .GetPathToGoal = Nav_GetPathToGoal,

    .LocateGameData = PF_LocateGameData,
    .ParseEntityString = PF_ParseEntityString,
    .GetLevelName = PF_GetLevelName,
    .GetSpawnPoint = PF_GetSpawnPoint,
    .GetUserinfo = PF_GetUserinfo,
    .GetUsercmd = PF_GetUsercmd,

    .FS_OpenFile = FS_OpenFile,
    .FS_CloseFile = FS_CloseFile,
    .FS_ReadFile = FS_Read,
    .FS_WriteFile = FS_Write,
    .FS_FlushFile = FS_Flush,
    .FS_TellFile = FS_Tell,
    .FS_SeekFile = FS_Seek,
    .FS_ReadLine = FS_ReadLine,
    .FS_ErrorString = PF_ErrorString,

#if USE_REF && USE_DEBUG
    .R_ClearDebugLines = R_ClearDebugLines,
    .R_AddDebugLine = R_AddDebugLine,
    .R_AddDebugPoint = R_AddDebugPoint,
    .R_AddDebugAxis = R_AddDebugAxis,
    .R_AddDebugBounds = R_AddDebugBounds,
    .R_AddDebugSphere = R_AddDebugSphere,
    .R_AddDebugCircle = R_AddDebugCircle,
    .R_AddDebugCylinder = R_AddDebugCylinder,
    .R_AddDebugArrow = R_AddDebugArrow,
    .R_AddDebugCurveArrow = R_AddDebugCurveArrow,
    .R_AddDebugText = R_AddDebugText,
#endif

    .RealTime = PF_RealTime,
    .LocalTime = PF_LocalTime,
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
    if (game_vm) {
        VM_Free(game_vm);
        game_vm = NULL;
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

    vm_t *vm = VM_Load("vm/game.qvm", game_vm_import);
    if (vm) {
        game_vm = vm;
        ge = &game_vm_export;
        for (int i = 0; i < G_NumEntries; i++) {
            game_exports[i] = VM_GetExport(vm, game_vm_exports[i].name);
            ASSERT(game_exports[i] >= 0, "Export %s not found", game_vm_exports[i].name);
        }
    } else {
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
    }

    // initialize
    ge->Init();

    // sanitize maxclients
    if (sv_maxclients->integer != svs.maxclients || sv_maxclients->value != svs.maxclients) {
        Com_Error(ERR_DROP, "Game library corrupted maxclients value");
    }
}
