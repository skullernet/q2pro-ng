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

#pragma once

#include "common/cmd.h"
#include "common/cvar.h"
#include "common/utils.h"

//
// common.h -- definitions common between client and server, but not game.dll
//

#define PRODUCT         "Q2PRO-NG"

#if USE_CLIENT
#define APPLICATION     "q2pro-ng"
#else
#define APPLICATION     "q2proded-ng"
#endif

#define COM_DEFAULT_CFG     "default.cfg"
#define COM_AUTOEXEC_CFG    "autoexec.cfg"
#define COM_POSTEXEC_CFG    "postexec.cfg"
#define COM_POSTINIT_CFG    "postinit.cfg"
#define COM_CONFIG_CFG      "config.cfg"

// FIXME: rename these
#define COM_HISTORYFILE_NAME    ".conhistory"
#define COM_DEMOCACHE_NAME      ".democache"
#define SYS_HISTORYFILE_NAME    ".syshistory"

#define MAXPRINTMSG     4096
#define MAXERRORMSG     1024

typedef struct {
    const char *name;
    void (*func)(void);
} ucmd_t;

static inline const ucmd_t *Com_Find(const ucmd_t *u, const char *c)
{
    for (; u->name; u++) {
        if (!strcmp(c, u->name)) {
            return u;
        }
    }
    return NULL;
}

typedef struct string_entry_s {
    struct string_entry_s *next;
    char string[1];
} string_entry_t;

typedef void (*rdflush_t)(int target, const char *buffer, size_t len);

void        Com_BeginRedirect(int target, char *buffer, size_t buffersize, rdflush_t flush);
void        Com_EndRedirect(void);

void        Com_AbortFunc(void (*func)(void *), void *arg);

q_cold
void        Com_SetLastError(const char *msg);

q_cold
const char  *Com_GetLastError(void);

q_noreturn
void        Com_Quit(const char *reason, error_type_t type);

void        Com_SetColor(color_index_t color);

void        Com_Address_g(void);
void        Com_Generic_c(int firstarg, int argnum);
#if USE_CLIENT
void        Com_Color_g(void);
#endif

size_t      Com_Time_m(char *buffer, size_t size);
size_t      Com_Uptime_m(char *buffer, size_t size);
size_t      Com_UptimeLong_m(char *buffer, size_t size);

#ifndef _WIN32
void        Com_FlushLogs(void);
#endif

void        Com_AddConfigFile(const char *name, unsigned flags);

#if USE_SYSCON
void        Sys_Printf(const char *fmt, ...) q_printf(1, 2);
#else
#define     Sys_Printf(...) (void)0
#endif

#if USE_CLIENT
#define COM_DEDICATED   (dedicated->integer != 0)
#else
#define COM_DEDICATED   1
#endif

#if USE_DEBUG
#define COM_DEVELOPER   (developer->integer)
#define Com_DPrintf(...) \
    do { if (developer && developer->integer >= 1) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__); } while (0)
#define Com_DDPrintf(...) \
    do { if (developer && developer->integer >= 2) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__); } while (0)
#define Com_DDDPrintf(...) \
    do { if (developer && developer->integer >= 3) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__); } while (0)
#define Com_DDDDPrintf(...) \
    do { if (developer && developer->integer >= 4) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__); } while (0)
#define Com_DWPrintf(...) \
    do { if (developer && developer->integer >= 1) \
        Com_LPrintf(PRINT_WARNING, __VA_ARGS__); } while (0)
#else
#define COM_DEVELOPER   0
#define Com_DPrintf(...) ((void)0)
#define Com_DDPrintf(...) ((void)0)
#define Com_DDDPrintf(...) ((void)0)
#define Com_DDDDPrintf(...) ((void)0)
#define Com_DWPrintf(...) ((void)0)
#endif

#if USE_TESTS
extern cvar_t   *z_perturb;
#endif

#if USE_DEBUG
extern cvar_t   *developer;
#endif
extern cvar_t   *dedicated;
#if USE_CLIENT
extern cvar_t   *host_speeds;
#endif
extern cvar_t   *com_version;

#if USE_CLIENT
extern cvar_t   *cl_running;
extern cvar_t   *cl_paused;
#endif
extern cvar_t   *sv_running;
extern cvar_t   *sv_paused;
extern cvar_t   *com_timedemo;
extern cvar_t   *com_native_modules;

extern cvar_t   *rcon_password;

#if USE_SYSCON
extern cvar_t   *sys_history;
#endif

#if USE_CLIENT
// host_speeds times
extern unsigned     time_before_game;
extern unsigned     time_after_game;
extern unsigned     time_before_ref;
extern unsigned     time_after_ref;
#endif

extern const char   com_version_string[];

extern unsigned     com_framenum;
extern unsigned     com_eventTime; // system time of the last event
extern unsigned     com_localTime; // milliseconds since Q2 startup
extern unsigned     com_localTime2; // milliseconds since Q2 startup, but doesn't run if paused
extern bool         com_initialized;
extern time_t       com_startTime;

void Qcommon_Init(int argc, char **argv);
void Qcommon_Frame(void);
