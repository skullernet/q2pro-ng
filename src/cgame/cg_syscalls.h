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

size_t trap_GetConfigstring(unsigned index, char *buf, size_t size);

void trap_BoxTrace(trace_t *trace,
                   const vec3_t start, const vec3_t end,
                   const vec3_t mins, const vec3_t maxs,
                   qhandle_t hmodel, contents_t contentmask);

void trap_TransformedBoxTrace(trace_t *trace,
                              const vec3_t start, const vec3_t end,
                              const vec3_t mins, const vec3_t maxs,
                              qhandle_t hmodel, contents_t contentmask,
                              const vec3_t origin, const vec3_t angles);

void trap_ClipEntity(trace_t *dst, const trace_t *src, int entnum);

contents_t trap_PointContents(const vec3_t point, qhandle_t hmodel);
contents_t trap_TransformedPointContents(const vec3_t point, qhandle_t hmodel,
                                         const vec3_t origin, const vec3_t angles);

qhandle_t trap_TempBoxModel(const vec3_t mins, const vec3_t maxs);

bool trap_GetSurfaceInfo(unsigned surf_id, surface_info_t *info);
bool trap_GetMaterialInfo(unsigned material_id, material_info_t *info);
void trap_GetBrushModelBounds(unsigned index, vec3_t mins, vec3_t maxs);

unsigned trap_GetUsercmdNumber(void);
bool trap_GetUsercmd(unsigned number, usercmd_t *ucmd);

unsigned trap_GetServerFrameNumber(void);
bool trap_GetServerFrame(unsigned frame, cg_server_frame_t *out);

bool trap_GetDemoInfo(cg_demo_info_t *info);

void trap_ClientCommand(const char *cmd);

int64_t trap_RealTime(void);
bool trap_LocalTime(int64_t time, vm_time_t *localtime);

bool trap_Cvar_Register(vm_cvar_t *var, const char *name, const char *value, unsigned flags);
void trap_Cvar_Set(const char *name, const char *value);
void trap_Cvar_ForceSet(const char *name, const char *value);
int trap_Cvar_VariableInteger(const char *name);
float trap_Cvar_VariableValue(const char *name);
size_t trap_Cvar_VariableString(const char *name, char *buf, size_t size);

int trap_Argc(void);
size_t trap_Argv(int arg, char *buf, size_t size);
size_t trap_Args(char *buf, size_t size);
void trap_AddCommandString(const char *text);

bool        trap_Key_GetOverstrikeMode(void);
void        trap_Key_SetOverstrikeMode(bool overstrike);
keydest_t   trap_Key_GetDest(void);
void        trap_Key_SetDest(keydest_t dest);

int         trap_Key_IsDown(int key);
int         trap_Key_AnyKeyDown(void);
void        trap_Key_ClearStates(void);

size_t  trap_Key_KeynumToString(int keynum, char *buf, size_t size);
int     trap_Key_StringToKeynum(const char *str);
size_t  trap_Key_GetBinding(const char *binding, char *buf, size_t size);
void    trap_Key_SetBinding(int keynum, const char *binding);
int     trap_Key_EnumBindings(int keynum, const char *binding);

qhandle_t trap_R_RegisterModel(const char *name);
qhandle_t trap_R_RegisterPic(const char *name);
qhandle_t trap_R_RegisterFont(const char *name);
qhandle_t trap_R_RegisterSkin(const char *name);
qhandle_t trap_R_RegisterSprite(const char *name);

void    trap_R_GetConfig(refcfg_t *cfg);
float   trap_R_GetAutoScale(void);

void    trap_R_SetSky(const char *name, float rotate, bool autorotate, const vec3_t axis);

void    trap_R_ClearScene(void);
void    trap_R_AddEntity(const entity_t *ent);
void    trap_R_AddLight(const vec3_t org, float intensity, float r, float g, float b);
void    trap_R_SetLightStyle(unsigned style, float value);
void    trap_R_LocateParticles(const particle_t *p, int num_particles);
void    trap_R_RenderScene(const refdef_t *fd);
void    trap_R_LightPoint(const vec3_t origin, vec3_t light);

void    trap_R_ClearColor(void);
void    trap_R_SetAlpha(float alpha);
void    trap_R_SetColor(uint32_t color);
void    trap_R_SetClipRect(const clipRect_t *clip);
void    trap_R_SetScale(float scale);
void    trap_R_DrawChar(int x, int y, int flags, int ch, qhandle_t font);
int     trap_R_DrawString(int x, int y, int flags, size_t max_chars,
                          const char *string, qhandle_t font);  // returns advanced x coord
bool    trap_R_GetPicSize(int *w, int *h, qhandle_t pic);   // returns transparency bit
void    trap_R_DrawPic(int x, int y, qhandle_t pic);
void    trap_R_DrawStretchPic(int x, int y, int w, int h, qhandle_t pic);
void    trap_R_DrawKeepAspectPic(int x, int y, int w, int h, qhandle_t pic);
void    trap_R_TileClear(int x, int y, int w, int h, qhandle_t pic);
void    trap_R_DrawFill8(int x, int y, int w, int h, int c);
void    trap_R_DrawFill32(int x, int y, int w, int h, uint32_t color);

qhandle_t trap_S_RegisterSound(const char *sample);
void trap_S_StartSound(const vec3_t origin, int entnum, int entchannel,
                       qhandle_t sfx, float volume, float attenuation, float timeofs);
void trap_S_ClearLoopingSounds(void);
void trap_S_AddLoopingSound(unsigned entnum, qhandle_t sfx, float volume, float attenuation, bool stereo_pan);
void trap_S_StartBackgroundTrack(const char *track);
void trap_S_StopBackgroundTrack(void);
void trap_S_UpdateEntity(unsigned entnum, const vec3_t origin);
void trap_S_UpdateListener(unsigned entnum, const vec3_t origin, const vec3_t axis[3], bool underwater);

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
#define trap_Print cgi->Print
#define trap_Error cgi->Error

#define trap_GetConfigstring cgi->GetConfigstring

#define trap_BoxTrace cgi->BoxTrace
#define trap_TransformedBoxTrace cgi->TransformedBoxTrace
#define trap_ClipEntity cgi->ClipEntity
#define trap_PointContents cgi->PointContents
#define trap_TransformedPointContents cgi->TransformedPointContents

#define trap_TempBoxModel cgi->TempBoxModel

#define trap_GetSurfaceInfo cgi->GetSurfaceInfo
#define trap_GetMaterialInfo cgi->GetMaterialInfo
#define trap_GetBrushModelBounds cgi->GetBrushModelBounds

#define trap_GetUsercmdNumber cgi->GetUsercmdNumber
#define trap_GetUsercmd cgi->GetUsercmd

#define trap_GetServerFrameNumber cgi->GetServerFrameNumber
#define trap_GetServerFrame cgi->GetServerFrame

#define trap_GetDemoInfo cgi->GetDemoInfo

#define trap_ClientCommand cgi->ClientCommand

#define trap_RealTime cgi->RealTime
#define trap_LocalTime cgi->LocalTime

#define trap_Cvar_Register cgi->Cvar_Register
#define trap_Cvar_Set cgi->Cvar_Set
#define trap_Cvar_ForceSet cgi->Cvar_ForceSet
#define trap_Cvar_VariableInteger cgi->Cvar_VariableInteger
#define trap_Cvar_VariableValue cgi->Cvar_VariableValue
#define trap_Cvar_VariableString cgi->Cvar_VariableString

#define trap_Argc cgi->Argc
#define trap_Argv cgi->Argv
#define trap_Args cgi->Args
#define trap_AddCommandString cgi->AddCommandString

#define trap_Key_GetOverstrikeMode cgi->Key_GetOverstrikeMode
#define trap_Key_SetOverstrikeMode cgi->Key_SetOverstrikeMode
#define trap_Key_GetDest cgi->Key_GetDest
#define trap_Key_SetDest cgi->Key_SetDest
#define trap_Key_IsDown cgi->Key_IsDown
#define trap_Key_AnyKeyDown cgi->Key_AnyKeyDown
#define trap_Key_ClearStates cgi->Key_ClearStates

#define trap_Key_KeynumToString cgi->Key_KeynumToString
#define trap_Key_StringToKeynum cgi->Key_StringToKeynum
#define trap_Key_GetBinding cgi->Key_GetBinding
#define trap_Key_SetBinding cgi->Key_SetBinding
#define trap_Key_EnumBindings cgi->Key_EnumBindings

#define trap_R_RegisterModel cgi->R_RegisterModel
#define trap_R_RegisterPic cgi->R_RegisterPic
#define trap_R_RegisterFont cgi->R_RegisterFont
#define trap_R_RegisterSkin cgi->R_RegisterSkin
#define trap_R_RegisterSprite cgi->R_RegisterSprite

#define trap_R_GetConfig cgi->R_GetConfig
#define trap_R_GetAutoScale cgi->R_GetAutoScale

#define trap_R_SetSky cgi->R_SetSky

#define trap_R_ClearScene cgi->R_ClearScene
#define trap_R_AddEntity cgi->R_AddEntity
#define trap_R_AddLight cgi->R_AddLight
#define trap_R_SetLightStyle cgi->R_SetLightStyle
#define trap_R_LocateParticles cgi->R_LocateParticles
#define trap_R_RenderScene cgi->R_RenderScene
#define trap_R_LightPoint cgi->R_LightPoint

#define trap_R_ClearColor cgi->R_ClearColor
#define trap_R_SetAlpha cgi->R_SetAlpha
#define trap_R_SetColor cgi->R_SetColor
#define trap_R_SetClipRect cgi->R_SetClipRect
#define trap_R_SetScale cgi->R_SetScale
#define trap_R_DrawChar cgi->R_DrawChar
#define trap_R_DrawString cgi->R_DrawString
#define trap_R_GetPicSize cgi->R_GetPicSize
#define trap_R_DrawPic cgi->R_DrawPic
#define trap_R_DrawStretchPic cgi->R_DrawStretchPic
#define trap_R_DrawKeepAspectPic cgi->R_DrawKeepAspectPic
#define trap_R_TileClear cgi->R_TileClear
#define trap_R_DrawFill8 cgi->R_DrawFill8
#define trap_R_DrawFill32 cgi->R_DrawFill32

#define trap_S_RegisterSound cgi->S_RegisterSound
#define trap_S_StartSound cgi->S_StartSound
#define trap_S_ClearLoopingSounds cgi->S_ClearLoopingSounds
#define trap_S_AddLoopingSound cgi->S_AddLoopingSound
#define trap_S_StartBackgroundTrack cgi->S_StartBackgroundTrack
#define trap_S_StopBackgroundTrack cgi->S_StopBackgroundTrack
#define trap_S_UpdateEntity cgi->S_UpdateEntity
#define trap_S_UpdateListener cgi->S_UpdateListener

#define trap_FS_OpenFile cgi->FS_OpenFile
#define trap_FS_CloseFile cgi->FS_CloseFile
#define trap_FS_ReadFile cgi->FS_ReadFile
#define trap_FS_WriteFile cgi->FS_WriteFile
#define trap_FS_FlushFile cgi->FS_FlushFile
#define trap_FS_TellFile cgi->FS_TellFile
#define trap_FS_SeekFile cgi->FS_SeekFile
#define trap_FS_ReadLine cgi->FS_ReadLine
#define trap_FS_ListFiles cgi->FS_ListFiles
#define trap_FS_ErrorString cgi->FS_ErrorString

#define trap_R_ClearDebugLines cgi->R_ClearDebugLines
#define trap_R_AddDebugLine cgi->R_AddDebugLine
#define trap_R_AddDebugPoint cgi->R_AddDebugPoint
#define trap_R_AddDebugAxis cgi->R_AddDebugAxis
#define trap_R_AddDebugBounds cgi->R_AddDebugBounds
#define trap_R_AddDebugSphere cgi->R_AddDebugSphere
#define trap_R_AddDebugCircle cgi->R_AddDebugCircle
#define trap_R_AddDebugCylinder cgi->R_AddDebugCylinder
#define trap_R_AddDebugArrow cgi->R_AddDebugArrow
#define trap_R_AddDebugCurveArrow cgi->R_AddDebugCurveArrow
#define trap_R_AddDebugText cgi->R_AddDebugText
#endif
