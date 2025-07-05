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

#include "shared/shared.h"
#include "common/msg.h"
#include "common/protocol.h"
#include "common/sizebuf.h"
#include "common/math.h"
#include "common/intreadwrite.h"

/*
==============================================================================

            MESSAGE IO FUNCTIONS

Handles byte ordering and avoids alignment errors
==============================================================================
*/

sizebuf_t   msg_write;
byte        msg_write_buffer[MAX_MSGLEN];

sizebuf_t   msg_read;
byte        msg_read_buffer[MAX_MSGLEN];

uint32_t    msg_max_entity_bytes;

const entity_state_t    nullEntityState;
const player_state_t    nullPlayerState;
const usercmd_t         nullUserCmd;

/*
=============
MSG_Clear

Initialize default buffers (also called from Com_Error).
This is the only place where writing buffer is initialized.
=============
*/
void MSG_Clear(void)
{
    SZ_Init(&msg_read, msg_read_buffer, MAX_MSGLEN, "msg_read");
    SZ_Init(&msg_write, msg_write_buffer, MAX_MSGLEN, "msg_write");
    msg_read.allowunderflow = true;
    msg_write.allowoverflow = true;
}


/*
==============================================================================

            WRITING

==============================================================================
*/

/*
=============
MSG_BeginWriting
=============
*/
void MSG_BeginWriting(void)
{
    msg_write.cursize = 0;
    msg_write.bits_buf = 0;
    msg_write.bits_left = 32;
    msg_write.overflowed = false;
}

/*
=============
MSG_WriteByte
=============
*/
void MSG_WriteByte(int c)
{
    WN8(SZ_GetSpace(&msg_write, 1), c);
}

/*
=============
MSG_WriteShort
=============
*/
void MSG_WriteShort(int c)
{
    WL16(SZ_GetSpace(&msg_write, 2), c);
}

/*
=============
MSG_WriteLong
=============
*/
void MSG_WriteLong(int c)
{
    WL32(SZ_GetSpace(&msg_write, 4), c);
}

/*
=============
MSG_WriteLong64
=============
*/
void MSG_WriteLong64(int64_t c)
{
    WL64(SZ_GetSpace(&msg_write, 8), c);
}

/*
=============
MSG_WriteString
=============
*/
void MSG_WriteString(const char *string)
{
    SZ_WriteString(&msg_write, string);
}

/*
=============
MSG_WriteBits
=============
*/
void MSG_WriteBits(int value, int bits)
{
    Q_assert(!(bits == 0 || bits < -32 || bits > 32));

    if (bits < 0) {
        bits = -bits;
    }

    uint64_t bits_buf  = msg_write.bits_buf;
    uint32_t bits_left = msg_write.bits_left;
    uint64_t v = value & MASK_ULL(bits);

    bits_buf |= v << (32 - bits_left);
    if (bits >= bits_left) {
        MSG_WriteLong(bits_buf);
        bits_buf   = v >> bits_left;
        bits_left += 32;
    }
    bits_left -= bits;

    msg_write.bits_buf  = bits_buf;
    msg_write.bits_left = bits_left;
}

void MSG_WriteBit(bool value)
{
    if (!msg_write.bits_left) {
        MSG_WriteLong(msg_write.bits_buf);
        msg_write.bits_buf  = 0;
        msg_write.bits_left = 32;
    }

    msg_write.bits_buf |= (uint32_t)value << (32 - msg_write.bits_left);
    msg_write.bits_left--;
}

static void MSG_WriteLeb32(uint32_t v)
{
    while (v) {
        MSG_WriteBit(1);
        MSG_WriteBits(v, 8);
        v >>= 8;
    }
    MSG_WriteBit(0);
}

static void MSG_WriteLeb64(uint64_t v)
{
    while (v) {
        MSG_WriteBit(1);
        MSG_WriteBits(v, 8);
        v >>= 8;
    }
    MSG_WriteBit(0);
}

/*
=============
MSG_FlushBits
=============
*/
void MSG_FlushBits(void)
{
    uint32_t bits_buf  = msg_write.bits_buf;
    uint32_t bits_left = msg_write.bits_left;

    while (bits_left < 32) {
        MSG_WriteByte(bits_buf & 255);
        bits_buf >>= 8;
        bits_left += 8;
    }

    msg_write.bits_buf  = 0;
    msg_write.bits_left = 32;
}

#if USE_CLIENT

static void MSG_WriteDeltaAngle(int from, int to)
{
    int delta = to - from;

    if (!delta) {
        MSG_WriteBit(0);
        return;
    }

    MSG_WriteBit(1);
    if (delta >= -128 && delta <= 127) {
        MSG_WriteBit(1);
        MSG_WriteBits(delta, -8);
    } else {
        MSG_WriteBit(0);
        MSG_WriteBits(to, -16);
    }
}

static void MSG_WriteDeltaValue(int from, int to, int bits)
{
    if (from == to) {
        MSG_WriteBit(0);
        return;
    }
    MSG_WriteBit(1);
    MSG_WriteBits(to, bits);
}

/*
=============
MSG_WriteDeltaUsercmd
=============
*/
void MSG_WriteDeltaUsercmd(const usercmd_t *from, const usercmd_t *to)
{
    Q_assert(to);

    if (!from)
        from = &nullUserCmd;

    if (!memcmp(from, to, sizeof(*to))) {
        MSG_WriteBit(0);
        return;
    }

//
// send the movement message
//
    MSG_WriteBit(1);
    MSG_WriteDeltaAngle(from->angles[0], to->angles[0]);
    MSG_WriteDeltaAngle(from->angles[1], to->angles[1]);
    MSG_WriteDeltaValue(from->angles[2], to->angles[2], -16);
    MSG_WriteDeltaValue(from->forwardmove, to->forwardmove, -10);
    MSG_WriteDeltaValue(from->sidemove, to->sidemove, -10);
    MSG_WriteDeltaValue(from->upmove, to->upmove, -10);
    MSG_WriteDeltaValue(from->buttons, to->buttons, 8);
    MSG_WriteDeltaValue(from->msec, to->msec, 8);
}

#endif // USE_CLIENT

typedef struct {
    const char *name;
    int offset;
    int bits;
} netfield_t;

#define NETF(f, bits)  { #f, offsetof(entity_state_t, f), bits }

static const netfield_t entity_state_fields[] = {
    NETF(angles[0], -2),
    NETF(angles[1], -2),
    NETF(angles[2], -2),
    NETF(origin[0], 0),
    NETF(origin[1], 0),
    NETF(origin[2], 0),
    NETF(old_origin[0], 0),
    NETF(old_origin[1], 0),
    NETF(old_origin[2], 0),
    NETF(modelindex, MODELINDEX_BITS),
    NETF(modelindex2, MODELINDEX_BITS),
    NETF(modelindex3, MODELINDEX_BITS),
    NETF(modelindex4, MODELINDEX_BITS),
    NETF(skinnum, -1),
    NETF(effects, -1),
    NETF(renderfx, -1),
    NETF(solid, 32),
    NETF(morefx, -1),
    NETF(frame, -1),
    NETF(sound, -1),
    NETF(event[0], 8),
    NETF(event[1], 8),
    NETF(event[2], 8),
    NETF(event[3], 8),
    NETF(event_param[0], -1),
    NETF(event_param[1], -1),
    NETF(event_param[2], -1),
    NETF(event_param[3], -1),
    NETF(alpha, 0),
    NETF(scale, 0),
    NETF(othernum, ENTITYNUM_BITS),
};

static unsigned entity_state_counts[q_countof(entity_state_fields)];

static const int entity_state_nc_bits = 32 - __builtin_clz(q_countof(entity_state_fields));

#undef NETF

static int MSG_CountDeltaMaxBits(const netfield_t *f, int n)
{
    int bits = 0;

    for (int i = 0; i < n; i++, f++) {
        bits++;
        if (f->bits == 0)
            bits += 2 + 32;
        else if (f->bits == -1)
            bits += 4 * 9;
        else if (f->bits == -2)
            bits += 16;
        else
            bits += abs(f->bits);
    }

    return bits;
}

static int MSG_CountDeltaFields(const netfield_t *f, int n, const void *from, const void *to, unsigned *counts)
{
    int nc = 0;

    for (int i = 0; i < n; i++, f++) {
        uint32_t from_v = RN32((const byte *)from + f->offset);
        uint32_t to_v   = RN32((const byte *)to   + f->offset);

        if (from_v == to_v)
            continue;

        counts[i]++;
        nc = i + 1;
    }

    return nc;
}

#define FLOAT_INT_BITS  14
#define FLOAT_INT_BIAS  (1 << (FLOAT_INT_BITS - 1))

static void MSG_WriteDeltaFields(const netfield_t *f, int n, const void *from, const void *to)
{
    for (int i = 0; i < n; i++, f++) {
        uint32_t from_v = RN32((const byte *)from + f->offset);
        uint32_t to_v   = RN32((const byte *)to   + f->offset);

        if (from_v == to_v) {
            MSG_WriteBit(0);    // not changed
            continue;
        }

        MSG_WriteBit(1);

        if (f->bits == 0) {
            float f_val = LongToFloat(to_v);
            int   i_val = f_val;

            if (f_val == 0.0f) {
                MSG_WriteBit(0);
            } else {
                MSG_WriteBit(1);
                if (f_val == i_val && i_val >= -FLOAT_INT_BIAS && i_val < FLOAT_INT_BIAS) {
                    MSG_WriteBit(1);
                    MSG_WriteBits(i_val + FLOAT_INT_BIAS, FLOAT_INT_BITS);
                } else {
                    MSG_WriteBit(0);
                    MSG_WriteBits(to_v, 32);
                }
            }
        } else if (f->bits == -1) {
            MSG_WriteLeb32(to_v);
        } else if (f->bits == -2) {
            MSG_WriteBits(ANGLE2SHORT(LongToFloat(to_v)), 16);
        } else {
            MSG_WriteBits(to_v, f->bits);
        }
    }
}

void MSG_WriteDeltaEntity(const entity_state_t *from, const entity_state_t *to, bool force)
{
    if (!to) {
        Q_assert(from);
        Q_assert(from->number < ENTITYNUM_WORLD);

        MSG_WriteBits(from->number, ENTITYNUM_BITS);
        MSG_WriteBit(1);    // removed
        return;
    }

    Q_assert(to->number < ENTITYNUM_WORLD);

    if (!from)
        from = &nullEntityState;

    int nc = MSG_CountDeltaFields(entity_state_fields, q_countof(entity_state_fields), from, to, entity_state_counts);
    if (!nc) {
        if (!force)
            return;     // nothing to send!
        MSG_WriteBits(to->number, ENTITYNUM_BITS);
        MSG_WriteBit(0);    // not removed
        MSG_WriteBit(0);    // not changed
        return;
    }

    MSG_WriteBits(to->number, ENTITYNUM_BITS);
    MSG_WriteBit(0);    // not removed
    MSG_WriteBit(1);    // changed
    MSG_WriteBits(nc, entity_state_nc_bits);
    MSG_WriteDeltaFields(entity_state_fields, nc, from, to);
}

void MSG_WriteBaseEntity(const entity_state_t *to)
{
    Q_assert(to->number < ENTITYNUM_WORLD);

    const entity_state_t *from = &nullEntityState;

    int nc = MSG_CountDeltaFields(entity_state_fields, q_countof(entity_state_fields), from, to, entity_state_counts);
    if (!nc)
        return;

    MSG_WriteBits(to->number, ENTITYNUM_BITS);
    MSG_WriteBits(nc, entity_state_nc_bits);
    MSG_WriteDeltaFields(entity_state_fields, nc, from, to);
}

#define NETF(f, bits)  { #f, offsetof(player_state_t, f), bits }

static const netfield_t player_state_fields[] = {
    NETF(pm_type, 8),
    NETF(origin[0], 0),
    NETF(origin[1], 0),
    NETF(origin[2], 0),
    NETF(velocity[0], 0),
    NETF(velocity[1], 0),
    NETF(velocity[2], 0),
    NETF(pm_flags, 16),
    NETF(pm_time, 16),
    NETF(gravity, -16),
    NETF(delta_angles[0], -16),
    NETF(delta_angles[1], -16),
    NETF(delta_angles[2], -16),

    NETF(clientnum, ENTITYNUM_BITS),
    NETF(viewangles[0], -2),
    NETF(viewangles[1], -2),
    NETF(viewangles[2], -2),
    NETF(viewheight, -8),
    NETF(bobtime, 8),
    NETF(gunindex, MODELINDEX_BITS),
    NETF(gunskin, 4),
    NETF(gunframe, 8),
    NETF(screen_blend[0], 0),
    NETF(screen_blend[1], 0),
    NETF(screen_blend[2], 0),
    NETF(screen_blend[3], 0),
    NETF(damage_blend[0], 0),
    NETF(damage_blend[1], 0),
    NETF(damage_blend[2], 0),
    NETF(damage_blend[3], 0),
    NETF(fov, 8),
    NETF(rdflags, 8),

    NETF(fog.color[0], 0),
    NETF(fog.color[1], 0),
    NETF(fog.color[2], 0),
    NETF(fog.density, 0),
    NETF(fog.sky_factor, 0),

    NETF(heightfog.start.color[0], 0),
    NETF(heightfog.start.color[1], 0),
    NETF(heightfog.start.color[2], 0),
    NETF(heightfog.start.dist, 0),

    NETF(heightfog.end.color[0], 0),
    NETF(heightfog.end.color[1], 0),
    NETF(heightfog.end.color[2], 0),
    NETF(heightfog.end.dist, 0),

    NETF(heightfog.density, 0),
    NETF(heightfog.falloff, 0),
};

static unsigned player_state_counts[q_countof(player_state_fields)];

static const int player_state_nc_bits = 32 - __builtin_clz(q_countof(player_state_fields));

#undef NETF

void MSG_WriteDeltaPlayerstate(const player_state_t *from, const player_state_t *to)
{
    Q_assert(to);

    if (!from)
        from = &nullPlayerState;

    uint64_t statbits = 0;
    for (int i = 0; i < MAX_STATS; i++)
        if (to->stats[i] != from->stats[i])
            statbits |= BIT_ULL(i);

    int nc = MSG_CountDeltaFields(player_state_fields, q_countof(player_state_fields), from, to, player_state_counts);
    if (!nc && !statbits) {
        MSG_WriteBit(0);
        return;
    }

    MSG_WriteBit(1);
    MSG_WriteBits(nc, player_state_nc_bits);
    MSG_WriteDeltaFields(player_state_fields, nc, from, to);

    MSG_WriteLeb64(statbits);
    if (statbits)
        for (int i = 0; i < MAX_STATS; i++)
            if (statbits & BIT_ULL(i))
                MSG_WriteBits(to->stats[i], -16);
}


/*
==============================================================================

            READING

==============================================================================
*/

void MSG_BeginReading(void)
{
    msg_read.readcount = 0;
    msg_read.bits_buf  = 0;
    msg_read.bits_left = 0;
}

byte *MSG_ReadData(size_t len)
{
    return SZ_ReadData(&msg_read, len);
}

int MSG_ReadByte(void)
{
    byte *buf = MSG_ReadData(1);
    return buf ? RN8(buf) : -1;
}

int MSG_ReadShort(void)
{
    byte *buf = MSG_ReadData(2);
    return buf ? RL16(buf) : -1;
}

int MSG_ReadLong(void)
{
    byte *buf = MSG_ReadData(4);
    return buf ? RL32(buf) : -1;
}

int64_t MSG_ReadLong64(void)
{
    byte *buf = MSG_ReadData(8);
    return buf ? RL64(buf) : -1;
}

size_t MSG_ReadString(char *dest, size_t size)
{
    int     c;
    size_t  len = 0;

    while (1) {
        c = MSG_ReadByte();
        if (c == -1 || c == 0) {
            break;
        }
        if (len + 1 < size) {
            *dest++ = c;
        }
        len++;
    }
    if (size) {
        *dest = 0;
    }

    return len;
}

size_t MSG_ReadStringLine(char *dest, size_t size)
{
    int     c;
    size_t  len = 0;

    while (1) {
        c = MSG_ReadByte();
        if (c == -1 || c == 0 || c == '\n') {
            break;
        }
        if (len + 1 < size) {
            *dest++ = c;
        }
        len++;
    }
    if (size) {
        *dest = 0;
    }

    return len;
}

int MSG_ReadBits(int bits)
{
    bool sgn = false;

    Q_assert(!(bits == 0 || bits < -32 || bits > 32));

    if (bits < 0) {
        bits = -bits;
        sgn = true;
    }

    uint64_t bits_buf  = msg_read.bits_buf;
    uint32_t bits_left = msg_read.bits_left;

    while (bits > bits_left) {
        bits_buf  |= (uint64_t)MSG_ReadByte() << bits_left;
        bits_left += 8;
    }

    uint32_t value = bits_buf & MASK_ULL(bits);

    msg_read.bits_buf  = bits_buf >> bits;
    msg_read.bits_left = bits_left - bits;

    if (sgn) {
        return SignExtend(value, bits);
    }

    return value;
}

bool MSG_ReadBit(void)
{
    if (!msg_read.bits_left) {
        msg_read.bits_buf  = MSG_ReadByte();
        msg_read.bits_left = 8;
    }

    bool v = msg_read.bits_buf & 1;
    msg_read.bits_buf >>= 1;
    msg_read.bits_left--;
    return v;
}

static int MSG_ReadDeltaAngle(int from)
{
    if (!MSG_ReadBit())
        return from;
    if (MSG_ReadBit())
        return from + MSG_ReadBits(-8);
    return MSG_ReadBits(-16);
}

static int MSG_ReadDeltaValue(int from, int bits)
{
    if (MSG_ReadBit())
        return MSG_ReadBits(bits);
    return from;
}

void MSG_ReadDeltaUsercmd(const usercmd_t *from, usercmd_t *to)
{
    Q_assert(to);

    if (!from)
        from = &nullUserCmd;

    if (!MSG_ReadBit()) {
        memcpy(to, from, sizeof(*to));
        return;
    }

    to->angles[0] = MSG_ReadDeltaAngle(from->angles[0]);
    to->angles[1] = MSG_ReadDeltaAngle(from->angles[1]);
    to->angles[2] = MSG_ReadDeltaValue(from->angles[2], -16);
    to->forwardmove = MSG_ReadDeltaValue(from->forwardmove, -10);
    to->sidemove = MSG_ReadDeltaValue(from->sidemove, -10);
    to->upmove = MSG_ReadDeltaValue(from->upmove, -10);
    to->buttons = MSG_ReadDeltaValue(from->buttons, 8);
    to->msec = MSG_ReadDeltaValue(from->msec, 8);
}

#if USE_CLIENT

static uint32_t MSG_ReadLeb32(void)
{
    uint32_t v = 0;
    int bits = 0;

    while (MSG_ReadBit()) {
        if (bits == 32)
            Com_Error(ERR_DROP, "%s: overlong", __func__);
        v |= (uint32_t)MSG_ReadBits(8) << bits;
        bits += 8;
    }

    return v;
}

static uint64_t MSG_ReadLeb64(void)
{
    uint64_t v = 0;
    int bits = 0;

    while (MSG_ReadBit()) {
        if (bits == 64)
            Com_Error(ERR_DROP, "%s: overlong", __func__);
        v |= (uint64_t)MSG_ReadBits(8) << bits;
        bits += 8;
    }

    return v;
}

static void MSG_ReadDeltaFields(const netfield_t *f, int n, void *to)
{
    for (int i = 0; i < n; i++, f++) {
        uint32_t to_v;

        if (!MSG_ReadBit())
            continue;

        if (f->bits == 0) {
            if (MSG_ReadBit()) {
                if (MSG_ReadBit()) {
                    to_v = FloatToLong(MSG_ReadBits(FLOAT_INT_BITS) - FLOAT_INT_BIAS);
                } else {
                    to_v = MSG_ReadBits(32);
                }
            } else {
                to_v = FloatToLong(0.0f);
            }
            SHOWNET(3, "%s:%g ", f->name, LongToFloat(to_v));
        } else if (f->bits == -1) {
            to_v = MSG_ReadLeb32();
            SHOWNET(3, "%s:%#x ", f->name, to_v);
        } else if (f->bits == -2) {
            to_v = FloatToLong(SHORT2ANGLE(MSG_ReadBits(16)));
            SHOWNET(3, "%s:%g ", f->name, LongToFloat(to_v));
        } else {
            to_v = MSG_ReadBits(f->bits);
            SHOWNET(3, "%s:%d ", f->name, to_v);
        }

        WN32((byte *)to + f->offset, to_v);
    }
}

/*
==================
MSG_ParseDeltaEntity

Can go from either a baseline or a previous packet_entity
==================
*/
void MSG_ParseDeltaEntity(entity_state_t *to, unsigned number)
{
    Q_assert(to);
    Q_assert(number < ENTITYNUM_WORLD);

    to->number = number;

    int nc = MSG_ReadBits(entity_state_nc_bits);
    if (nc > q_countof(entity_state_fields))
        Com_Error(ERR_DROP, "%s: bad number of fields", __func__);

    MSG_ReadDeltaFields(entity_state_fields, nc, to);
}

/*
===================
MSG_ParseDeltaPlayerstate
===================
*/
void MSG_ParseDeltaPlayerstate(player_state_t *to)
{
    Q_assert(to);

    if (!MSG_ReadBit())
        return;

    int nc = MSG_ReadBits(player_state_nc_bits);
    if (nc > q_countof(player_state_fields))
        Com_Error(ERR_DROP, "%s: bad number of fields", __func__);

    MSG_ReadDeltaFields(player_state_fields, nc, to);

    uint64_t statbits = MSG_ReadLeb64();
    if (statbits)
        for (int i = 0; i < MAX_STATS; i++)
            if (statbits & BIT_ULL(i))
                to->stats[i] = MSG_ReadBits(-16);
}

#endif // USE_CLIENT


/*
==============================================================================

            DEBUGGING STUFF

==============================================================================
*/

#if USE_CLIENT && USE_DEBUG

const char *MSG_ServerCommandString(int cmd)
{
    switch (cmd) {
    case -1: return "END OF MESSAGE";
    default: return "UNKNOWN COMMAND";
#define S(x) case svc_##x: return "svc_" #x;
        S(bad)
        S(nop)
        S(disconnect)
        S(reconnect)
        S(stringcmd)
        S(serverdata)
        S(configstring)
        S(download)
        S(frame)
        S(zpacket)
        S(zdownload)
        S(configstringstream)
        S(baselinestream)
#undef S
    }
}

#endif // USE_CLIENT && USE_DEBUG

static void MSG_ChangeVectors_f(void)
{
    Com_Printf("\n");
    for (int i = 0; i < q_countof(entity_state_fields); i++)
        Com_Printf("%s: %u\n", entity_state_fields[i].name, entity_state_counts[i]);

    Com_Printf("\n");
    for (int i = 0; i < q_countof(player_state_fields); i++)
        Com_Printf("%s: %u\n", player_state_fields[i].name, player_state_counts[i]);
}

void MSG_Init(void)
{
    int bits = ENTITYNUM_BITS + 2 + MSG_CountDeltaMaxBits(entity_state_fields, q_countof(entity_state_fields));
    msg_max_entity_bytes = (bits + 7) / 8;

    MSG_Clear();

    Cmd_AddCommand("changevectors", MSG_ChangeVectors_f);
}
