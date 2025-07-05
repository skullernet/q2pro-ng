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

#include "common/cvar.h"
#include "common/protocol.h"
#include "common/sizebuf.h"

extern sizebuf_t    msg_write;
extern byte         msg_write_buffer[MAX_MSGLEN];

extern sizebuf_t    msg_read;
extern byte         msg_read_buffer[MAX_MSGLEN];

extern const entity_state_t     nullEntityState;
extern const player_state_t     nullPlayerState;
extern const usercmd_t          nullUserCmd;

extern uint32_t     msg_max_entity_bytes;

void    MSG_Init(void);
void    MSG_Clear(void);

void    MSG_BeginWriting(void);
void    MSG_WriteByte(int c);
void    MSG_WriteShort(int c);
void    MSG_WriteLong(int c);
void    MSG_WriteLong64(int64_t c);
void    MSG_WriteString(const char *s);
void    MSG_WriteBits(int value, int bits);
void    MSG_WriteBit(bool value);
void    MSG_FlushBits(void);
#if USE_CLIENT
void    MSG_WriteDeltaUsercmd(const usercmd_t *from, const usercmd_t *to);
#endif
void    MSG_WriteBaseEntity(const entity_state_t *to);
void    MSG_WriteDeltaEntity(const entity_state_t *from, const entity_state_t *to, bool force);
void    MSG_WriteDeltaPlayerstate(const player_state_t *from, const player_state_t *to);

static inline void *MSG_WriteData(const void *data, size_t len)
{
    return memcpy(SZ_GetSpace(&msg_write, len), data, len);
}

static inline void MSG_FlushTo(sizebuf_t *buf)
{
    SZ_Write(buf, msg_write.data, msg_write.cursize);
    SZ_Clear(&msg_write);
}

void    MSG_BeginReading(void);
byte    *MSG_ReadData(size_t len);
int     MSG_ReadByte(void);
int     MSG_ReadShort(void);
int     MSG_ReadLong(void);
int64_t MSG_ReadLong64(void);
size_t  MSG_ReadString(char *dest, size_t size);
size_t  MSG_ReadStringLine(char *dest, size_t size);
int     MSG_ReadBits(int bits);
bool    MSG_ReadBit(void);
void    MSG_ReadDeltaUsercmd(const usercmd_t *from, usercmd_t *cmd);
void    MSG_ParseDeltaEntity(entity_state_t *to, unsigned number);
#if USE_CLIENT
void    MSG_ParseDeltaPlayerstate(player_state_t *to);
#endif

#if USE_CLIENT
#if USE_DEBUG
extern cvar_t   *cl_shownet;
#define SHOWNET(level, ...) \
    do { if (cl_shownet->integer >= level) \
        Com_LPrintf(PRINT_DEVELOPER, __VA_ARGS__); } while (0)
const char *MSG_ServerCommandString(int cmd);
#else
#define SHOWNET(...)
#endif
#endif // USE_CLIENT
