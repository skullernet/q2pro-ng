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

#ifdef Q2_VM
void trap_Print(print_type_t type, const char *msg);
q_noreturn void trap_Error(const char *msg);

void trap_SetConfigstring(unsigned index, const char *str);
size_t trap_GetConfigstring(unsigned index, char *buf, size_t size);
int trap_FindConfigstring(const char *name, int start, int max, bool create);

void trap_Trace(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, unsigned passent, contents_t contentmask);
void trap_Clip(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, unsigned clipent, contents_t contentmask);
contents_t trap_PointContents(const vec3_t point);
int trap_BoxEdicts(const vec3_t mins, const vec3_t maxs, int *list, int maxcount, int areatype);

bool trap_InVis(const vec3_t p1, const vec3_t p2, vis_t vis);
void trap_SetAreaPortalState(unsigned portalnum, bool open);
bool trap_AreasConnected(int area1, int area2);

void trap_LinkEntity(edict_t *ent);
void trap_UnlinkEntity(edict_t *ent);
void trap_SetBrushModel(edict_t *ent, const char *name);

bool trap_GetSurfaceInfo(unsigned surf_id, surface_info_t *info);
bool trap_GetMaterialInfo(unsigned material_id, material_info_t *info);

void trap_ClientCommand(edict_t *ent, const char *str, bool reliable);

void trap_LocateGameData(edict_t *edicts, size_t edict_size, unsigned num_edicts, gclient_t *clients, size_t client_size);
bool trap_ParseEntityString(char *buf, size_t size);
size_t trap_GetLevelName(char *buf, size_t size);
size_t trap_GetSpawnPoint(char *buf, size_t size);
size_t trap_GetUserinfo(unsigned clientnum, char *buf, size_t size);
size_t trap_GetConnectinfo(unsigned clientnum, char *buf, size_t size);
void trap_GetUsercmd(unsigned clientnum, usercmd_t *ucmd);
bool trap_GetPathToGoal(const PathRequest *request, PathInfo *info, vec3_t *points, int maxPoints);

int64_t trap_RealTime(void);
bool trap_LocalTime(int64_t time, vm_time_t *localtime);

bool trap_Cvar_Register(vm_cvar_t *var, const char *name, const char *value, unsigned flags);
void trap_Cvar_Set(const char *name, const char *value);
int trap_Cvar_VariableInteger(const char *name);
float trap_Cvar_VariableValue(const char *name);
size_t trap_Cvar_VariableString(const char *name, char *buf, size_t size);

int trap_Argc(void);
size_t trap_Argv(int arg, char *buf, size_t size);
size_t trap_Args(char *buf, size_t size);
void trap_AddCommandString(const char *text);

void trap_DebugGraph(float value, int color);

int64_t trap_FS_OpenFile(const char *path, qhandle_t *f, unsigned mode);
int trap_FS_CloseFile(qhandle_t f);
int trap_FS_ReadFile(void *buffer, size_t len, qhandle_t f);
int trap_FS_WriteFile(const void *buffer, size_t len, qhandle_t f);
int trap_FS_FlushFile(qhandle_t f);
int64_t trap_FS_TellFile(qhandle_t f);
int trap_FS_SeekFile(qhandle_t f, int64_t offset, int whence);
int trap_FS_ReadLine(qhandle_t f, char *buffer, size_t size);
size_t trap_FS_ListFiles(const char *path, const char *filter, unsigned flags, char *buffer, size_t size);
size_t trap_FS_ErrorString(int error, char *buf, size_t size);

void trap_R_ClearDebugLines(void);
void trap_R_AddDebugLine(const vec3_t start, const vec3_t end, uint32_t color, uint32_t time, bool depth_test);
void trap_R_AddDebugPoint(const vec3_t point, float size, uint32_t color, uint32_t time, bool depth_test);
void trap_R_AddDebugAxis(const vec3_t origin, const vec3_t angles, float size, uint32_t time, bool depth_test);
void trap_R_AddDebugBounds(const vec3_t mins, const vec3_t maxs, uint32_t color, uint32_t time, bool depth_test);
void trap_R_AddDebugSphere(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test);
void trap_R_AddDebugCircle(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test);
void trap_R_AddDebugCylinder(const vec3_t origin, float half_height, float radius, uint32_t color, uint32_t time,
                             bool depth_test);
void trap_R_AddDebugArrow(const vec3_t start, const vec3_t end, float size, uint32_t line_color,
                          uint32_t arrow_color, uint32_t time, bool depth_test);
void trap_R_AddDebugCurveArrow(const vec3_t start, const vec3_t ctrl, const vec3_t end, float size,
                               uint32_t line_color, uint32_t arrow_color, uint32_t time, bool depth_test);
void trap_R_AddDebugText(const vec3_t origin, const vec3_t angles, const char *text,
                         float size, uint32_t color, uint32_t time, bool depth_test);
#else
#define trap_Print gi->Print
#define trap_Error gi->Error

#define trap_SetConfigstring gi->SetConfigstring
#define trap_GetConfigstring gi->GetConfigstring
#define trap_FindConfigstring gi->FindConfigstring

#define trap_Trace gi->Trace
#define trap_Clip gi->Clip
#define trap_PointContents gi->PointContents
#define trap_BoxEdicts gi->BoxEdicts

#define trap_InVis gi->InVis
#define trap_SetAreaPortalState gi->SetAreaPortalState
#define trap_AreasConnected gi->AreasConnected

#define trap_LinkEntity gi->LinkEntity
#define trap_UnlinkEntity gi->UnlinkEntity
#define trap_SetBrushModel gi->SetBrushModel

#define trap_GetSurfaceInfo gi->GetSurfaceInfo
#define trap_GetMaterialInfo gi->GetMaterialInfo

#define trap_ClientCommand gi->ClientCommand

#define trap_LocateGameData gi->LocateGameData
#define trap_ParseEntityString gi->ParseEntityString
#define trap_GetLevelName gi->GetLevelName
#define trap_GetSpawnPoint gi->GetSpawnPoint
#define trap_GetUserinfo gi->GetUserinfo
#define trap_GetConnectinfo gi->GetConnectinfo
#define trap_GetUsercmd gi->GetUsercmd
#define trap_GetPathToGoal gi->GetPathToGoal

#define trap_RealTime gi->RealTime
#define trap_LocalTime gi->LocalTime

#define trap_Cvar_Register gi->Cvar_Register
#define trap_Cvar_Set gi->Cvar_Set
#define trap_Cvar_VariableInteger gi->Cvar_VariableInteger
#define trap_Cvar_VariableValue gi->Cvar_VariableValue
#define trap_Cvar_VariableString gi->Cvar_VariableString

#define trap_Argc gi->Argc
#define trap_Argv gi->Argv
#define trap_Args gi->Args
#define trap_AddCommandString gi->AddCommandString

#define trap_DebugGraph gi->DebugGraph

#define trap_FS_OpenFile gi->FS_OpenFile
#define trap_FS_CloseFile gi->FS_CloseFile
#define trap_FS_ReadFile gi->FS_ReadFile
#define trap_FS_WriteFile gi->FS_WriteFile
#define trap_FS_FlushFile gi->FS_FlushFile
#define trap_FS_TellFile gi->FS_TellFile
#define trap_FS_SeekFile gi->FS_SeekFile
#define trap_FS_ReadLine gi->FS_ReadLine
#define trap_FS_ListFiles gi->FS_ListFiles
#define trap_FS_ErrorString gi->FS_ErrorString

#define trap_R_ClearDebugLines gi->R_ClearDebugLines
#define trap_R_AddDebugLine gi->R_AddDebugLine
#define trap_R_AddDebugPoint gi->R_AddDebugPoint
#define trap_R_AddDebugAxis gi->R_AddDebugAxis
#define trap_R_AddDebugBounds gi->R_AddDebugBounds
#define trap_R_AddDebugSphere gi->R_AddDebugSphere
#define trap_R_AddDebugCircle gi->R_AddDebugCircle
#define trap_R_AddDebugCylinder gi->R_AddDebugCylinder
#define trap_R_AddDebugArrow gi->R_AddDebugArrow
#define trap_R_AddDebugCurveArrow gi->R_AddDebugCurveArrow
#define trap_R_AddDebugText gi->R_AddDebugText
#endif
