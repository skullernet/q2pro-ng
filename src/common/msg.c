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

static void MSG_WriteSignedLeb32(int32_t v)
{
    MSG_WriteLeb32(((uint32_t)v << 1) ^ (v >> 31));
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

// special values for bits
typedef enum {
    NETF_FLOAT = 0,
    NETF_LEB   = -1,
    NETF_ANGLE = -2,
    NETF_COLOR = -3,
} netfield_kind_t;

#define NETF(f, bits)  { #f, offsetof(entity_state_t, f), bits }

static const netfield_t entity_state_fields[] = {
    NETF(angles[0], NETF_ANGLE),
    NETF(angles[1], NETF_ANGLE),
    NETF(angles[2], NETF_ANGLE),
    NETF(origin[0], NETF_FLOAT),
    NETF(origin[1], NETF_FLOAT),
    NETF(origin[2], NETF_FLOAT),
    NETF(modelindex, MODELINDEX_BITS),
    NETF(modelindex2, MODELINDEX_BITS),
    NETF(modelindex3, MODELINDEX_BITS),
    NETF(modelindex4, MODELINDEX_BITS),
    NETF(skinnum, NETF_LEB),
    NETF(effects, NETF_LEB),
    NETF(renderfx, NETF_LEB),
    NETF(solid, NETF_LEB),
    NETF(morefx, NETF_LEB),
    NETF(frame, NETF_LEB),
    NETF(sound, NETF_LEB),
    NETF(event[0], 8),
    NETF(event[1], 8),
    NETF(event[2], 8),
    NETF(event[3], 8),
    NETF(event_param[0], NETF_LEB),
    NETF(event_param[1], NETF_LEB),
    NETF(event_param[2], NETF_LEB),
    NETF(event_param[3], NETF_LEB),
    NETF(alpha, NETF_FLOAT),
    NETF(scale, NETF_FLOAT),
    NETF(othernum, ENTITYNUM_BITS),
};

static const netfield_t entity_state_fields2[] = {
    NETF(old_origin[0], NETF_FLOAT),
    NETF(old_origin[1], NETF_FLOAT),
    NETF(old_origin[2], NETF_FLOAT),
};

static unsigned entity_state_counts[q_countof(entity_state_fields)];

static const int entity_state_nc_bits = 32 - __builtin_clz(q_countof(entity_state_fields));

#undef NETF

static int MSG_CountDeltaMaxBits(const netfield_t *f, int n)
{
    int bits = 0;

    for (int i = 0; i < n; i++, f++) {
        bits++;
        switch (f->bits) {
        case NETF_FLOAT:
            bits += 2 + 32;
            break;
        case NETF_LEB:
            bits += 4 * 9;
            break;
        case NETF_ANGLE:
            bits += 16;
            break;
        case NETF_COLOR:
            bits += 8;
            break;
        default:
            bits += abs(f->bits);
            break;
        }
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

static void MSG_WriteFloat(uint32_t to_v)
{
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
}

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

        switch (f->bits) {
        case NETF_FLOAT:
            MSG_WriteFloat(to_v);
            break;
        case NETF_LEB:
            MSG_WriteLeb32(to_v);
            break;
        case NETF_ANGLE:
            MSG_WriteBits(ANGLE2SHORT(LongToFloat(to_v)), -16);
            break;
        case NETF_COLOR:
            MSG_WriteBits(Q_clip_uint8(LongToFloat(to_v) * 255), 8);
            break;
        default:
            MSG_WriteBits(to_v, f->bits);
            break;
        }
    }
}

void MSG_WriteDeltaEntity(const entity_state_t *from, const entity_state_t *to, bool force)
{
    int oldorg, nc;
    bool baseline;

    if (!to) {
        Q_assert(from);
        Q_assert(from->number < ENTITYNUM_WORLD);

        MSG_WriteBits(from->number, ENTITYNUM_BITS);
        MSG_WriteBit(1);    // removed
        return;
    }

    Q_assert(to->number < ENTITYNUM_WORLD);

    baseline = false;
    if (!from) {
        from = &nullEntityState;
        baseline = true;
    }

    if (VectorCompare(to->old_origin, from->old_origin))
        oldorg = 0;
    else if (VectorCompare(to->old_origin, from->origin))
        oldorg = 1;
    else if (VectorCompare(to->old_origin, to->origin))
        oldorg = 2;
    else
        oldorg = 3;

    nc = MSG_CountDeltaFields(entity_state_fields, q_countof(entity_state_fields), from, to, entity_state_counts);
    if (!nc && !oldorg) {
        if (!force)
            return;     // nothing to send!
        MSG_WriteBits(to->number, ENTITYNUM_BITS);
        MSG_WriteBit(0);    // not removed
        MSG_WriteBit(0);    // not changed
        return;
    }

    MSG_WriteBits(to->number, ENTITYNUM_BITS);
    if (!baseline) {
        MSG_WriteBit(0);    // not removed
        MSG_WriteBit(1);    // changed
    }
    MSG_WriteBits(nc, entity_state_nc_bits);
    MSG_WriteDeltaFields(entity_state_fields, nc, from, to);

    MSG_WriteBits(oldorg, 2);
    if (oldorg == 3)
        MSG_WriteDeltaFields(entity_state_fields2, 3, from, to);
}

#define NETF(f, bits)  { #f, offsetof(player_state_t, f), bits }

static const netfield_t player_state_fields[] = {
    NETF(pm_type, 8),
    NETF(origin[0], NETF_FLOAT),
    NETF(origin[1], NETF_FLOAT),
    NETF(origin[2], NETF_FLOAT),
    NETF(velocity[0], NETF_FLOAT),
    NETF(velocity[1], NETF_FLOAT),
    NETF(velocity[2], NETF_FLOAT),
    NETF(pm_flags, 16),
    NETF(pm_time, 16),
    NETF(gravity, -16),
    NETF(delta_angles[0], -16),
    NETF(delta_angles[1], -16),
    NETF(delta_angles[2], -16),

    NETF(clientnum, ENTITYNUM_BITS),
    NETF(viewangles[0], NETF_ANGLE),
    NETF(viewangles[1], NETF_ANGLE),
    NETF(viewangles[2], NETF_ANGLE),
    NETF(viewheight, -8),
    NETF(bobtime, 8),
    NETF(gunindex, MODELINDEX_BITS),
    NETF(gunskin, 8),
    NETF(gunframe, 8),
    NETF(gunrate, 2),
    NETF(screen_blend[0], NETF_COLOR),
    NETF(screen_blend[1], NETF_COLOR),
    NETF(screen_blend[2], NETF_COLOR),
    NETF(screen_blend[3], NETF_COLOR),
    NETF(damage_blend[0], NETF_COLOR),
    NETF(damage_blend[1], NETF_COLOR),
    NETF(damage_blend[2], NETF_COLOR),
    NETF(damage_blend[3], NETF_COLOR),
    NETF(fov, 8),
    NETF(rdflags, NETF_LEB),

    NETF(fog.color[0], NETF_COLOR),
    NETF(fog.color[1], NETF_COLOR),
    NETF(fog.color[2], NETF_COLOR),
    NETF(fog.density, NETF_FLOAT),
    NETF(fog.sky_factor, NETF_FLOAT),

    NETF(heightfog.start.color[0], NETF_COLOR),
    NETF(heightfog.start.color[1], NETF_COLOR),
    NETF(heightfog.start.color[2], NETF_COLOR),
    NETF(heightfog.start.dist, NETF_FLOAT),

    NETF(heightfog.end.color[0], NETF_COLOR),
    NETF(heightfog.end.color[1], NETF_COLOR),
    NETF(heightfog.end.color[2], NETF_COLOR),
    NETF(heightfog.end.dist, NETF_FLOAT),

    NETF(heightfog.density, NETF_FLOAT),
    NETF(heightfog.falloff, NETF_FLOAT),
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
                MSG_WriteSignedLeb32(to->stats[i]);
}

void MSG_WriteDeltaAreaBits(const byte *from, const byte *to, unsigned areabytes)
{
    Q_assert(areabytes <= MAX_MAP_AREA_BYTES);
    if (areabytes == 0 || (from && !memcmp(from, to, areabytes))) {
        MSG_WriteBit(0);
    } else {
        MSG_WriteBit(1);
        MSG_WriteBits(areabytes - 1, 5);
        for (int i = 0; i < areabytes; i++)
            MSG_WriteBits(to[i], 8);
    }
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

void MSG_AlignBits(void)
{
    msg_read.bits_buf  = 0;
    msg_read.bits_left = 0;
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
        *to = *from;
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
        Q_assert_soft(bits < 32);
        v |= (uint32_t)MSG_ReadBits(8) << bits;
        bits += 8;
    }

    return v;
}

static int32_t MSG_ReadSignedLeb32(void)
{
    uint32_t v = MSG_ReadLeb32();
    return (v >> 1) ^ -(v & 1);
}

static uint64_t MSG_ReadLeb64(void)
{
    uint64_t v = 0;
    int bits = 0;

    while (MSG_ReadBit()) {
        Q_assert_soft(bits < 64);
        v |= (uint64_t)MSG_ReadBits(8) << bits;
        bits += 8;
    }

    return v;
}

static uint32_t MSG_ReadFloat(void)
{
    if (MSG_ReadBit()) {
        if (MSG_ReadBit())
            return FloatToLong(MSG_ReadBits(FLOAT_INT_BITS) - FLOAT_INT_BIAS);
        else
            return MSG_ReadBits(32);
    } else {
        return FloatToLong(0.0f);
    }
}

static void MSG_ReadDeltaFields(const netfield_t *f, int n, void *to)
{
    for (int i = 0; i < n; i++, f++) {
        uint32_t to_v;

        if (!MSG_ReadBit())
            continue;

        switch (f->bits) {
        case NETF_FLOAT:
            to_v = MSG_ReadFloat();
            SHOWNET(3, "%s:%g ", f->name, LongToFloat(to_v));
            break;
        case NETF_LEB:
            to_v = MSG_ReadLeb32();
            SHOWNET(3, to_v > 1023 ? "%s:%#x " : "%s:%d ", f->name, to_v);
            break;
        case NETF_ANGLE:
            to_v = FloatToLong(SHORT2ANGLE(MSG_ReadBits(-16)));
            SHOWNET(3, "%s:%g ", f->name, LongToFloat(to_v));
            break;
        case NETF_COLOR:
            to_v = FloatToLong(MSG_ReadBits(8) / 255.0f);
            SHOWNET(3, "%s:%g ", f->name, LongToFloat(to_v));
            break;
        default:
            to_v = MSG_ReadBits(f->bits);
            SHOWNET(3, "%s:%d ", f->name, to_v);
            break;
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
void MSG_ParseDeltaEntity(const entity_state_t *from, entity_state_t *to)
{
    Q_assert(to);
    Q_assert(to->number < ENTITYNUM_WORLD);

    int nc = MSG_ReadBits(entity_state_nc_bits);
    Q_assert_soft(nc <= q_countof(entity_state_fields));

    MSG_ReadDeltaFields(entity_state_fields, nc, to);

    switch (MSG_ReadBits(2)) {
    case 1:
        VectorCopy(from->origin, to->old_origin);
        break;
    case 2:
        VectorCopy(to->origin, to->old_origin);
        break;
    case 3:
        MSG_ReadDeltaFields(entity_state_fields2, 3, to);
        break;
    }
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
    Q_assert_soft(nc <= q_countof(player_state_fields));

    MSG_ReadDeltaFields(player_state_fields, nc, to);

    uint64_t statbits = MSG_ReadLeb64();
    if (statbits) {
        SHOWNET(3, "stats");
        for (int i = 0; i < MAX_STATS; i++) {
            if (statbits & BIT_ULL(i)) {
                to->stats[i] = MSG_ReadSignedLeb32();
                SHOWNET(3, "[%d]:%d ", i, to->stats[i]);
            }
        }
    }
}

#endif // USE_CLIENT


/*
==============================================================================

            DEBUGGING STUFF

==============================================================================
*/

#if USE_CLIENT && USE_DEBUG

#define SVC(x) [svc_##x] = "svc_" #x

static const char *const svc_names[svc_num_types] = {
    SVC(bad),
    SVC(nop),
    SVC(zpacket),
    SVC(disconnect),
    SVC(reconnect),
    SVC(stringcmd),
    SVC(configstring),
    SVC(serverdata),
    SVC(configstringstream),
    SVC(baselinestream),
    SVC(frame),
};

#undef SVC

const char *MSG_ServerCommandString(int cmd)
{
    if (cmd < 0)
        return "END OF MESSAGE";
    if (cmd < q_countof(svc_names))
        return svc_names[cmd];
    return "UNKNOWN COMMAND";
}

typedef struct {
    const char *name;
    unsigned count;
} changevec_t;

static int veccmp(const void *p1, const void *p2)
{
    const changevec_t *v1 = p1;
    const changevec_t *v2 = p2;
    if (v1->count > v2->count)
        return -1;
    if (v1->count < v2->count)
        return 1;
    return 0;
}

static void dumpvecs(changevec_t *vecs, const netfield_t *f, int n, const unsigned *counts)
{
    for (int i = 0; i < n; i++) {
        vecs[i].name  = f[i].name;
        vecs[i].count = counts[i];
    }
    qsort(vecs, n, sizeof(vecs[0]), veccmp);

    for (int i = 0; i < n; i++)
        Com_Printf("%s: %u\n", vecs[i].name, vecs[i].count);
}

static void MSG_ChangeVectors_f(void)
{
    changevec_t vecs[max(q_countof(entity_state_fields), q_countof(player_state_fields))];

    Com_Printf("\n");
    dumpvecs(vecs, entity_state_fields, q_countof(entity_state_fields), entity_state_counts);

    Com_Printf("\n");
    dumpvecs(vecs, player_state_fields, q_countof(player_state_fields), player_state_counts);
}

#endif // USE_CLIENT && USE_DEBUG

void MSG_Init(void)
{
    int bits = ENTITYNUM_BITS + 2 + entity_state_nc_bits + 2;
    bits += MSG_CountDeltaMaxBits(entity_state_fields,  q_countof(entity_state_fields ));
    bits += MSG_CountDeltaMaxBits(entity_state_fields2, q_countof(entity_state_fields2));
    msg_max_entity_bytes = (bits + 7) / 8;

    MSG_Clear();

#if USE_CLIENT && USE_DEBUG
    Cmd_AddCommand("changevectors", MSG_ChangeVectors_f);
#endif
}
