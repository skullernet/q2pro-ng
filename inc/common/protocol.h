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

#define MAX_MSGLEN  0x10000     // max length of a message, 64 KiB

#define PROTOCOL_VERSION_MAJOR          36
#define PROTOCOL_VERSION_MINOR          2000
#define PROTOCOL_VERSION_MINOR_OLDEST   2000

#define Q2PRO_SUPPORTED(x) \
    ((x) >= PROTOCOL_VERSION_MINOR_OLDEST && \
     (x) <= PROTOCOL_VERSION_MINOR)

//=========================================

#define UPDATE_BACKUP   16  // copies of entity_state_t to keep buffered
                            // must be power of two
#define UPDATE_MASK     (UPDATE_BACKUP - 1)

#define FRAMEDELTA_BITS         4
#define FRAMEFLAGS_BITS         4
#define SERVERTIME_BITS         28  // avoid cg.time floating point issues

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
    svc_nop,
    svc_zpacket,
    svc_disconnect,
    svc_reconnect,
    svc_stringcmd,
    svc_configstring,
    svc_serverdata,
    svc_configstringstream,
    svc_baselinestream,
    svc_frame,

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

#define FF_SUPPRESSED   BIT(0)
#define FF_CLIENTDROP   BIT(1)
#define FF_CLIENTPRED   BIT(2)
#define FF_RESERVED     BIT(3)
