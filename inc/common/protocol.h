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

//
// protocol.h -- communications protocols
//

#define MAX_MSGLEN  0x8000      // max length of a message, 32 KiB

#define PROTOCOL_VERSION_MAJOR          36
#define PROTOCOL_VERSION_MINOR          2000
#define PROTOCOL_VERSION_MINOR_OLDEST   2000

#define Q2PRO_SUPPORTED(x) \
    ((x) >= PROTOCOL_VERSION_MINOR_OLDEST && \
     (x) <= PROTOCOL_VERSION_MINOR)

#define VALIDATE_CLIENTNUM(x) \
    ((x) >= -1 && (x) < MAX_EDICTS - 1)

//=========================================

#define UPDATE_BACKUP   16  // copies of entity_state_t to keep buffered
                            // must be power of two
#define UPDATE_MASK     (UPDATE_BACKUP - 1)

#define CMD_BACKUP      128 // allow a lot of command backups for very fast systems
                            // increased from 64
#define CMD_MASK        (CMD_BACKUP - 1)

#define SVCMD_BITS              5
#define FRAMENUM_BITS           27
#define DELTAFRAME_BITS         5
#define FRAMEFLAGS_BITS         4
#define SUPPRESSCOUNT_BITS      4

#define MAX_PARSE_ENTITIES      (MAX_PACKET_ENTITIES * UPDATE_BACKUP)
#define PARSE_ENTITIES_MASK     (MAX_PARSE_ENTITIES - 1)

#define MAX_PACKET_USERCMDS     32
#define MAX_PACKET_FRAMES       4

#define MAX_PACKET_STRINGCMDS   8
#define MAX_PACKET_USERINFOS    8

#define MVD_MAGIC               MakeRawLong('M','V','D','2')

//
// server to client
//
typedef enum {
    svc_bad,

    // the rest are private to the client and server
    svc_nop,
    svc_disconnect,
    svc_reconnect,
    svc_stringcmd,
    svc_serverdata,
    svc_configstring,
    svc_download,
    svc_frame,
    svc_zpacket,
    svc_zdownload,
    svc_configstringstream,
    svc_baselinestream,

    svc_num_types
} svc_ops_t;

//==============================================

//
// client to server
//
typedef enum {
    clc_bad,
    clc_nop,
    clc_move_nodelta,
    clc_move_batched,
    clc_userinfo,
    clc_userinfo_delta,
    clc_stringcmd,
} clc_ops_t;

//==============================================

// a client with this number will never be included in MVD stream
#define CLIENTNUM_NONE      (MAX_CLIENTS - 1)

// a SOLID_BBOX will never create this value
#define PACKED_BSP      31

typedef enum {
    // R1Q2 specific
    CLS_NOGUN,
    CLS_NOBLEND,
    CLS_RECORDING,
    CLS_PLAYERUPDATES,
    CLS_FPS,

    // Q2PRO specific
    CLS_NOGIBS            = 10,
    CLS_NOFOOTSTEPS,
    CLS_NOPREDICT,
    CLS_NOFLARES,

    CLS_MAX
} clientSetting_t;

typedef enum {
    // R1Q2 specific
    SVS_PLAYERUPDATES,
    SVS_FPS,

    SVS_MAX
} serverSetting_t;

// Q2PRO frame flags sent by the server
// only SUPPRESSCOUNT_BITS can be used
#define FF_SUPPRESSED   BIT(0)
#define FF_CLIENTDROP   BIT(1)
#define FF_CLIENTPRED   BIT(2)
#define FF_RESERVED     BIT(3)
