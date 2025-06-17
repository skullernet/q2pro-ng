/*
Copyright (C) 2025 Andrey Nazarov

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

#include "client.h"
#include "common/vm.h"

#define MODELINDEX_TEMPBOX  -1

static vm_module_t      cgame;
const cgame_export_t    *cge;

static const mnode_t *box_headnode;

static const mnode_t *CL_ClipHandleToNode(unsigned index, bool transformed)
{
    if (index == MODELINDEX_TEMPBOX)
        return box_headnode;
    Q_assert_soft(cl.bsp);
    Q_assert_soft(index >= transformed);
    Q_assert_soft(index < cl.bsp->nummodels);
    return cl.bsp->models[index].headnode;
}

static void PF_Print(print_type_t type, const char *msg)
{
    Con_SkipNotify(type & PRINT_SKIPNOTIFY);
    Com_LPrintf(type & ~PRINT_SKIPNOTIFY, "%s", msg);
    Con_SkipNotify(false);
}

static q_noreturn void PF_Error(const char *msg)
{
    Com_Error(ERR_DROP, "CGame Error: %s", msg);
}

static size_t PF_GetConfigstring(unsigned index, char *buf, size_t size)
{
    Q_assert_soft(index < MAX_CONFIGSTRINGS);
    return Q_strlcpy_null(buf, cl.configstrings[index], size);
}

static void PF_BoxTrace(trace_t *trace,
                        const vec3_t start, const vec3_t end,
                        const vec3_t mins, const vec3_t maxs,
                        qhandle_t hmodel, contents_t contentmask)
{
    CM_BoxTrace(trace, start, end, mins, maxs, CL_ClipHandleToNode(hmodel, false), contentmask);
}

static void PF_TransformedBoxTrace(trace_t *trace,
                                   const vec3_t start, const vec3_t end,
                                   const vec3_t mins, const vec3_t maxs,
                                   qhandle_t hmodel, contents_t contentmask,
                                   const vec3_t origin, const vec3_t angles)
{
    CM_TransformedBoxTrace(trace, start, end, mins, maxs, CL_ClipHandleToNode(hmodel, true),
                           contentmask, origin, angles);
}

static contents_t PF_PointContents(const vec3_t point, qhandle_t hmodel)
{
    return BSP_PointLeaf(CL_ClipHandleToNode(hmodel, false), point)->contents;
}

static contents_t PF_TransformedPointContents(const vec3_t point, qhandle_t hmodel,
                                              const vec3_t origin, const vec3_t angles)
{
    return CM_TransformedPointContents(point, CL_ClipHandleToNode(hmodel, true), origin, angles);
}

static qhandle_t PF_TempBoxModel(const vec3_t mins, const vec3_t maxs)
{
    CM_HeadnodeForBox(mins, maxs);
    return MODELINDEX_TEMPBOX;
}

static bool PF_Cvar_Register(vm_cvar_t *var, const char *name, const char *value, unsigned flags)
{
    return VM_RegisterCvar(&cgame, var, name, value, flags);
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
    Cbuf_AddText(&cmd_buffer, string);
}

static size_t PF_Key_KeynumToString(int keynum, char *buf, size_t size)
{
    return Q_strlcpy(buf, Key_KeynumToString(keynum), size);
}

static size_t PF_Key_GetBinding(const char *binding, char *buf, size_t size)
{
    return Q_strlcpy(buf, Key_GetBinding(binding), size);
}

static bool PF_GetSurfaceInfo(unsigned surf_id, surface_info_t *info)
{
    return BSP_GetSurfaceInfo(cl.bsp, surf_id, info);
}

static bool PF_GetMaterialInfo(unsigned material_id, material_info_t *info)
{
    return BSP_GetMaterialInfo(cl.bsp, material_id, info);
}

static void PF_GetBrushModelBounds(unsigned index, vec3_t mins, vec3_t maxs)
{
    Q_assert_soft(cl.bsp);
    Q_assert_soft(index < cl.bsp->nummodels);
    const mmodel_t *mod = &cl.bsp->models[index];
    VectorCopy(mod->mins, mins);
    VectorCopy(mod->maxs, maxs);
}

static int64_t PF_OpenFile(const char *path, qhandle_t *f, unsigned mode)
{
    return VM_OpenFile(&cgame, path, f, mode);
}

static int PF_CloseFile(qhandle_t f)
{
    return VM_CloseFile(&cgame, f);
}

static size_t PF_ListFiles(const char *path, const char *filter, unsigned flags, char *buffer, size_t size)
{
    return 0;
}

static void PF_R_GetConfig(refcfg_t *cfg)
{
    *cfg = r_config;
}

static float PF_R_GetAutoScale(void)
{
    return R_ClampScale(NULL);
}

static void PF_GetUsercmdNumber(unsigned *ack_p, unsigned *cur_p)
{
    unsigned ack = cl.history[cls.netchan.incoming_acknowledged & CMD_MASK].cmdNumber;
    unsigned cur = cl.cmdNumber + !!cl.cmd.msec;

    if (ack_p)
        *ack_p = ack;
    if (cur_p)
        *cur_p = cur;
}

static bool PF_GetUsercmd(unsigned number, usercmd_t *ucmd)
{
    // return pending cmd
    if (number == cl.cmdNumber + 1 && cl.cmd.msec) {
        *ucmd = cl.cmd;
        ucmd->forwardmove = cl.localmove[0];
        ucmd->sidemove = cl.localmove[1];
        ucmd->upmove = cl.localmove[2];
        return true;
    }

    if (cl.cmdNumber - number >= CMD_BACKUP)
        return false;

    *ucmd = cl.cmds[number & CMD_MASK];
    return true;
}

static void PF_GetServerFrameNumber(unsigned *frame, unsigned *time)
{
    *frame = cl.frame.number;
    *time = cl.frame.number * BASE_FRAMETIME;
}

static bool PF_GetServerFrame(unsigned number, cg_server_frame_t *out)
{
    const server_frame_t *frame = &cl.frames[number & UPDATE_MASK];

    if (!frame->valid)
        return false;
    if (frame->number != number)
        return false;
    if (cl.numEntityStates - frame->firstEntity > MAX_PARSE_ENTITIES)
        return false;

    out->number = number;
    memcpy(out->areabits, frame->areabits, sizeof(out->areabits));

    out->ps = frame->ps;

    for (int i = 0; i < frame->numEntities; i++)
        out->entities[i] = cl.entityStates[(frame->firstEntity + i) & PARSE_ENTITIES_MASK];
    out->num_entities = frame->numEntities;

    return true;
}

static bool PF_GetDemoInfo(cg_demo_info_t *info)
{
    if (!cls.demo.playback)
        return false;
    if (!info)
        return true;
    Q_strlcpy(info->name, cls.servername, sizeof(info->name));
    info->progress = cls.demo.file_progress;
    info->framenum = cls.demo.frames_read;
    return true;
}

//==============================================

VM_THUNK(Print) {
    PF_Print(VM_U32(0), VM_STR(1));
}

VM_THUNK(Error) {
    PF_Error(VM_STR(0));
}

VM_THUNK(GetConfigstring) {
    VM_U32(0) = PF_GetConfigstring(VM_U32(0), VM_STR_BUF(1, 2), VM_U32(2));
}

VM_THUNK(BoxTrace) {
    PF_BoxTrace(VM_PTR(0, trace_t), VM_VEC3(1), VM_VEC3(2), VM_VEC3(3), VM_VEC3(4), VM_U32(5), VM_U32(6));
}

VM_THUNK(TransformedBoxTrace) {
    PF_TransformedBoxTrace(VM_PTR(0, trace_t), VM_VEC3(1), VM_VEC3(2), VM_VEC3(3), VM_VEC3(4), VM_U32(5), VM_U32(6), VM_VEC3(7), VM_VEC3(8));
}

VM_THUNK(ClipEntity) {
    CM_ClipEntity(VM_PTR(0, trace_t), VM_PTR(1, trace_t), VM_U32(2));
}

VM_THUNK(PointContents) {
    VM_U32(0) = PF_PointContents(VM_VEC3(0), VM_U32(1));
}

VM_THUNK(TransformedPointContents) {
    VM_U32(0) = PF_TransformedPointContents(VM_VEC3(0), VM_U32(1), VM_VEC3(2), VM_VEC3(3));
}

VM_THUNK(TempBoxModel) {
    VM_U32(0) = PF_TempBoxModel(VM_VEC3(0), VM_VEC3(1));
}

VM_THUNK(GetSurfaceInfo) {
    VM_U32(0) = PF_GetSurfaceInfo(VM_U32(0), VM_PTR(1, surface_info_t));
}

VM_THUNK(GetMaterialInfo) {
    VM_U32(0) = PF_GetMaterialInfo(VM_U32(0), VM_PTR(1, material_info_t));
}

VM_THUNK(GetBrushModelBounds) {
    PF_GetBrushModelBounds(VM_U32(0), VM_VEC3(1), VM_VEC3(2));
}

VM_THUNK(GetUsercmdNumber) {
    PF_GetUsercmdNumber(VM_PTR_NULL(0, unsigned), VM_PTR_NULL(1, unsigned));
}

VM_THUNK(GetUsercmd) {
    VM_U32(0) = PF_GetUsercmd(VM_U32(0), VM_PTR(1, usercmd_t));
}

VM_THUNK(GetServerFrameNumber) {
    PF_GetServerFrameNumber(VM_PTR(0, unsigned), VM_PTR(1, unsigned));
}

VM_THUNK(GetServerFrame) {
    VM_U32(0) = PF_GetServerFrame(VM_U32(0), VM_PTR(1, cg_server_frame_t));
}

VM_THUNK(GetDemoInfo) {
    VM_U32(0) = PF_GetDemoInfo(VM_PTR_NULL(0, cg_demo_info_t));
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

VM_THUNK(Key_GetOverstrikeMode) {
    VM_U32(0) = Key_GetOverstrikeMode();
}

VM_THUNK(Key_SetOverstrikeMode) {
    Key_SetOverstrikeMode(VM_U32(0));
}

VM_THUNK(Key_GetDest) {
    VM_U32(0) = Key_GetDest();
}

VM_THUNK(Key_SetDest) {
    Key_SetDest(VM_U32(0));
}

VM_THUNK(Key_IsDown) {
    VM_U32(0) = Key_IsDown(VM_U32(0));
}

VM_THUNK(Key_AnyKeyDown) {
    VM_U32(0) = Key_AnyKeyDown();
}

VM_THUNK(Key_ClearStates) {
    Key_ClearStates();
}

VM_THUNK(Key_KeynumToString) {
    VM_U32(0) = PF_Key_KeynumToString(VM_U32(0), VM_STR_BUF(1, 2), VM_U32(2));
}

VM_THUNK(Key_StringToKeynum) {
    VM_U32(0) = Key_StringToKeynum(VM_STR(0));
}

VM_THUNK(Key_GetBinding) {
    VM_U32(0) = PF_Key_GetBinding(VM_STR(0), VM_STR_BUF(1, 2), VM_U32(2));
}

VM_THUNK(Key_SetBinding) {
    Key_SetBinding(VM_U32(0), VM_STR(1));
}

VM_THUNK(Key_EnumBindings) {
    VM_U32(0) = Key_EnumBindings(VM_U32(0), VM_STR(1));
}

VM_THUNK(R_RegisterModel) {
    VM_U32(0) = R_RegisterModel(VM_STR(0));
}

VM_THUNK(R_RegisterPic) {
    VM_U32(0) = R_RegisterPic(VM_STR(0));
}

VM_THUNK(R_RegisterFont) {
    VM_U32(0) = R_RegisterFont(VM_STR(0));
}

VM_THUNK(R_RegisterSkin) {
    VM_U32(0) = R_RegisterSkin(VM_STR(0));
}

VM_THUNK(R_RegisterSprite) {
    VM_U32(0) = R_RegisterSprite(VM_STR(0));
}

VM_THUNK(R_GetConfig) {
    PF_R_GetConfig(VM_PTR(0, refcfg_t));
}

VM_THUNK(R_GetAutoScale) {
    VM_F32(0) = PF_R_GetAutoScale();
}

VM_THUNK(R_SetSky) {
    R_SetSky(VM_STR(0), VM_F32(1), VM_U32(2), VM_VEC3(3));
}

VM_THUNK(R_ClearScene) {
    R_ClearScene();
}

VM_THUNK(R_AddEntity) {
    R_AddEntity(VM_PTR(0, entity_t));
}

VM_THUNK(R_AddLight) {
    R_AddLight(VM_VEC3(0), VM_F32(1), VM_F32(2), VM_F32(3), VM_F32(4));
}

VM_THUNK(R_SetLightStyle) {
    R_SetLightStyle(VM_U32(0), VM_F32(1));
}

VM_THUNK(R_LocateParticles) {
    R_LocateParticles(VM_PTR_CNT(0, particle_t, VM_U32(1)), VM_U32(1));
}

VM_THUNK(R_RenderScene) {
    R_RenderFrame(VM_PTR(0, refdef_t));
}

VM_THUNK(R_LightPoint) {
    R_LightPoint(VM_VEC3(0), VM_VEC3(1));
}

VM_THUNK(R_ClearColor) {
    R_ClearColor();
}

VM_THUNK(R_SetAlpha) {
    R_SetAlpha(VM_F32(0));
}

VM_THUNK(R_SetColor) {
    R_SetColor(VM_U32(0));
}

VM_THUNK(R_SetClipRect) {
    R_SetClipRect(VM_PTR(0, clipRect_t));
}

VM_THUNK(R_SetScale) {
    R_SetScale(VM_F32(0));
}

VM_THUNK(R_DrawChar) {
    R_DrawChar(VM_U32(0), VM_U32(1), VM_U32(2), VM_U32(3), VM_U32(4));
}

VM_THUNK(R_DrawString) {
    VM_U32(0) = R_DrawString(VM_U32(0), VM_U32(1), VM_U32(2), VM_U32(3), VM_STR(4), VM_U32(5));
}

VM_THUNK(R_GetPicSize) {
    VM_U32(0) = R_GetPicSize(VM_PTR(0, int), VM_PTR(1, int), VM_U32(2));
}

VM_THUNK(R_DrawPic) {
    R_DrawPic(VM_U32(0), VM_U32(1), VM_U32(2));
}

VM_THUNK(R_DrawStretchPic) {
    R_DrawStretchPic(VM_U32(0), VM_U32(1), VM_U32(2), VM_U32(3), VM_U32(4));
}

VM_THUNK(R_DrawKeepAspectPic) {
    R_DrawKeepAspectPic(VM_U32(0), VM_U32(1), VM_U32(2), VM_U32(3), VM_U32(4));
}

VM_THUNK(R_TileClear) {
    R_TileClear(VM_U32(0), VM_U32(1), VM_U32(2), VM_U32(3), VM_U32(4));
}

VM_THUNK(R_DrawFill8) {
    R_DrawFill8(VM_U32(0), VM_U32(1), VM_U32(2), VM_U32(3), VM_U32(4));
}

VM_THUNK(R_DrawFill32) {
    R_DrawFill32(VM_U32(0), VM_U32(1), VM_U32(2), VM_U32(3), VM_U32(4));
}

VM_THUNK(S_RegisterSound) {
    VM_U32(0) = S_RegisterSound(VM_STR(0));
}

VM_THUNK(S_StartSound) {
    S_StartSound(VM_VEC3_NULL(0), VM_U32(1), VM_U32(2), VM_U32(3), VM_F32(4), VM_F32(5), VM_F32(6));
}

VM_THUNK(S_ClearLoopingSounds) {
    S_ClearLoopingSounds();
}

VM_THUNK(S_AddLoopingSound) {
    S_AddLoopingSound(VM_U32(0), VM_U32(1), VM_F32(2), VM_F32(3), VM_U32(4));
}

VM_THUNK(S_StartBackgroundTrack) {
    S_StartBackgroundTrack(VM_STR(0));
}

VM_THUNK(S_StopBackgroundTrack) {
    S_StopBackgroundTrack();
}

VM_THUNK(S_UpdateEntity) {
    S_UpdateEntity(VM_U32(0), VM_VEC3(1));
}

VM_THUNK(S_UpdateListener) {
    S_UpdateListener(VM_U32(0), VM_VEC3(1), VM_PTR_CNT(2, vec3_t, 3), VM_U32(3));
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

VM_THUNK(atanf) {
    VM_F32(0) = atanf(VM_F32(0));
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

static const vm_import_t cgame_vm_imports[] = {
    VM_IMPORT(Print, "ii"),
    VM_IMPORT(Error, "i"),
    VM_IMPORT(GetConfigstring, "i iii"),
    VM_IMPORT(BoxTrace, "iiiiiii"),
    VM_IMPORT(TransformedBoxTrace, "iiiiiiiii"),
    VM_IMPORT(ClipEntity, "iii"),
    VM_IMPORT(PointContents, "i ii"),
    VM_IMPORT(TransformedPointContents, "i iiii"),
    VM_IMPORT(TempBoxModel, "i ii"),
    VM_IMPORT(GetSurfaceInfo, "i ii"),
    VM_IMPORT(GetMaterialInfo, "i ii"),
    VM_IMPORT(GetBrushModelBounds, "iii"),
    VM_IMPORT(GetUsercmdNumber, "ii"),
    VM_IMPORT(GetUsercmd, "i ii"),
    VM_IMPORT(GetServerFrameNumber, "ii"),
    VM_IMPORT(GetServerFrame, "i ii"),
    VM_IMPORT(GetDemoInfo, "i i"),
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
    VM_IMPORT(Key_GetOverstrikeMode, "i "),
    VM_IMPORT(Key_SetOverstrikeMode, "i"),
    VM_IMPORT(Key_GetDest, "i "),
    VM_IMPORT(Key_SetDest, "i"),
    VM_IMPORT(Key_IsDown, "i i"),
    VM_IMPORT(Key_AnyKeyDown, "i "),
    VM_IMPORT(Key_ClearStates, ""),
    VM_IMPORT(Key_KeynumToString, "i iii"),
    VM_IMPORT(Key_StringToKeynum, "i i"),
    VM_IMPORT(Key_GetBinding, "i iii"),
    VM_IMPORT(Key_SetBinding, "ii"),
    VM_IMPORT(Key_EnumBindings, "i ii"),
    VM_IMPORT(R_RegisterModel, "i i"),
    VM_IMPORT(R_RegisterPic, "i i"),
    VM_IMPORT(R_RegisterFont, "i i"),
    VM_IMPORT(R_RegisterSkin, "i i"),
    VM_IMPORT(R_RegisterSprite, "i i"),
    VM_IMPORT(R_GetConfig, "i"),
    VM_IMPORT(R_GetAutoScale, "f "),
    VM_IMPORT(R_SetSky, "ifii"),
    VM_IMPORT(R_ClearScene, ""),
    VM_IMPORT(R_AddEntity, "i"),
    VM_IMPORT(R_AddLight, "iffff"),
    VM_IMPORT(R_SetLightStyle, "if"),
    VM_IMPORT(R_LocateParticles, "ii"),
    VM_IMPORT(R_RenderScene, "i"),
    VM_IMPORT(R_LightPoint, "ii"),
    VM_IMPORT(R_ClearColor, ""),
    VM_IMPORT(R_SetAlpha, "f"),
    VM_IMPORT(R_SetColor, "i"),
    VM_IMPORT(R_SetClipRect, "i"),
    VM_IMPORT(R_SetScale, "f"),
    VM_IMPORT(R_DrawChar, "iiiii"),
    VM_IMPORT(R_DrawString, "i iiiiii"),
    VM_IMPORT(R_GetPicSize, "i iii"),
    VM_IMPORT(R_DrawPic, "iii"),
    VM_IMPORT(R_DrawStretchPic, "iiiii"),
    VM_IMPORT(R_DrawKeepAspectPic, "iiiii"),
    VM_IMPORT(R_TileClear, "iiiii"),
    VM_IMPORT(R_DrawFill8, "iiiii"),
    VM_IMPORT(R_DrawFill32, "iiiii"),
    VM_IMPORT(S_RegisterSound, "i i"),
    VM_IMPORT(S_StartSound, "iiiifff"),
    VM_IMPORT(S_ClearLoopingSounds, ""),
    VM_IMPORT(S_AddLoopingSound, "iiffi"),
    VM_IMPORT(S_StartBackgroundTrack, "i"),
    VM_IMPORT(S_StopBackgroundTrack, ""),
    VM_IMPORT(S_UpdateEntity, "ii"),
    VM_IMPORT(S_UpdateListener, "iiii"),
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
    VM_IMPORT_RAW(atanf, "f f"),
    VM_IMPORT_RAW(atan2f, "f ff"),
    VM_IMPORT_RAW(memcmp, "i iii"),

    { 0 }
};

//==============================================

typedef enum {
    vm_CG_Init,
    vm_CG_Shutdown,
    vm_CG_DrawActiveFrame,
    vm_CG_ModeChanged,
    vm_CG_ConsoleCommand,
    vm_CG_ServerCommand,
    vm_CG_UpdateConfigstring,
} cgame_entry_t;

static const vm_export_t cgame_vm_exports[] = {
    VM_EXPORT(CG_Init, ""),
    VM_EXPORT(CG_Shutdown, ""),
    VM_EXPORT(CG_DrawActiveFrame, "i"),
    VM_EXPORT(CG_ModeChanged, ""),
    VM_EXPORT(CG_ConsoleCommand, "i "),
    VM_EXPORT(CG_ServerCommand, ""),
    VM_EXPORT(CG_UpdateConfigstring, "i"),

    { 0 }
};

static void thunk_CG_Init(void) {
    VM_Call(cgame.vm, vm_CG_Init);
}

static void thunk_CG_Shutdown(void) {
    VM_Call(cgame.vm, vm_CG_Shutdown);
}

static void thunk_CG_DrawActiveFrame(unsigned msec) {
    vm_value_t *stack = VM_Push(cgame.vm, 1);
    VM_U32(0) = msec;
    VM_Call(cgame.vm, vm_CG_DrawActiveFrame);
}

static void thunk_CG_ModeChanged(void) {
    VM_Call(cgame.vm, vm_CG_ModeChanged);
}

static bool thunk_CG_ConsoleCommand(void) {
    VM_Call(cgame.vm, vm_CG_ConsoleCommand);
    const vm_value_t *stack = VM_Pop(cgame.vm);
    return VM_U32(0);
}

static void thunk_CG_ServerCommand(void) {
    VM_Call(cgame.vm, vm_CG_ServerCommand);
}

static void thunk_CG_UpdateConfigstring(unsigned index) {
    vm_value_t *stack = VM_Push(cgame.vm, 1);
    VM_U32(0) = index;
    VM_Call(cgame.vm, vm_CG_UpdateConfigstring);
}

//==============================================

static const cgame_import_t cgame_dll_imports = {
    .apiversion = CGAME_API_VERSION,
    .structsize = sizeof(cgame_import_t),

    .Print = PF_Print,
    .Error = PF_Error,

    .GetConfigstring = PF_GetConfigstring,

    .BoxTrace = PF_BoxTrace,
    .TransformedBoxTrace = PF_TransformedBoxTrace,
    .ClipEntity = CM_ClipEntity,
    .PointContents = PF_PointContents,
    .TransformedPointContents = PF_TransformedPointContents,
    .TempBoxModel = PF_TempBoxModel,

    .GetSurfaceInfo = PF_GetSurfaceInfo,
    .GetMaterialInfo = PF_GetMaterialInfo,
    .GetBrushModelBounds = PF_GetBrushModelBounds,

    .GetUsercmdNumber = PF_GetUsercmdNumber,
    .GetUsercmd = PF_GetUsercmd,

    .GetServerFrameNumber = PF_GetServerFrameNumber,
    .GetServerFrame = PF_GetServerFrame,

    .GetDemoInfo = PF_GetDemoInfo,

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

    .Key_GetOverstrikeMode = Key_GetOverstrikeMode,
    .Key_SetOverstrikeMode = Key_SetOverstrikeMode,
    .Key_GetDest = Key_GetDest,
    .Key_SetDest = Key_SetDest,

    .Key_IsDown = Key_IsDown,
    .Key_AnyKeyDown = Key_AnyKeyDown,
    .Key_ClearStates = Key_ClearStates,

    .Key_KeynumToString = PF_Key_KeynumToString,
    .Key_StringToKeynum = Key_StringToKeynum,
    .Key_GetBinding = PF_Key_GetBinding,
    .Key_SetBinding = Key_SetBinding,
    .Key_EnumBindings = Key_EnumBindings,

    .S_RegisterSound = S_RegisterSound,
    .S_StartSound = S_StartSound,
    .S_ClearLoopingSounds = S_ClearLoopingSounds,
    .S_AddLoopingSound = S_AddLoopingSound,
    .S_StartBackgroundTrack = S_StartBackgroundTrack,
    .S_StopBackgroundTrack = S_StopBackgroundTrack,
    .S_UpdateEntity = S_UpdateEntity,
    .S_UpdateListener = S_UpdateListener,

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

    .R_RegisterModel = R_RegisterModel,
    .R_RegisterPic = R_RegisterTempPic,
    .R_RegisterFont = R_RegisterTempFont,
    .R_RegisterSkin = R_RegisterSkin,
    .R_RegisterSprite = R_RegisterSprite,

    .R_GetConfig = PF_R_GetConfig,
    .R_GetAutoScale = PF_R_GetAutoScale,

    .R_SetSky = R_SetSky,

    .R_ClearScene = R_ClearScene,
    .R_AddEntity = R_AddEntity,
    .R_AddLight = R_AddLight,
    .R_SetLightStyle = R_SetLightStyle,
    .R_LocateParticles = R_LocateParticles,
    .R_RenderScene = R_RenderFrame,
    .R_LightPoint = R_LightPoint,

    .R_ClearColor = R_ClearColor,
    .R_SetAlpha = R_SetAlpha,
    .R_SetColor = R_SetColor,
    .R_SetClipRect = R_SetClipRect,
    .R_SetScale = R_SetScale,
    .R_DrawChar = R_DrawChar,
    .R_DrawString = R_DrawString,
    .R_GetPicSize = R_GetPicSize,
    .R_DrawPic = R_DrawPic,
    .R_DrawStretchPic = R_DrawStretchPic,
    .R_DrawKeepAspectPic = R_DrawKeepAspectPic,
    .R_TileClear = R_TileClear,
    .R_DrawFill8 = R_DrawFill8,
    .R_DrawFill32 = R_DrawFill32,

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
static const cgame_export_t cgame_dll_exports = {
    .apiversion = CGAME_API_VERSION,
    .structsize = sizeof(cgame_export_t),

    .Init = thunk_CG_Init,
    .Shutdown = thunk_CG_Shutdown,
    .DrawActiveFrame = thunk_CG_DrawActiveFrame,
    .ModeChanged = thunk_CG_ModeChanged,
    .ConsoleCommand = thunk_CG_ConsoleCommand,
    .ServerCommand = thunk_CG_ServerCommand,
    .UpdateConfigstring = thunk_CG_UpdateConfigstring,
};

static const vm_interface_t cgame_iface = {
    .name = "cgame",
    .vm_imports = cgame_vm_imports,
    .vm_exports = cgame_vm_exports,
    .dll_entry_name = "GetCGameAPI",
    .dll_imports = &cgame_dll_imports,
    .dll_exports = &cgame_dll_exports,
    .api_version = CGAME_API_VERSION,
};

void CL_ShutdownCGame(void)
{
    // clear pointers to cgame memory
    R_ClearScene();

    VM_Reset(cgame.vm);

    if (cge) {
        cge->Shutdown();
        cge = NULL;
    }

    VM_FreeModule(&cgame);
}

/*
=================
CL_LoadMap

Registers main BSP file and inline models
=================
*/
static void CL_LoadMap(void)
{
    char name[MAX_QPATH];
    int ret;

    if (cl.bsp)
        return;

    Q_snprintf(name, sizeof(name), "maps/%s.bsp", cl.mapname);
    ret = BSP_Load(name, &cl.bsp);
    if (cl.bsp == NULL) {
        Com_Error(ERR_DROP, "Couldn't load %s: %s", name, BSP_ErrorString(ret));
    }

    if (cl.bsp->checksum != cl.mapchecksum) {
        if (cls.demo.playback) {
            Com_WPrintf("Local map version differs from demo: %#x != %#x\n",
                        cl.bsp->checksum, cl.mapchecksum);
        } else {
            Com_Error(ERR_DROP, "Local map version differs from server: %#x != %#x",
                      cl.bsp->checksum, cl.mapchecksum);
        }
    }

    box_headnode = CM_HeadnodeForBox(vec3_origin, vec3_origin);
}

void CL_InitCGame(void)
{
    CL_LoadMap();

    // load cgame module
    if (!cge)
        cge = VM_LoadModule(&cgame, &cgame_iface);

    // register models, pics, and skins
    R_BeginRegistration(cl.mapname);

    S_BeginRegistration();

    // initialize
    cge->Init();

    S_EndRegistration();

    // the renderer can now free unneeded stuff
    R_EndRegistration();

    // clear any lines of console text
    Con_ClearNotify_f();

    SCR_UpdateScreen();
}
