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

#include "common/net/net.h"

#if USE_ICMP
void SV_ErrorEvent(const netadr_t *from, int ee_errno, int ee_info);
#endif
void SV_Init(void);
void SV_Shutdown(const char *finalmsg, error_type_t type);
bool SV_CheckPaused(void);
gtime_t SV_Frame(gtime_t delta, gtime_t delta_s);
#if USE_SYSCON
void SV_SetConsoleTitle(void);
#endif
//void SV_ConsoleOutput(const char *msg);
void SV_CompleteCommand(int firstarg, int argnum);
void SV_RestartFilesystem(void);

#if USE_SAVEGAMES
char *SV_GetSaveInfo(const char *dir);
#else
#define SV_GetSaveInfo(dir) NULL
#endif
