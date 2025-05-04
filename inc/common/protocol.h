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

#define Q2PRO_PF_STRAFEJUMP_HACK    BIT(0)
#define Q2PRO_PF_QW_MODE            BIT(1)
#define Q2PRO_PF_WATERJUMP_HACK     BIT(2)
#define Q2PRO_PF_EXTENSIONS         BIT(3)
#define Q2PRO_PF_EXTENSIONS_2       BIT(4)

//=========================================

#define UPDATE_BACKUP   16  // copies of entity_state_t to keep buffered
                            // must be power of two
#define UPDATE_MASK     (UPDATE_BACKUP - 1)

#define CMD_BACKUP      128 // allow a lot of command backups for very fast systems
                            // increased from 64
#define CMD_MASK        (CMD_BACKUP - 1)

#define SVCMD_BITS              5
#define SVCMD_MASK              MASK(SVCMD_BITS)

#define FRAMENUM_BITS           27
#define FRAMENUM_MASK           MASK(FRAMENUM_BITS)

#define SUPPRESSCOUNT_BITS      4
#define SUPPRESSCOUNT_MASK      MASK(SUPPRESSCOUNT_BITS)

#define MAX_PACKET_ENTITIES_OLD 128

#define MAX_PACKET_ENTITIES     512
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

    // these ops are known to the game dll
    svc_muzzleflash,
    svc_muzzleflash2,
    svc_temp_entity,
    svc_layout,
    svc_inventory,

    // the rest are private to the client and server
    svc_nop,
    svc_disconnect,
    svc_reconnect,
    svc_sound,                  // <see code>
    svc_print,                  // [byte] id [string] null terminated string
    svc_stufftext,              // [string] stuffed into client's console buffer
                                // should be \n terminated
    svc_serverdata,             // [long] protocol ...
    svc_configstring,           // [short] [string]
    svc_spawnbaseline,
    svc_centerprint,            // [string] to put in center of the screen
    svc_download,               // [short] size [size bytes]
    svc_playerinfo,             // variable
    svc_packetentities,         // [...]
    svc_deltapacketentities,    // [...]
    svc_frame,

    // R1Q2 specific operations
    svc_zpacket,
    svc_zdownload,
    svc_gamestate, // Q2PRO specific, means svc_playerupdate in R1Q2
    svc_setting,

    // Q2PRO specific operations
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
    clc_move,               // [usercmd_t]
    clc_userinfo,           // [userinfo string]
    clc_stringcmd,          // [string] message

    // R1Q2 specific operations
    clc_setting,

    // Q2PRO specific operations
    clc_move_nodelta = 10,
    clc_move_batched,
    clc_userinfo_delta
} clc_ops_t;

//==============================================

typedef enum {
    FOG_BIT_COLOR               = BIT(0),
    FOG_BIT_DENSITY             = BIT(1),
    FOG_BIT_HEIGHT_DENSITY      = BIT(2),
    FOG_BIT_HEIGHT_FALLOFF      = BIT(3),
    FOG_BIT_HEIGHT_START_COLOR  = BIT(4),
    FOG_BIT_HEIGHT_END_COLOR    = BIT(5),
    FOG_BIT_HEIGHT_START_DIST   = BIT(6),
    FOG_BIT_HEIGHT_END_DIST     = BIT(7),
} fog_bits_t;

// player_state_t communication

#define PS_M_TYPE           BIT(0)
#define PS_M_ORIGIN         BIT(1)
#define PS_M_VELOCITY       BIT(2)
#define PS_M_TIME           BIT(3)
#define PS_M_FLAGS          BIT(4)
#define PS_M_GRAVITY        BIT(5)
#define PS_M_DELTA_ANGLES   BIT(6)

#define PS_VIEWOFFSET       BIT(7)
#define PS_VIEWANGLES       BIT(8)
#define PS_KICKANGLES       BIT(9)
#define PS_BLEND            BIT(10)
#define PS_FOV              BIT(11)
#define PS_WEAPONINDEX      BIT(12)
#define PS_WEAPONFRAME      BIT(13)
#define PS_RDFLAGS          BIT(14)
#define PS_MOREBITS         BIT(15)     // read one additional byte

#define PS_FOG              BIT(16)

// R1Q2 protocol specific extra flags
#define EPS_GUNOFFSET       BIT(0)
#define EPS_GUNANGLES       BIT(1)
#define EPS_M_VELOCITY2     BIT(2)
#define EPS_M_ORIGIN2       BIT(3)
#define EPS_VIEWANGLE2      BIT(4)
#define EPS_STATS           BIT(5)

// Q2PRO protocol specific extra flags
#define EPS_CLIENTNUM       BIT(6)

//==============================================

// user_cmd_t communication

// ms and light always sent, the others are optional
#define CM_ANGLE1       BIT(0)
#define CM_ANGLE2       BIT(1)
#define CM_ANGLE3       BIT(2)
#define CM_FORWARD      BIT(3)
#define CM_SIDE         BIT(4)
#define CM_UP           BIT(5)
#define CM_BUTTONS      BIT(6)
#define CM_IMPULSE      BIT(7)

// R1Q2 button byte hacks
#define BUTTON_MASK     (BUTTON_ATTACK|BUTTON_USE|BUTTON_ANY)
#define BUTTON_FORWARD  BIT(2)
#define BUTTON_SIDE     BIT(3)
#define BUTTON_UP       BIT(4)
#define BUTTON_ANGLE1   BIT(5)
#define BUTTON_ANGLE2   BIT(6)

//==============================================

// a sound without an ent or pos will be a local only sound
#define SND_VOLUME          BIT(0)  // a byte
#define SND_ATTENUATION     BIT(1)  // a byte
#define SND_POS             BIT(2)  // three coordinates
#define SND_ENT             BIT(3)  // a short 0-2: channel, 3-15: entity
#define SND_OFFSET          BIT(4)  // a byte, msec offset from frame start
#define SND_INDEX16         BIT(5)  // index is 16-bit

#define DEFAULT_SOUND_PACKET_VOLUME         1.0f
#define DEFAULT_SOUND_PACKET_ATTENUATION    1.0f

//==============================================

// entity_state_t communication

// try to pack the common update flags into the first byte
#define U_ORIGIN1       BIT_ULL(0)
#define U_ORIGIN2       BIT_ULL(1)
#define U_ANGLE2        BIT_ULL(2)
#define U_ANGLE3        BIT_ULL(3)
#define U_FRAME8        BIT_ULL(4)      // frame is a byte
#define U_EVENT         BIT_ULL(5)
#define U_REMOVE        BIT_ULL(6)      // REMOVE this entity, don't add it
#define U_MOREBITS1     BIT_ULL(7)      // read one additional byte

// second byte
#define U_NUMBER16      BIT_ULL(8)      // NUMBER8 is implicit if not set
#define U_ORIGIN3       BIT_ULL(9)
#define U_ANGLE1        BIT_ULL(10)
#define U_MODEL         BIT_ULL(11)
#define U_RENDERFX8     BIT_ULL(12)     // fullbright, etc
#define U_ANGLE16       BIT_ULL(13)
#define U_EFFECTS8      BIT_ULL(14)     // autorotate, trails, etc
#define U_MOREBITS2     BIT_ULL(15)     // read one additional byte

// third byte
#define U_SKIN8         BIT_ULL(16)
#define U_FRAME16       BIT_ULL(17)     // frame is a short
#define U_RENDERFX16    BIT_ULL(18)     // 8 + 16 = 32
#define U_EFFECTS16     BIT_ULL(19)     // 8 + 16 = 32
#define U_MODEL2        BIT_ULL(20)     // weapons, flags, etc
#define U_MODEL3        BIT_ULL(21)
#define U_MODEL4        BIT_ULL(22)
#define U_MOREBITS3     BIT_ULL(23)     // read one additional byte

// fourth byte
#define U_OLDORIGIN     BIT_ULL(24)     // FIXME: get rid of this
#define U_SKIN16        BIT_ULL(25)
#define U_SOUND         BIT_ULL(26)
#define U_SOLID         BIT_ULL(27)
#define U_MODEL16       BIT_ULL(28)
#define U_MOREFX8       BIT_ULL(29)
#define U_ALPHA         BIT_ULL(30)
#define U_MOREBITS4     BIT_ULL(31)     // read one additional byte

// fifth byte
#define U_SCALE         BIT_ULL(32)
#define U_MOREFX16      BIT_ULL(33)

#define U_SKIN32        (U_SKIN8 | U_SKIN16)        // used for laser colors
#define U_EFFECTS32     (U_EFFECTS8 | U_EFFECTS16)
#define U_RENDERFX32    (U_RENDERFX8 | U_RENDERFX16)
#define U_MOREFX32      (U_MOREFX8 | U_MOREFX16)

// ==============================================================

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
