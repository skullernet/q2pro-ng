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

static vm_module_t  cgame;
cgame_export_t      *cge;

static const mnode_t *CL_ClipHandleToNode(unsigned index, bool world)
{
    if (index == MODELINDEX_TEMPBOX)
        return box_headnode;
    Q_assert_soft(index > 0 || world);
    Q_assert_soft(index < cl.bsp->nummodels);
    return &cl.bsp->models[index];
}

static void PF_Print(print_type_t type, const char *msg)
{
    Com_LPrintf(type, "%s", msg);
}

static q_noreturn void PF_Error(const char *msg)
{
    Com_Error(ERR_DROP, "CGame Error: %s", msg);
}

static void PF_BoxTrace(trace_t *trace,
                        const vec3_t start, const vec3_t end,
                        const vec3_t mins, const vec3_t maxs,
                        qhandle_t hmodel, contents_t contentmask)
{
    CM_BoxTrace(trace, start, end, mins, maxs, CL_ClipHandleToNode(hmodel, true), contentmask);
}

static void PF_TransformedBoxTrace(trace_t *trace,
                                   const vec3_t start, const vec3_t end,
                                   const vec3_t mins, const vec3_t maxs,
                                   qhandle_t hmodel, contents_t contentmask,
                                   const vec3_t origin, const vec3_t angles)
{
    CM_TransformedBoxTrace(trace, start, end, mins, maxs, CL_ClipHandleToNode(hmodel, false),
                           contentmask, origin, angles);
}

static contents_t PF_PointContents(const vec3_t point, qhandle_t hmodel)
{
    return BSP_PointLeaf(CL_ClipHandleToNode(hmodel, true), point)->contents;
}

static contents_t PF_TransformedPointContents(const vec3_t point, qhandle_t hmodel,
                                              const vec3_t origin, const vec3_t angles)
{
    return CM_TransformedPointContents(point, CL_ClipHandleToNode(hmodel, false), origin, angles);
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

static bool PF_GetSurfaceInfo(unsigned surf_id, surface_info_t *info)
{
    return BSP_SurfaceInfo(cl.bsp, surf_id, info);
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

static bool PF_GetServerFrame(unsigned number, vm_server_frame_t *out)
{
    const server_frame_t *frame = &cl.frames[number & UPDATE_MASK];

    if (!frame->valid)
        return false;
    if (frame->number != number)
        return false;
    if (cl.numEntityStates - frame->firstEntity > MAX_PARSE_ENTITIES)
        return false;

    for (int i = 0; i < frame->numEntities; i++)
        out->entities[i] = cl.entityStates[(frame->firstEntity + i) & PARSE_ENTITIES_MASK];

    out->num_entities = frame->numEntities;
    return true;
}

static const cgame_import_t cgame_dll_imports = {
    .apiversion = CGAME_API_VERSION,
    .structsize = sizeof(cgame_import_t),

    .Print = PF_Print,
    .Error = PF_Error,

    .SetConfigstring = PF_SetConfigstring,
    .GetConfigstring = PF_GetConfigstring,

    .BoxTrace = PF_BoxTrace,
    .TransformedBoxTrace = PF_TransformedBoxTrace,
    .PointContents = PF_PointContents,
    .TransformedPointContents = PF_TransformedPointContents,
    .TempBoxModel = PF_TempBoxModel,

    .DirToByte = DirToByte,
    .ByteToDir = ByteToDir,
    .GetSurfaceInfo = PF_GetSurfaceInfo,

    .RealTime = PF_RealTime,
    .LocalTime = PF_LocalTime,

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

    .S_RegisterSound = S_RegisterSound,
    .S_StartSound = S_StartSound,
    .S_ClearLoopingSounds = S_ClearLoopingSounds,
    .S_AddLoopingSound = S_AddLoopingSound,
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
    .RenderFrame = thunk_CG_RenderFrame,
    .ServerCommand = thunk_CG_ServerCommand,
    .RestartFilesystem = thunk_CG_RestartFilesystem,
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
    VM_Reset(cgame.vm);

    if (cge) {
        cge->Shutdown();
        cge = NULL;
    }

    VM_FreeModule(&cgame);
}

void CL_InitCGame(void)
{
    // unload anything we have now
    CL_ShutdownCGame();

    // load cgame module
    cge = VM_LoadModule(&cgame, &cgame_iface);

    // initialize
    cge->Init();
}
