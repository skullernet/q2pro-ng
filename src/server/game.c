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

static vm_module_t      game;
const game_export_t     *ge;

static void PF_ClientCommand(edict_t *ent, const char *str, bool reliable)
{
    client_t *client = NULL;
    int flags = reliable ? MSG_RELIABLE : 0;

    if (ent) {
        int clientNum = SV_NumForEdict(ent);
        if (clientNum < 0 || clientNum >= svs.maxclients) {
            Com_DWPrintf("%s to a non-client %d\n", __func__, clientNum);
            return;
        }

        client = svs.client_pool + clientNum;
        if (client->state <= cs_zombie) {
            Com_DWPrintf("%s to a free/zombie client %d\n", __func__, clientNum);
            return;
        }
    }

    MSG_WriteByte(svc_stringcmd);
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
static void PF_SetConfigstring(unsigned index, const char *val)
{
    size_t len, maxlen;
    client_t *client;
    char *dst;

    if (index >= MAX_CONFIGSTRINGS)
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

static size_t PF_GetConfigstring(unsigned index, char *buf, size_t size)
{
    if (index >= MAX_CONFIGSTRINGS)
        Com_Error(ERR_DROP, "%s: bad index: %d", __func__, index);

    return Q_strlcpy(buf, sv.configstrings[index], size);
}

static int PF_FindConfigstring(const char *name, int start, int max, int skip)
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

static bool PF_InVis(const vec3_t p1, const vec3_t p2, vis_t vis)
{
    return CM_InVis(&sv.cm, p1, p2, vis);
}

static bool PF_Cvar_Register(vm_cvar_t *var, const char *name, const char *value, unsigned flags)
{
    if (flags & CVAR_EXTENDED_MASK) {
        Com_WPrintf("Game attempted to set extended flags on '%s', masked out.\n", name);
        flags &= ~CVAR_EXTENDED_MASK;
    }

    return VM_RegisterCvar(&game, var, name, value, flags | CVAR_GAME);
}

static void PF_Cvar_Set(const char *name, const char *value)
{
    Cvar_UserSet(name, value);
}

static void PF_Cvar_ForceSet(const char *name, const char *value)
{
    Cvar_Set(name, value);
}

static void PF_AddCommandString(const char *string)
{
#if USE_CLIENT
    if (!strcmp(string, "menu_loadgame\n"))
        string = "pushmenu loadgame\n";
#endif
    Cbuf_AddText(&cmd_buffer, string);
}

static void PF_SetAreaPortalState(unsigned portalnum, bool open)
{
    CM_SetAreaPortalState(&sv.cm, portalnum, open);
}

static bool PF_AreasConnected(int area1, int area2)
{
    return CM_AreasConnected(&sv.cm, area1, area2);
}

static size_t PF_GetUserinfo(unsigned clientnum, char *buf, size_t size)
{
    Q_assert_soft(clientnum < svs.maxclients);
    const client_t *cl = &svs.client_pool[clientnum];
    Q_assert_soft(cl->state > cs_zombie);
    return Q_strlcpy(buf, cl->userinfo, size);
}

static size_t PF_GetConnectinfo(unsigned clientnum, char *buf, size_t size)
{
    Q_assert_soft(clientnum < svs.maxclients);
    const client_t *cl = &svs.client_pool[clientnum];
    Q_assert_soft(cl->state > cs_zombie);

    return Q_snprintf(buf, size,
                      "\\challenge\\%d\\ip\\%s\\major\\%d\\minor\\%d\\zlib\\%d",
                      cl->challenge, NET_AdrToString(&cl->netchan.remote_address),
                      cl->protocol, cl->version, cl->has_zlib);
}

static void PF_GetUsercmd(unsigned clientnum, usercmd_t *ucmd)
{
    Q_assert_soft(clientnum < svs.maxclients);
    const client_t *cl = &svs.client_pool[clientnum];
    Q_assert_soft(cl->state > cs_zombie);
    *ucmd = cl->lastcmd;
}

static size_t PF_GetLevelName(char *buf, size_t size)
{
    return Q_strlcpy(buf, sv.name, size);
}

static size_t PF_GetSpawnPoint(char *buf, size_t size)
{
    return Q_strlcpy(buf, sv.spawnpoint, size);
}

static bool PF_ParseEntityString(char *buf, size_t size)
{
    COM_ParseToken(&sv.entitystring, buf, size);
    return sv.entitystring;
}

static bool PF_GetSurfaceInfo(unsigned surf_id, surface_info_t *info)
{
    return BSP_SurfaceInfo(sv.cm.cache, surf_id, info);
}

static void PF_LocateGameData(edict_t *edicts, size_t edict_size, unsigned num_edicts, gclient_t *clients, size_t client_size)
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

static int64_t PF_OpenFile(const char *path, qhandle_t *f, unsigned mode)
{
    return VM_OpenFile(&game, path, f, mode);
}

static int PF_CloseFile(qhandle_t f)
{
    return VM_CloseFile(&game, f);
}

static size_t PF_ListFiles(const char *path, const char *filter, unsigned flags, char *buffer, size_t size)
{
    return 0;
}

// edict pointers need custom validation
static inline edict_t *VM_GetEntity(const vm_memory_t *m, uint32_t ptr, const char *func)
{
    VM_ASSERT2(ptr >= svs.vm_edicts_minptr &&
               ptr <= svs.vm_edicts_maxptr, "Out of bounds VM edict");
    VM_ASSERT2(!(ptr % q_alignof(edict_t)), "Misaligned VM edict");
    return (edict_t *)(m->bytes + ptr);
}

#define VM_ENT(arg)      VM_GetEntity(m, VM_U32(arg), __func__)
#define VM_ENT_NULL(arg) (VM_U32(arg) ? VM_GetEntity(m, VM_U32(arg), __func__) : NULL)

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

VM_THUNK(FindConfigstring) {
    VM_U32(0) = PF_FindConfigstring(VM_STR(0), VM_U32(1), VM_U32(2), VM_U32(3));
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

VM_THUNK(BoxEdicts) {
    VM_U32(0) = SV_AreaEdicts(VM_VEC3(0), VM_VEC3(1), VM_PTR_CNT(2, int, VM_U32(3)), VM_U32(3), VM_U32(4));
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

VM_THUNK(LinkEntity) {
    PF_LinkEdict(VM_ENT(0));
}

VM_THUNK(UnlinkEntity) {
    PF_UnlinkEdict(VM_ENT(0));
}

VM_THUNK(SetBrushModel) {
    PF_SetBrushModel(VM_ENT(0), VM_STR(1));
}

VM_THUNK(ClientCommand) {
    PF_ClientCommand(VM_ENT_NULL(0), VM_STR(1), VM_U32(2));
}

VM_THUNK(DirToByte) {
    VM_U32(0) = DirToByte(VM_VEC3(0));
}

VM_THUNK(ByteToDir) {
    ByteToDir(VM_U32(0), VM_VEC3(1));
}

VM_THUNK(GetSurfaceInfo) {
    VM_U32(0) = PF_GetSurfaceInfo(VM_U32(0), VM_PTR(1, surface_info_t));
}

VM_THUNK(LocateGameData) {
    uint32_t edicts_ptr = VM_U32(0);
    uint32_t edict_size = VM_U32(1);
    uint32_t num_edicts = VM_U32(2);

    edict_t *edicts = VM_GetPointer(m, edicts_ptr, edict_size, MAX_EDICTS, q_alignof(*edicts), __func__);
    gclient_t *clients = VM_GetPointer(m, VM_U32(3), VM_U32(4), svs.maxclients, q_alignof(*clients), __func__);
    PF_LocateGameData(edicts, edict_size, num_edicts, clients, VM_U32(4));

    svs.vm_edicts_minptr = edicts_ptr;
    svs.vm_edicts_maxptr = edicts_ptr + (MAX_EDICTS - 1) * edict_size;
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

VM_THUNK(GetConnectinfo) {
    VM_U32(0) = PF_GetConnectinfo(VM_U32(0), VM_STR_BUF(1, 2), VM_U32(2));
}

VM_THUNK(GetUsercmd) {
    PF_GetUsercmd(VM_U32(0), VM_PTR(1, usercmd_t));
}

VM_THUNK(GetPathToGoal) {
    VM_U32(0) = Nav_GetPathToGoal(VM_PTR(0, PathRequest), VM_PTR(1, PathInfo));
}

VM_THUNK(RealTime) {
    VM_I64(0) = Com_RealTime();
}

VM_THUNK(LocalTime) {
    VM_U32(0) = Com_LocalTime(VM_I64(0), VM_PTR(1, vm_time_t));
}

VM_THUNK(Cvar_Register) {
    VM_U32(0) = PF_Cvar_Register(VM_PTR_NULL(0, vm_cvar_t), VM_STR(1), VM_STR_NULL(2), VM_U32(3));
}

VM_THUNK(Cvar_Set) {
    PF_Cvar_Set(VM_STR(0), VM_STR(1));
}

VM_THUNK(Cvar_ForceSet) {
    PF_Cvar_ForceSet(VM_STR(0), VM_STR(1));
}

VM_THUNK(Cvar_VariableInteger) {
    VM_U32(0) = Cvar_VariableInteger(VM_STR(0));
}

VM_THUNK(Cvar_VariableValue) {
    VM_F32(0) = Cvar_VariableValue(VM_STR(0));
}

VM_THUNK(Cvar_VariableString) {
    VM_U32(0) = Cvar_VariableStringBuffer(VM_STR(0), VM_STR_BUF(1, 2), VM_U32(2));
}

VM_THUNK(Argc) {
    VM_U32(0) = Cmd_Argc();
}

VM_THUNK(Argv) {
    VM_U32(0) = Cmd_ArgvBuffer(VM_U32(0), VM_STR_BUF(1, 2), VM_U32(2));
}

VM_THUNK(Args) {
    VM_U32(0) = Cmd_RawArgsBuffer(VM_STR_BUF(0, 1), VM_U32(1));
}

VM_THUNK(AddCommandString) {
    PF_AddCommandString(VM_STR(0));
}

VM_THUNK(DebugGraph) {
    SCR_DebugGraph(VM_F32(0), VM_U32(1));
}

VM_THUNK(FS_OpenFile) {
    VM_U64(0) = PF_OpenFile(VM_STR(0), VM_PTR(1, qhandle_t), VM_U32(2));
}

VM_THUNK(FS_CloseFile) {
    VM_I32(0) = PF_CloseFile(VM_U32(0));
}

VM_THUNK(FS_ReadFile) {
    VM_I32(0) = FS_Read(VM_STR_BUF(0, 1), VM_U32(1), VM_U32(2));
}

VM_THUNK(FS_WriteFile) {
    VM_I32(0) = FS_Write(VM_STR_BUF(0, 1), VM_U32(1), VM_U32(2));
}

VM_THUNK(FS_FlushFile) {
    VM_I32(0) = FS_Flush(VM_U32(0));
}

VM_THUNK(FS_TellFile) {
    VM_I64(0) = FS_Tell(VM_U32(0));
}

VM_THUNK(FS_SeekFile) {
    VM_I32(0) = FS_Seek(VM_U32(0), VM_I64(1), VM_U32(2));
}

VM_THUNK(FS_ReadLine) {
    VM_I32(0) = FS_ReadLine(VM_U32(0), VM_STR_BUF(1, 2), VM_U32(2));
}

VM_THUNK(FS_ListFiles) {
    VM_U32(0) = PF_ListFiles(VM_STR(0), VM_STR_NULL(1), VM_U32(2), VM_STR_BUF(3, 4), VM_U32(4));
}

VM_THUNK(FS_ErrorString) {
    VM_U32(0) = Q_ErrorStringBuffer(VM_U32(0), VM_STR_BUF(1, 2), VM_U32(2));
}

VM_THUNK(R_ClearDebugLines) {
    R_ClearDebugLines();
}

VM_THUNK(R_AddDebugLine) {
    R_AddDebugLine(VM_VEC3(0), VM_VEC3(1), VM_U32(2), VM_U32(3), VM_U32(4));
}

VM_THUNK(R_AddDebugPoint) {
    R_AddDebugPoint(VM_VEC3(0), VM_F32(1), VM_U32(2), VM_U32(3), VM_U32(4));
}

VM_THUNK(R_AddDebugAxis) {
    R_AddDebugAxis(VM_VEC3(0), VM_VEC3_NULL(1), VM_F32(2), VM_U32(3), VM_U32(4));
}

VM_THUNK(R_AddDebugBounds) {
    R_AddDebugBounds(VM_VEC3(0), VM_VEC3(1), VM_U32(2), VM_U32(3), VM_U32(4));
}

VM_THUNK(R_AddDebugSphere) {
    R_AddDebugSphere(VM_VEC3(0), VM_F32(1), VM_U32(2), VM_U32(3), VM_U32(4));
}

VM_THUNK(R_AddDebugCircle) {
    R_AddDebugCircle(VM_VEC3(0), VM_F32(1), VM_U32(2), VM_U32(3), VM_U32(4));
}

VM_THUNK(R_AddDebugCylinder) {
    R_AddDebugCylinder(VM_VEC3(0), VM_F32(1), VM_F32(2), VM_U32(3), VM_U32(4), VM_U32(5));
}

VM_THUNK(R_AddDebugArrow) {
    R_AddDebugArrow(VM_VEC3(0), VM_VEC3(1), VM_F32(2), VM_U32(3), VM_U32(4), VM_U32(5), VM_U32(6));
}

VM_THUNK(R_AddDebugCurveArrow) {
    R_AddDebugCurveArrow(VM_VEC3(0), VM_VEC3(1), VM_VEC3(2), VM_F32(3), VM_U32(4), VM_U32(5), VM_U32(6), VM_U32(7));
}

VM_THUNK(R_AddDebugText) {
    R_AddDebugText(VM_VEC3(0), VM_VEC3_NULL(1), VM_STR(2), VM_F32(3), VM_U32(4), VM_U32(5), VM_U32(6));
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

    VM_ASSERT((uint64_t)p1 + size <= m->pages * VM_PAGE_SIZE &&
              (uint64_t)p2 + size <= m->pages * VM_PAGE_SIZE, "Memory compare out of bounds");

    VM_I32(0) = memcmp(m->bytes + p1, m->bytes + p2, size);
}

static const vm_import_t game_vm_imports[] = {
    VM_IMPORT(Print, "ii"),
    VM_IMPORT(Error, "i"),
    VM_IMPORT(SetConfigstring, "ii"),
    VM_IMPORT(GetConfigstring, "i iiii"),
    VM_IMPORT(FindConfigstring, "i iiii"),
    VM_IMPORT(Trace, "iiiiiii"),
    VM_IMPORT(Clip, "iiiiiii"),
    VM_IMPORT(PointContents, "i i"),
    VM_IMPORT(BoxEdicts, "i iiiii"),
    VM_IMPORT(InVis, "i iii"),
    VM_IMPORT(SetAreaPortalState, "ii"),
    VM_IMPORT(AreasConnected, "i ii"),
    VM_IMPORT(LinkEntity, "i"),
    VM_IMPORT(UnlinkEntity, "i"),
    VM_IMPORT(SetBrushModel, "ii"),
    VM_IMPORT(ClientCommand, "iii"),
    VM_IMPORT(DirToByte, "i i"),
    VM_IMPORT(ByteToDir, "ii"),
    VM_IMPORT(GetSurfaceInfo, "i ii"),
    VM_IMPORT(LocateGameData, "iiiii"),
    VM_IMPORT(ParseEntityString, "i ii"),
    VM_IMPORT(GetLevelName, "i ii"),
    VM_IMPORT(GetSpawnPoint, "i ii"),
    VM_IMPORT(GetUserinfo, "i iii"),
    VM_IMPORT(GetConnectinfo, "i iii"),
    VM_IMPORT(GetUsercmd, "ii"),
    VM_IMPORT(GetPathToGoal, "i ii"),
    VM_IMPORT(RealTime, "I "),
    VM_IMPORT(LocalTime, "i Ii"),
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
    VM_IMPORT(DebugGraph, "fi"),
    VM_IMPORT(FS_OpenFile, "I iii"),
    VM_IMPORT(FS_CloseFile, "i i"),
    VM_IMPORT(FS_ReadFile, "i iii"),
    VM_IMPORT(FS_WriteFile, "i iii"),
    VM_IMPORT(FS_FlushFile, "i i"),
    VM_IMPORT(FS_TellFile, "I i"),
    VM_IMPORT(FS_SeekFile, "i iIi"),
    VM_IMPORT(FS_ReadLine, "i iii"),
    VM_IMPORT(FS_ListFiles, "i iiiii"),
    VM_IMPORT(FS_ErrorString, "i iii"),
    VM_IMPORT(R_ClearDebugLines, ""),
    VM_IMPORT(R_AddDebugLine, "iiiii"),
    VM_IMPORT(R_AddDebugPoint, "ifiii"),
    VM_IMPORT(R_AddDebugAxis, "iifii"),
    VM_IMPORT(R_AddDebugBounds, "iiiii"),
    VM_IMPORT(R_AddDebugSphere, "ifiii"),
    VM_IMPORT(R_AddDebugCircle, "ifiii"),
    VM_IMPORT(R_AddDebugCylinder, "iffiii"),
    VM_IMPORT(R_AddDebugArrow, "iifiiii"),
    VM_IMPORT(R_AddDebugCurveArrow, "iiifiiii"),
    VM_IMPORT(R_AddDebugText, "iiifiii"),

    VM_IMPORT_RAW(sinf, "f f"),
    VM_IMPORT_RAW(cosf, "f f"),
    VM_IMPORT_RAW(tanf, "f f"),
    VM_IMPORT_RAW(asinf, "f f"),
    VM_IMPORT_RAW(acosf, "f f"),
    VM_IMPORT_RAW(atan2f, "f ff"),
    VM_IMPORT_RAW(memcmp, "i iii"),

    { 0 }
};

//==============================================

typedef enum {
    vm_G_Init,
    vm_G_Shutdown,
    vm_G_SpawnEntities,
    vm_G_WriteGame,
    vm_G_ReadGame,
    vm_G_WriteLevel,
    vm_G_ReadLevel,
    vm_G_CanSave,
    vm_G_ClientConnect,
    vm_G_ClientBegin,
    vm_G_ClientUserinfoChanged,
    vm_G_ClientDisconnect,
    vm_G_ClientCommand,
    vm_G_ClientThink,
    vm_G_PrepFrame,
    vm_G_RunFrame,
    vm_G_ServerCommand,
    vm_G_RestartFilesystem,
} game_entry_t;

static const vm_export_t game_vm_exports[] = {
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

    { 0 }
};

static void thunk_G_Init(void) {
    VM_Call(game.vm, vm_G_Init);
}

static void thunk_G_Shutdown(void) {
    VM_Call(game.vm, vm_G_Shutdown);
}

static void thunk_G_SpawnEntities(void) {
    VM_Call(game.vm, vm_G_SpawnEntities);
}

static void thunk_G_WriteGame(qhandle_t handle, bool autosave) {
    vm_value_t *stack = VM_Push(game.vm, 2);
    VM_U32(0) = handle;
    VM_U32(1) = autosave;
    VM_Call(game.vm, vm_G_WriteGame);
}

static void call_single(game_entry_t entry, uint32_t arg) {
    vm_value_t *stack = VM_Push(game.vm, 1);
    VM_U32(0) = arg;
    VM_Call(game.vm, entry);
}

static void thunk_G_ReadGame(qhandle_t handle) {
    call_single(vm_G_ReadGame, handle);
}

static void thunk_G_WriteLevel(qhandle_t handle) {
    call_single(vm_G_WriteLevel, handle);
}

static void thunk_G_ReadLevel(qhandle_t handle) {
    call_single(vm_G_ReadLevel, handle);
}

static bool thunk_G_CanSave(void) {
    VM_Call(game.vm, vm_G_CanSave);
    const vm_value_t *stack = VM_Pop(game.vm);
    return VM_U32(0);
}

static const char *thunk_G_ClientConnect(int clientnum) {
    call_single(vm_G_ClientConnect, clientnum);
    const vm_value_t *stack = VM_Pop(game.vm);
    const vm_memory_t *m = VM_Memory(game.vm);
    return VM_STR_NULL(0);
}

static void thunk_G_ClientBegin(int clientnum) {
    call_single(vm_G_ClientBegin, clientnum);
}

static void thunk_G_ClientUserinfoChanged(int clientnum) {
    call_single(vm_G_ClientUserinfoChanged, clientnum);
}

static void thunk_G_ClientDisconnect(int clientnum) {
    call_single(vm_G_ClientDisconnect, clientnum);
}

static void thunk_G_ClientCommand(int clientnum) {
    call_single(vm_G_ClientCommand, clientnum);
}

static void thunk_G_ClientThink(int clientnum) {
    call_single(vm_G_ClientThink, clientnum);
}

static void thunk_G_PrepFrame(void) {
    VM_Call(game.vm, vm_G_PrepFrame);
}

static void thunk_G_RunFrame(void) {
    VM_Call(game.vm, vm_G_RunFrame);
}

static void thunk_G_ServerCommand(void) {
    VM_Call(game.vm, vm_G_ServerCommand);
}

static void thunk_G_RestartFilesystem(void) {
    VM_Call(game.vm, vm_G_RestartFilesystem);
}

//==============================================

static const game_import_t game_dll_imports = {
    .apiversion = GAME_API_VERSION,
    .structsize = sizeof(game_import_t),

    .Print = PF_Print,
    .Error = PF_Error,

    .SetConfigstring = PF_SetConfigstring,
    .GetConfigstring = PF_GetConfigstring,
    .FindConfigstring = PF_FindConfigstring,

    .Trace = SV_Trace,
    .Clip = SV_Clip,
    .PointContents = SV_PointContents,
    .BoxEdicts = SV_AreaEdicts,

    .InVis = PF_InVis,
    .SetAreaPortalState = PF_SetAreaPortalState,
    .AreasConnected = PF_AreasConnected,

    .LinkEntity = PF_LinkEdict,
    .UnlinkEntity = PF_UnlinkEdict,
    .SetBrushModel = PF_SetBrushModel,

    .ClientCommand = PF_ClientCommand,

    .DirToByte = DirToByte,
    .ByteToDir = ByteToDir,
    .GetSurfaceInfo = PF_GetSurfaceInfo,

    .LocateGameData = PF_LocateGameData,
    .ParseEntityString = PF_ParseEntityString,
    .GetLevelName = PF_GetLevelName,
    .GetSpawnPoint = PF_GetSpawnPoint,
    .GetUserinfo = PF_GetUserinfo,
    .GetUsercmd = PF_GetUsercmd,
    .GetPathToGoal = Nav_GetPathToGoal,

    .RealTime = Com_RealTime,
    .LocalTime = Com_LocalTime,

    .Cvar_Register = PF_Cvar_Register,
    .Cvar_Set = PF_Cvar_Set,
    .Cvar_ForceSet = PF_Cvar_ForceSet,
    .Cvar_VariableInteger = Cvar_VariableInteger,
    .Cvar_VariableValue = Cvar_VariableValue,
    .Cvar_VariableString = Cvar_VariableStringBuffer,

    .Argc = Cmd_Argc,
    .Argv = Cmd_ArgvBuffer,
    .Args = Cmd_RawArgsBuffer,
    .AddCommandString = PF_AddCommandString,

    .DebugGraph = SCR_DebugGraph,

    .FS_OpenFile = PF_OpenFile,
    .FS_CloseFile = PF_CloseFile,
    .FS_ReadFile = FS_Read,
    .FS_WriteFile = FS_Write,
    .FS_FlushFile = FS_Flush,
    .FS_TellFile = FS_Tell,
    .FS_SeekFile = FS_Seek,
    .FS_ReadLine = FS_ReadLine,
    .FS_ListFiles = PF_ListFiles,
    .FS_ErrorString = Q_ErrorStringBuffer,

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
};

// "fake" exports for calling into VM
static const game_export_t game_dll_exports = {
    .apiversion = GAME_API_VERSION,
    .structsize = sizeof(game_export_t),

    .Init = thunk_G_Init,
    .Shutdown = thunk_G_Shutdown,
    .SpawnEntities = thunk_G_SpawnEntities,
    .WriteGame = thunk_G_WriteGame,
    .ReadGame = thunk_G_ReadGame,
    .WriteLevel = thunk_G_WriteLevel,
    .ReadLevel = thunk_G_ReadLevel,
    .CanSave = thunk_G_CanSave,
    .ClientConnect = thunk_G_ClientConnect,
    .ClientBegin = thunk_G_ClientBegin,
    .ClientUserinfoChanged = thunk_G_ClientUserinfoChanged,
    .ClientDisconnect = thunk_G_ClientDisconnect,
    .ClientCommand = thunk_G_ClientCommand,
    .ClientThink = thunk_G_ClientThink,
    .PrepFrame = thunk_G_PrepFrame,
    .RunFrame = thunk_G_RunFrame,
    .ServerCommand = thunk_G_ServerCommand,
    .RestartFilesystem = thunk_G_RestartFilesystem,
};

static const vm_interface_t game_iface = {
    .name = "game",
    .vm_imports = game_vm_imports,
    .vm_exports = game_vm_exports,
    .dll_entry_name = "GetGameAPI",
    .dll_imports = &game_dll_imports,
    .dll_exports = &game_dll_exports,
    .api_version = GAME_API_VERSION,
};

/*
===============
SV_ShutdownGameProgs

Called when either the entire server is being killed, or
it is changing to a different game directory.
===============
*/
void SV_ShutdownGameProgs(void)
{
    VM_Reset(game.vm);

    if (ge) {
        ge->Shutdown();
        ge = NULL;
    }

    VM_FreeModule(&game);
}

/*
===============
SV_InitGameProgs

Init the game subsystem for a new map
===============
*/
void SV_InitGameProgs(void)
{
    // unload anything we have now
    SV_ShutdownGameProgs();

    // load game module
    ge = VM_LoadModule(&game, &game_iface);

    // initialize
    ge->Init();
}
