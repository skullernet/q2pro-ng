/*
Copyright (C) 2003-2006 Andrey Nazarov

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

#include "common/cmd.h"
#include "common/net/net.h"
#include "common/utils.h"

void CL_PreInit(void);

void SCR_DebugGraph(float value, int color);

#if USE_CLIENT

#define MAX_LOCAL_SERVERS   16
#define MAX_STATUS_PLAYERS  64

typedef struct {
    char name[MAX_CLIENT_NAME];
    int ping;
    int score;
} playerStatus_t;

typedef struct {
    char infostring[MAX_INFO_STRING];
    playerStatus_t players[MAX_STATUS_PLAYERS];
    int numPlayers;
} serverStatus_t;

typedef struct {
    char map[MAX_QPATH];
    char pov[MAX_CLIENT_NAME];
    bool mvd;
} demoInfo_t;

typedef enum {
    ACT_MINIMIZED,
    ACT_RESTORED,
    ACT_ACTIVATED
} active_t;

bool CL_ProcessEvents(void);
#if USE_ICMP
void CL_ErrorEvent(const netadr_t *from);
#endif
void CL_Init(void);
void CL_Disconnect(error_type_t type);
void CL_Shutdown(void);
unsigned CL_Frame(unsigned msec);
void CL_RestartFilesystem(bool total);
void CL_Activate(active_t active);
void CL_UpdateUserinfo(cvar_t *var, from_t from);
void CL_SendStatusRequest(const netadr_t *address);
bool CL_GetDemoInfo(const char *path, demoInfo_t *info);
bool CL_CheatsOK(void);
void CL_SetSky(void);

#if USE_CURL
int HTTP_FetchFile(const char *url, void **data);
#define HTTP_FreeFile(data) free(data)
#endif

bool CL_ForwardToServer(void);
// adds the current command line as a clc_stringcmd to the client message.
// things like godmode, noclip, etc, are commands directed to the server,
// so when they are typed in at the console, they will need to be forwarded.

void Con_Init(void);
void Con_SetColor(color_index_t color);
void Con_SkipNotify(bool skip);
void Con_Print(const char *text);
void Con_Printf(const char *fmt, ...) q_printf(1, 2);
void Con_Close(bool force);

void SCR_BeginLoadingPlaque(void);
void SCR_EndLoadingPlaque(void);

void SCR_ModeChanged(void);
void SCR_UpdateScreen(void);

#else // USE_CLIENT

#define CL_Init()                       (void)0
#define CL_Disconnect(type)             (void)0
#define CL_Shutdown()                   (void)0
#define CL_UpdateUserinfo(var, from)    (void)0
#define CL_ErrorEvent(from)             (void)0
#define CL_RestartFilesystem(total)     FS_Restart(total)
#define CL_ForwardToServer()            false
#define CL_CheatsOK()                   (bool)Cvar_VariableInteger("cheats")

#define Con_Init()                      (void)0
#define Con_SetColor(color)             (void)0
#define Con_SkipNotify(skip)            (void)0
#define Con_Print(text)                 (void)0

#define SCR_BeginLoadingPlaque()        (void)0
#define SCR_EndLoadingPlaque()          (void)0

#endif // !USE_CLIENT

#if USE_CLIENT && USE_AVCODEC
int SCR_CheckForCinematic(const char *name);
void SCR_Cinematic_g(void);
#else
#define SCR_CheckForCinematic(name)     Q_ERR(ENOSYS)
#define SCR_Cinematic_g(ctx)            (void)0
#endif

#if USE_REF && USE_DEBUG
void R_ClearDebugLines(void);
void R_AddDebugLine(const vec3_t start, const vec3_t end, uint32_t color, uint32_t time, bool depth_test);
void R_AddDebugPoint(const vec3_t point, float size, uint32_t color, uint32_t time, bool depth_test);
void R_AddDebugAxis(const vec3_t origin, const vec3_t angles, float size, uint32_t time, bool depth_test);
void R_AddDebugBounds(const vec3_t mins, const vec3_t maxs, uint32_t color, uint32_t time, bool depth_test);
void R_AddDebugSphere(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test);
void R_AddDebugCircle(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test);
void R_AddDebugCylinder(const vec3_t origin, float half_height, float radius, uint32_t color, uint32_t time,
                        bool depth_test);
void R_DrawArrowCap(const vec3_t apex, const vec3_t dir, float size,
                    uint32_t color, uint32_t time, bool depth_test);
void R_AddDebugArrow(const vec3_t start, const vec3_t end, float size, uint32_t line_color,
                     uint32_t arrow_color, uint32_t time, bool depth_test);
void R_AddDebugCurveArrow(const vec3_t start, const vec3_t ctrl, const vec3_t end, float size,
                          uint32_t line_color, uint32_t arrow_color, uint32_t time, bool depth_test);
void R_AddDebugText(const vec3_t origin, const vec3_t angles, const char *text,
                    float size, uint32_t color, uint32_t time, bool depth_test);
#else
static inline void R_ClearDebugLines(void) { }
static inline void R_AddDebugLine(const vec3_t start, const vec3_t end, uint32_t color, uint32_t time, bool depth_test) { }
static inline void R_AddDebugPoint(const vec3_t point, float size, uint32_t color, uint32_t time, bool depth_test) { }
static inline void R_AddDebugAxis(const vec3_t origin, const vec3_t angles, float size, uint32_t time, bool depth_test) { }
static inline void R_AddDebugBounds(const vec3_t mins, const vec3_t maxs, uint32_t color, uint32_t time, bool depth_test) { }
static inline void R_AddDebugSphere(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test) { }
static inline void R_AddDebugCircle(const vec3_t origin, float radius, uint32_t color, uint32_t time, bool depth_test) { }
static inline void R_AddDebugCylinder(const vec3_t origin, float half_height, float radius, uint32_t color, uint32_t time,
                                      bool depth_test) { }
static inline void R_DrawArrowCap(const vec3_t apex, const vec3_t dir, float size,
                                  uint32_t color, uint32_t time, bool depth_test) { }
static inline void R_AddDebugArrow(const vec3_t start, const vec3_t end, float size, uint32_t line_color,
                                   uint32_t arrow_color, uint32_t time, bool depth_test) { }
static inline void R_AddDebugCurveArrow(const vec3_t start, const vec3_t ctrl, const vec3_t end, float size,
                                        uint32_t line_color, uint32_t arrow_color, uint32_t time, bool depth_test) { }
static inline void R_AddDebugText(const vec3_t origin, const vec3_t angles, const char *text,
                                  float size, uint32_t color, uint32_t time, bool depth_test) { }
#endif
