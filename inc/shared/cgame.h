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

#pragma once

#include "shared/keys.h"
#include "shared/refresh.h"

#define CGAME_API_VERSION    4000

#define CMD_BACKUP      128 // allow a lot of command backups for very fast systems
                            // increased from 64
#define CMD_MASK        (CMD_BACKUP - 1)

typedef struct {
    int             number;
    int             delta;

    byte            areabits[MAX_MAP_AREA_BYTES];
    int             areabytes;

    player_state_t  ps;

    int             num_entities;
    entity_state_t  entities[MAX_PACKET_ENTITIES];
} cg_server_frame_t;

typedef struct {
    char        name[MAX_QPATH];
    float       progress;
    unsigned    framenum;
} cg_demo_info_t;

//===============================================================

#ifndef Q2_VM

//
// functions provided by the main engine
//
typedef struct {
    uint32_t    apiversion;
    uint32_t    structsize;

    void (*Print)(print_type_t type, const char *msg);
    void (*q_noreturn_ptr Error)(const char *msg);

    size_t (*GetConfigstring)(unsigned index, char *buf, size_t size);

    void (*BoxTrace)(trace_t *trace,
                     const vec3_t start, const vec3_t end,
                     const vec3_t mins, const vec3_t maxs,
                     qhandle_t hmodel, contents_t contentmask);

    void (*TransformedBoxTrace)(trace_t *trace,
                                const vec3_t start, const vec3_t end,
                                const vec3_t mins, const vec3_t maxs,
                                qhandle_t hmodel, contents_t contentmask,
                                const vec3_t origin, const vec3_t angles);

    void (*ClipEntity)(trace_t *dst, const trace_t *src, int entnum);

    contents_t (*PointContents)(const vec3_t point, qhandle_t hmodel);
    contents_t (*TransformedPointContents)(const vec3_t point, qhandle_t hmodel,
                                           const vec3_t origin, const vec3_t angles);

    qhandle_t (*TempBoxModel)(const vec3_t mins, const vec3_t maxs);

    bool (*GetSurfaceInfo)(unsigned surf_id, surface_info_t *info);
    bool (*GetMaterialInfo)(unsigned material_id, material_info_t *info);
    void (*GetBrushModelBounds)(unsigned index, vec3_t mins, vec3_t maxs);

    void (*GetUsercmdNumber)(unsigned *ack, unsigned *current);
    bool (*GetUsercmd)(unsigned number, usercmd_t *ucmd);

    void (*GetServerFrameNumber)(unsigned *frame, unsigned *time);
    bool (*GetServerFrame)(unsigned frame, cg_server_frame_t *out);

    bool (*GetDemoInfo)(cg_demo_info_t *info);

    void (*ClientCommand)(const char *cmd);

    int64_t (*RealTime)(void);
    bool (*LocalTime)(int64_t time, vm_time_t *localtime);

    bool (*Cvar_Register)(vm_cvar_t *var, const char *name, const char *value, unsigned flags);
    void (*Cvar_Set)(const char *name, const char *value);
    void (*Cvar_ForceSet)(const char *name, const char *value);
    int (*Cvar_VariableInteger)(const char *name);
    float (*Cvar_VariableValue)(const char *name);
    size_t (*Cvar_VariableString)(const char *name, char *buf, size_t size);

    int (*Argc)(void);
    size_t (*Argv)(int arg, char *buf, size_t size);
    size_t (*Args)(char *buf, size_t size);
    void (*AddCommandString)(const char *text);

    bool        (*Key_GetOverstrikeMode)(void);
    void        (*Key_SetOverstrikeMode)(bool overstrike);
    keydest_t   (*Key_GetDest)(void);
    void        (*Key_SetDest)(keydest_t dest);

    int         (*Key_IsDown)(int key);
    int         (*Key_AnyKeyDown)(void);
    void        (*Key_ClearStates)(void);

    size_t  (*Key_KeynumToString)(int keynum, char *buf, size_t size);
    int     (*Key_StringToKeynum)(const char *str);
    size_t  (*Key_GetBinding)(const char *binding, char *buf, size_t size);
    void    (*Key_SetBinding)(int keynum, const char *binding);
    int     (*Key_EnumBindings)(int keynum, const char *binding);

    qhandle_t (*R_RegisterModel)(const char *name);
    qhandle_t (*R_RegisterPic)(const char *name);
    qhandle_t (*R_RegisterFont)(const char *name);
    qhandle_t (*R_RegisterSkin)(const char *name);
    qhandle_t (*R_RegisterSprite)(const char *name);

    void    (*R_GetConfig)(refcfg_t *cfg);
    float   (*R_GetAutoScale)(void);

    void    (*R_SetSky)(const char *name, float rotate, bool autorotate, const vec3_t axis);

    void    (*R_ClearScene)(void);
    void    (*R_AddEntity)(const entity_t *ent);
    void    (*R_AddLight)(const vec3_t org, float intensity, float r, float g, float b);
    void    (*R_SetLightStyle)(unsigned style, float value);
    void    (*R_LocateParticles)(const particle_t *p, int count);
    void    (*R_RenderScene)(const refdef_t *fd);
    void    (*R_LightPoint)(const vec3_t origin, vec3_t light);

    void    (*R_ClearColor)(void);
    void    (*R_SetAlpha)(float clpha);
    void    (*R_SetColor)(uint32_t color);
    void    (*R_SetClipRect)(const clipRect_t *clip);
    void    (*R_SetScale)(float scale);
    void    (*R_DrawChar)(int x, int y, int flags, int ch, qhandle_t font);
    int     (*R_DrawString)(int x, int y, int flags, size_t max_chars,
                            const char *string, qhandle_t font);  // returns advanced x coord
    bool    (*R_GetPicSize)(int *w, int *h, qhandle_t pic);   // returns transparency bit
    void    (*R_DrawPic)(int x, int y, qhandle_t pic);
    void    (*R_DrawStretchPic)(int x, int y, int w, int h, qhandle_t pic);
    void    (*R_DrawKeepAspectPic)(int x, int y, int w, int h, qhandle_t pic);
    void    (*R_TileClear)(int x, int y, int w, int h, qhandle_t pic);
    void    (*R_DrawFill8)(int x, int y, int w, int h, int c);
    void    (*R_DrawFill32)(int x, int y, int w, int h, uint32_t color);

    qhandle_t (*S_RegisterSound)(const char *sample);
    void (*S_StartSound)(const vec3_t origin, int entnum, int entchannel,
                         qhandle_t sfx, float volume, float attenuation, float timeofs);
    void (*S_ClearLoopingSounds)(void);
    void (*S_AddLoopingSound)(unsigned entnum, qhandle_t sfx, float volume, float attenuation, bool stereo_pan);
    void (*S_StartBackgroundTrack)(const char *track);
    void (*S_StopBackgroundTrack)(void);
    void (*S_UpdateEntity)(unsigned entnum, const vec3_t origin);
    void (*S_UpdateListener)(unsigned entnum, const vec3_t origin, const vec3_t axis[3], bool underwater);

    int64_t (*FS_OpenFile)(const char *path, qhandle_t *f, unsigned mode);
    int (*FS_CloseFile)(qhandle_t f);
    int (*FS_ReadFile)(void *buffer, size_t len, qhandle_t f);
    int (*FS_WriteFile)(const void *buffer, size_t len, qhandle_t f);
    int (*FS_FlushFile)(qhandle_t f);
    int64_t (*FS_TellFile)(qhandle_t f);
    int (*FS_SeekFile)(qhandle_t f, int64_t offset, int whence);
    int (*FS_ReadLine)(qhandle_t f, char *buffer, size_t size);
    size_t (*FS_ListFiles)(const char *path, const char *filter, unsigned flags, char *buffer, size_t size);
    size_t (*FS_ErrorString)(int error, char *buf, size_t size);

    void (*R_ClearDebugLines)(void);
    void (*R_AddDebugLine)(const vec3_t start, const vec3_t end, uint32_t color, uint32_t time, bool depth_test);
    void (*R_AddDebugPoint)(const vec3_t point, float size, uint32_t color, uint32_t time, bool depth_test);
    void (*R_AddDebugAxis)(const vec3_t origin, const vec3_t angles, float size, uint32_t time, bool depth_test);
    void (*R_AddDebugBounds)(const vec3_t mins, const vec3_t maxs, uint32_t color, uint32_t time, bool depth_test);
    void (*R_AddDebugSphere)(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test);
    void (*R_AddDebugCircle)(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test);
    void (*R_AddDebugCylinder)(const vec3_t origin, float half_height, float radius, uint32_t color, uint32_t time,
                               bool depth_test);
    void (*R_AddDebugArrow)(const vec3_t start, const vec3_t end, float size, uint32_t line_color,
                            uint32_t arrow_color, uint32_t time, bool depth_test);
    void (*R_AddDebugCurveArrow)(const vec3_t start, const vec3_t ctrl, const vec3_t end, float size,
                                 uint32_t line_color, uint32_t arrow_color, uint32_t time, bool depth_test);
    void (*R_AddDebugText)(const vec3_t origin, const vec3_t angles, const char *text,
                           float size, uint32_t color, uint32_t time, bool depth_test);
} cgame_import_t;

//
// functions exported by the cgame subsystem
//
typedef struct {
    uint32_t    apiversion;
    uint32_t    structsize;

    void (*Init)(void);
    void (*Shutdown)(void);
    void (*DrawActiveFrame)(unsigned msec);
    void (*ModeChanged)(void);
    bool (*ConsoleCommand)(void);
    void (*ServerCommand)(void);
    void (*UpdateConfigstring)(unsigned index);
    bool (*KeyEvent)(unsigned key, bool down);
    void (*CharEvent)(unsigned key);
} cgame_export_t;

#endif
