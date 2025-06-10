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
// shared.h -- included first by ALL program modules
//

#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef Q2_VM
#include "shared/bg_lib.h"
#else
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#endif

#if HAVE_CONFIG_H
#include "config.h"
#endif

#include "shared/platform.h"

#define q_countof(a)        (sizeof(a) / sizeof(a[0]))

typedef unsigned char byte;
typedef int qhandle_t;

// angle indexes
#define PITCH               0       // up / down
#define YAW                 1       // left / right
#define ROLL                2       // fall over

#define MAX_STRING_CHARS    1024    // max length of a string passed to Cmd_TokenizeString
#define MAX_STRING_TOKENS   256     // max tokens resulting from Cmd_TokenizeString
#define MAX_TOKEN_CHARS     1024    // max length of an individual token
#define MAX_NET_STRING      2048    // max length of a string used in network protocol

#define MAX_QPATH           64      // max length of a quake game pathname
#define MAX_OSPATH          256     // max length of a filesystem pathname

//
// per-level limits
//
#define MAX_CLIENTS         256     // absolute limit
#define MAX_EDICTS_OLD      1024
#define MAX_EDICTS          8192    // sent as ENTITYNUM_BITS, can't be increased
#define MAX_MODELS          4096
#define MAX_SOUNDS          2048
#define MAX_IMAGES          2048
#define MAX_LIGHTSTYLES     256
#define MAX_ITEMS           256
#define MAX_GENERAL         (MAX_CLIENTS * 2) // general config strings

#define MODELINDEX_WORLD    0
#define MODELINDEX_DUMMY    1   // dummy index for beams
#define MODELINDEX_PLAYER   255

#define ENTITYNUM_WORLD     (MAX_EDICTS - 2)
#define ENTITYNUM_NONE      (MAX_EDICTS - 1)

#define MAX_CLIENT_NAME     16

typedef enum {
    ERR_FATAL,          // exit the entire game with a popup window
    ERR_DROP,           // print to console and disconnect from game
    ERR_DISCONNECT,     // like drop, but not an error
    ERR_RECONNECT       // make server broadcast 'reconnect' message
} error_type_t;

typedef enum {
    PRINT_ALL,          // general messages
    PRINT_TALK,         // print in green color
    PRINT_DEVELOPER,    // only print when "developer 1"
    PRINT_WARNING,      // print in yellow color
    PRINT_ERROR,        // print in red color
    PRINT_NOTICE        // print in cyan color
} print_type_t;

q_printf(2, 3)
void    Com_LPrintf(print_type_t type, const char *fmt, ...);

q_cold q_noreturn q_printf(2, 3)
void    Com_Error(error_type_t code, const char *fmt, ...);

#define Com_Printf(...) Com_LPrintf(PRINT_ALL, __VA_ARGS__)
#define Com_WPrintf(...) Com_LPrintf(PRINT_WARNING, __VA_ARGS__)
#define Com_EPrintf(...) Com_LPrintf(PRINT_ERROR, __VA_ARGS__)
#define Com_NPrintf(...) Com_LPrintf(PRINT_NOTICE, __VA_ARGS__)

// an assertion that's ALWAYS enabled. `expr' may have side effects.
#define Q_assert_type(type, expr) \
    do { if (!(expr)) Com_Error(type, "%s: assertion `%s' failed", __func__, #expr); } while (0)
#define Q_assert_soft(expr) Q_assert_type(ERR_DROP, expr)
#define Q_assert(expr) Q_assert_type(ERR_FATAL, expr)

// game print flags
typedef enum {
    PRINT_LOW,          // pickup messages
    PRINT_MEDIUM,       // death messages
    PRINT_HIGH,         // critical messages
    PRINT_CHAT,         // chat messages
    PRINT_TYPEWRITER,
    PRINT_CENTER,
} print_level_t;

typedef char configstring_t[MAX_QPATH];

/*
==============================================================

MATHLIB

==============================================================
*/

typedef float vec_t;
typedef vec_t vec2_t[2];
typedef vec_t vec3_t[3];
typedef vec_t vec4_t[4];

typedef float mat4_t[16];

typedef union {
    uint32_t u32;
    uint8_t u8[4];
} color_t;

extern const vec3_t vec3_origin;

typedef struct {
    int x, y, width, height;
} vrect_t;

#ifndef M_PIf
#define M_PIf       3.14159265358979323846f
#define M_SQRT2f    1.41421356237309504880f
#define M_SQRT1_2f  0.70710678118654752440f
#endif

#define DEG2RAD(a)      ((a) * (M_PIf / 180))
#define RAD2DEG(a)      ((a) * (180 / M_PIf))

#define Q_ALIGN(x, a)   (((x) + (a) - 1) & ~((a) - 1))

#define BIT(n)          (1U << (n))
#define BIT_ULL(n)      (1ULL << (n))

#define MASK(n)         (BIT(n) - 1U)
#define MASK_ULL(n)     (BIT_ULL(n) - 1ULL)

#define SWAP(type, a, b) \
    do { type SWAP_tmp = a; a = b; b = SWAP_tmp; } while (0)

#define DotProduct(x,y)         ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2])
#define CrossProduct(v1,v2,cross) \
        ((cross)[0]=(v1)[1]*(v2)[2]-(v1)[2]*(v2)[1], \
         (cross)[1]=(v1)[2]*(v2)[0]-(v1)[0]*(v2)[2], \
         (cross)[2]=(v1)[0]*(v2)[1]-(v1)[1]*(v2)[0])
#define VectorSubtract(a,b,c) \
        ((c)[0]=(a)[0]-(b)[0], \
         (c)[1]=(a)[1]-(b)[1], \
         (c)[2]=(a)[2]-(b)[2])
#define VectorAdd(a,b,c) \
        ((c)[0]=(a)[0]+(b)[0], \
         (c)[1]=(a)[1]+(b)[1], \
         (c)[2]=(a)[2]+(b)[2])
#define VectorAdd3(a,b,c,d) \
        ((d)[0]=(a)[0]+(b)[0]+(c)[0], \
         (d)[1]=(a)[1]+(b)[1]+(c)[1], \
         (d)[2]=(a)[2]+(b)[2]+(c)[2])
#define VectorCopy(a,b)     ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2])
#define VectorClear(a)      ((a)[0]=(a)[1]=(a)[2]=0)
#define VectorNegate(a,b)   ((b)[0]=-(a)[0],(b)[1]=-(a)[1],(b)[2]=-(a)[2])
#define VectorInverse(a)    ((a)[0]=-(a)[0],(a)[1]=-(a)[1],(a)[2]=-(a)[2])
#define VectorSet(v, x, y, z)   ((v)[0]=(x),(v)[1]=(y),(v)[2]=(z))
#define VectorAvg(a,b,c) \
        ((c)[0]=((a)[0]+(b)[0])*0.5f, \
         (c)[1]=((a)[1]+(b)[1])*0.5f, \
         (c)[2]=((a)[2]+(b)[2])*0.5f)
#define VectorMA(a,b,c,d) \
        ((d)[0]=(a)[0]+(b)*(c)[0], \
         (d)[1]=(a)[1]+(b)*(c)[1], \
         (d)[2]=(a)[2]+(b)*(c)[2])
#define VectorVectorMA(a,b,c,d) \
        ((d)[0]=(a)[0]+(b)[0]*(c)[0], \
         (d)[1]=(a)[1]+(b)[1]*(c)[1], \
         (d)[2]=(a)[2]+(b)[2]*(c)[2])
#define VectorRotate(in,axis,out) \
        ((out)[0]=DotProduct(in,(axis)[0]), \
         (out)[1]=DotProduct(in,(axis)[1]), \
         (out)[2]=DotProduct(in,(axis)[2]))

#define VectorEmpty(v) ((v)[0]==0&&(v)[1]==0&&(v)[2]==0)
#define VectorCompare(v1,v2)    ((v1)[0]==(v2)[0]&&(v1)[1]==(v2)[1]&&(v1)[2]==(v2)[2])
#define VectorLength(v)     (sqrtf(DotProduct((v),(v))))
#define VectorLengthSquared(v)      (DotProduct((v),(v)))
#define VectorScale(in,scale,out) \
        ((out)[0]=(in)[0]*(scale), \
         (out)[1]=(in)[1]*(scale), \
         (out)[2]=(in)[2]*(scale))
#define VectorVectorScale(in,scale,out) \
        ((out)[0]=(in)[0]*(scale)[0], \
         (out)[1]=(in)[1]*(scale)[1], \
         (out)[2]=(in)[2]*(scale)[2])
#define DistanceSquared(v1,v2) \
        (((v1)[0]-(v2)[0])*((v1)[0]-(v2)[0])+ \
        ((v1)[1]-(v2)[1])*((v1)[1]-(v2)[1])+ \
        ((v1)[2]-(v2)[2])*((v1)[2]-(v2)[2]))
#define Distance(v1,v2) (sqrtf(DistanceSquared(v1,v2)))
#define LerpAngles(a,b,c,d) \
        ((d)[0]=LerpAngle((a)[0],(b)[0],c), \
         (d)[1]=LerpAngle((a)[1],(b)[1],c), \
         (d)[2]=LerpAngle((a)[2],(b)[2],c))
#define LerpVector(a,b,c,d) \
    ((d)[0]=(a)[0]+(c)*((b)[0]-(a)[0]), \
     (d)[1]=(a)[1]+(c)*((b)[1]-(a)[1]), \
     (d)[2]=(a)[2]+(c)*((b)[2]-(a)[2]))
#define LerpVector2(a,b,c,d,e) \
    ((e)[0]=(a)[0]*(c)+(b)[0]*(d), \
     (e)[1]=(a)[1]*(c)+(b)[1]*(d), \
     (e)[2]=(a)[2]*(c)+(b)[2]*(d))
#define PlaneDiff(v,p)   (DotProduct(v,(p)->normal)-(p)->dist)

#define Vector4Subtract(a,b,c)      ((c)[0]=(a)[0]-(b)[0],(c)[1]=(a)[1]-(b)[1],(c)[2]=(a)[2]-(b)[2],(c)[3]=(a)[3]-(b)[3])
#define Vector4Add(a,b,c)           ((c)[0]=(a)[0]+(b)[0],(c)[1]=(a)[1]+(b)[1],(c)[2]=(a)[2]+(b)[2],(c)[3]=(a)[3]+(b)[3])
#define Vector4Scale(a,s,b)         ((b)[0]=(a)[0]*(s),(b)[1]=(a)[1]*(s),(b)[2]=(a)[2]*(s),(b)[3]=(a)[3]*(s))
#define Vector4Copy(a,b)            ((b)[0]=(a)[0],(b)[1]=(a)[1],(b)[2]=(a)[2],(b)[3]=(a)[3])
#define Vector4Clear(a)             ((a)[0]=(a)[1]=(a)[2]=(a)[3]=0)
#define Vector4Negate(a,b)          ((b)[0]=-(a)[0],(b)[1]=-(a)[1],(b)[2]=-(a)[2],(b)[3]=-(a)[3])
#define Vector4Set(v, a, b, c, d)   ((v)[0]=(a),(v)[1]=(b),(v)[2]=(c),(v)[3]=(d))
#define Vector4Compare(v1,v2)       ((v1)[0]==(v2)[0]&&(v1)[1]==(v2)[1]&&(v1)[2]==(v2)[2]&&(v1)[3]==(v2)[3])
#define Vector4Unpack(v)            (v)[0],(v)[1],(v)[2],(v)[3]
#define Dot4Product(x, y)           ((x)[0]*(y)[0]+(x)[1]*(y)[1]+(x)[2]*(y)[2]+(x)[3]*(y)[3])

void AngleVectors(const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up);
vec_t VectorNormalize(vec3_t v);        // returns vector length
vec_t VectorNormalize2(const vec3_t v, vec3_t out);
void ClearBounds(vec3_t mins, vec3_t maxs);
void AddPointToBounds(const vec3_t v, vec3_t mins, vec3_t maxs);
vec_t RadiusFromBounds(const vec3_t mins, const vec3_t maxs);
void UnionBounds(const vec3_t a[2], const vec3_t b[2], vec3_t c[2]);
void SetupRotationMatrix(vec3_t matrix[3], const vec3_t dir, float degrees);
void RotatePointAroundVector(vec3_t out, const vec3_t dir, const vec3_t in, float degrees);

static inline void AnglesToAxis(const vec3_t angles, vec3_t axis[3])
{
    AngleVectors(angles, axis[0], axis[1], axis[2]);
    VectorInverse(axis[1]);
}

static inline void TransposeAxis(vec3_t axis[3])
{
    SWAP(vec_t, axis[0][1], axis[1][0]);
    SWAP(vec_t, axis[0][2], axis[2][0]);
    SWAP(vec_t, axis[1][2], axis[2][1]);
}

static inline void RotatePoint(vec3_t point, const vec3_t axis[3])
{
    vec3_t temp;
    VectorCopy(point, temp);
    VectorRotate(temp, axis, point);
}

static inline uint32_t Q_npot32(uint32_t k)
{
    if (k == 0)
        return 1;

    k--;
    k = k | (k >> 1);
    k = k | (k >> 2);
    k = k | (k >> 4);
    k = k | (k >> 8);
    k = k | (k >> 16);

    return k + 1;
}

static inline int Q_log2(uint32_t k)
{
#if q_has_builtin(__builtin_clz)
    return 31 - __builtin_clz(k | 1);
#elif (defined _MSC_VER)
    unsigned long index;
    _BitScanReverse(&index, k | 1);
    return index;
#else
    for (int i = 31; i > 0; i--)
        if (k & BIT(i))
            return i;
    return 0;
#endif
}

static inline float LerpAngle(float a2, float a1, float frac)
{
    if (a1 - a2 > 180)
        a1 -= 360;
    if (a1 - a2 < -180)
        a1 += 360;
    return a2 + frac * (a1 - a2);
}

static inline float anglemod(float a)
{
    a = (360.0f / 65536) * ((int)(a * (65536 / 360.0f)) & 65535);
    return a;
}

static inline int Q_align_down(int value, int align)
{
    int mod = value % align;
    return value - mod;
}

static inline int Q_align_up(int value, int align)
{
    int mod = value % align;
    return mod ? value + align - mod : value;
}

static inline int Q_gcd(int a, int b)
{
    while (b != 0) {
        int t = b;
        b = a % b;
        a = t;
    }
    return a;
}

void Q_srand(uint32_t seed);
uint32_t Q_rand(void);
uint32_t Q_rand_uniform(uint32_t n);

static inline int Q_clip(int a, int b, int c)
{
    if (a < b)
        return b;
    if (a > c)
        return c;
    return a;
}

static inline float Q_clipf(float a, float b, float c)
{
#if defined(__GNUC__) && defined(__SSE__)
    __asm__("maxss %1, %0 \n\t"
            "minss %2, %0 \n\t"
            : "+&x"(a) : "xm"(b), "xm"(c));
    return a;
#else
    if (a < b)
        return b;
    if (a > c)
        return c;
    return a;
#endif
}

static inline float Q_circ_clipf(float a, float b, float c)
{
    return b > c ? Q_clipf(a, c, b) : Q_clipf(a, b, c);
}

static inline int8_t Q_clip_int8(int a)
{
    return ((a + 0x80U) & ~0xFF) ? (a >> 31) ^ 0x7F : a;
}

static inline int16_t Q_clip_int16(int a)
{
    return ((a + 0x8000U) & ~0xFFFF) ? (a >> 31) ^ 0x7FFF : a;
}

static inline int32_t Q_clip_int32(int64_t a)
{
    return ((a + 0x80000000ULL) & ~0xFFFFFFFFULL) ? (a >> 63) ^ 0x7FFFFFFF : a;
}

#ifdef _LP64
#define Q_clipl_int32(a)    Q_clip_int32(a)
#else
#define Q_clipl_int32(a)    (a)
#endif

static inline uint8_t Q_clip_uint8(int a)
{
    return (a & ~0xFF) ? ~a >> 31 : a;
}

static inline uint16_t Q_clip_uint16(int a)
{
    return (a & ~0xFFFF) ? ~a >> 31 : a;
}

#ifndef max
#define max(a,b) ((a)>(b)?(a):(b))
#endif

#ifndef min
#define min(a,b) ((a)<(b)?(a):(b))
#endif

#define frand()     ((int32_t)Q_rand() * 0x1p-32f + 0.5f)
#define crand()     ((int32_t)Q_rand() * 0x1p-31f)

#define Q_rint(x)   ((x) < 0 ? ((int)((x) - 0.5f)) : ((int)((x) + 0.5f)))

#define Q_IsBitSet(data, bit)   ((((const byte *)(data))[(bit) >> 3] >> ((bit) & 7)) & 1)
#define Q_SetBit(data, bit)     (((byte *)(data))[(bit) >> 3] |= (1 << ((bit) & 7)))
#define Q_ClearBit(data, bit)   (((byte *)(data))[(bit) >> 3] &= ~(1 << ((bit) & 7)))

//=============================================

// fast "C" macros
#define Q_isupper(c)    ((c) >= 'A' && (c) <= 'Z')
#define Q_islower(c)    ((c) >= 'a' && (c) <= 'z')
#define Q_isdigit(c)    ((c) >= '0' && (c) <= '9')
#define Q_isalpha(c)    (Q_isupper(c) || Q_islower(c))
#define Q_isalnum(c)    (Q_isalpha(c) || Q_isdigit(c))
#define Q_isprint(c)    ((c) >= 32 && (c) < 127)
#define Q_isgraph(c)    ((c) > 32 && (c) < 127)
#define Q_isspace(c)    (c == ' ' || c == '\f' || c == '\n' || \
                         c == '\r' || c == '\t' || c == '\v')

// tests if specified character is valid quake path character
#define Q_ispath(c)     (Q_isalnum(c) || (c) == '_' || (c) == '-')

// tests if specified character has special meaning to quake console
#define Q_isspecial(c)  ((c) == '\r' || (c) == '\n' || (c) == 127)

static inline int Q_tolower(int c)
{
    if (Q_isupper(c)) {
        c += ('a' - 'A');
    }
    return c;
}

static inline int Q_toupper(int c)
{
    if (Q_islower(c)) {
        c -= ('a' - 'A');
    }
    return c;
}

static inline char *Q_strlwr(char *s)
{
    char *p = s;

    while (*p) {
        *p = Q_tolower(*p);
        p++;
    }

    return s;
}

static inline char *Q_strupr(char *s)
{
    char *p = s;

    while (*p) {
        *p = Q_toupper(*p);
        p++;
    }

    return s;
}

static inline int Q_charhex(int c)
{
    if (c >= 'A' && c <= 'F') {
        return 10 + (c - 'A');
    }
    if (c >= 'a' && c <= 'f') {
        return 10 + (c - 'a');
    }
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    return -1;
}

// converts quake char to ASCII equivalent
static inline int Q_charascii(int c)
{
    if (c == ' ' || c == '\r' || c == '\n') {
        // white-space chars are output as-is
        return c;
    }
    c &= 127; // strip high bits
    if (Q_isprint(c)) {
        return c;
    }
    switch (c) {
        // handle bold brackets
        case 16: return '[';
        case 17: return ']';
    }
    return '.'; // don't output control chars, etc
}

// portable case insensitive compare
int Q_strcasecmp(const char *s1, const char *s2);
int Q_strncasecmp(const char *s1, const char *s2, size_t n);
char *Q_strcasestr(const char *s1, const char *s2);

#define Q_stricmp   Q_strcasecmp
#define Q_stricmpn  Q_strncasecmp
#define Q_stristr   Q_strcasestr

#ifdef HAVE_STRCHRNUL
#define Q_strchrnul strchrnul
#else
char *Q_strchrnul(const char *s, int c);
#endif

#ifdef HAVE_MEMCCPY
#define Q_memccpy memccpy
#else
void *Q_memccpy(void *dst, const void *src, int c, size_t size);
#endif

#ifdef HAVE_STRNLEN
#define Q_strnlen strnlen
#else
size_t Q_strnlen(const char *s, size_t maxlen);
#endif

#ifdef _WIN32
#define Q_atoi(s) atoi(s)
#else
int Q_atoi(const char *s);
#endif

#define Q_atof(s) strtof(s, NULL)

char *COM_SkipPath(const char *pathname);
size_t COM_StripExtension(char *out, const char *in, size_t size);
size_t COM_DefaultExtension(char *path, const char *ext, size_t size);
char *COM_FileExtension(const char *in);
void COM_SplitPath(const char *in, char *name, size_t name_size,
                   char *path, size_t path_size, bool strip_ext);

#define COM_CompareExtension(in, ext) \
    Q_strcasecmp(COM_FileExtension(in), ext)

bool COM_IsFloat(const char *s);
bool COM_IsUint(const char *s);
bool COM_IsPath(const char *s);
bool COM_IsWhite(const char *s);

extern unsigned com_linenum;

#define COM_SkipToken(data_p) COM_ParseToken(data_p, NULL, 0)
size_t COM_ParseToken(const char **data_p, char *buffer, size_t size);
char *COM_Parse(const char **data_p);
// data is an in/out param, returns a parsed out token
size_t COM_Compress(char *data);

extern const char com_hexchars[16];

size_t COM_EscapeString(char *dst, const char *src, size_t size);
char *COM_MakePrintable(const char *s);

typedef enum {
    COLOR_BLACK,
    COLOR_RED,
    COLOR_GREEN,
    COLOR_YELLOW,
    COLOR_BLUE,
    COLOR_CYAN,
    COLOR_MAGENTA,
    COLOR_WHITE,

    COLOR_ALT,
    COLOR_NONE,

    COLOR_COUNT
} color_index_t;

extern const char *const colorNames[COLOR_COUNT];
extern const uint32_t    colorTable[8];

color_index_t COM_ParseColorIndex(const char *s);
bool COM_ParseColor(const char *s, color_t *color);

int SortStrcmp(const void *p1, const void *p2);
int SortStricmp(const void *p1, const void *p2);

size_t COM_strclr(char *s);
char *COM_StripQuotes(char *s);
char *COM_TrimSpace(char *s);

// buffer safe operations
size_t Q_strlcpy(char *dst, const char *src, size_t size);
size_t Q_strlcat(char *dst, const char *src, size_t size);

#define Q_concat(dest, size, ...) \
    Q_concat_array(dest, size, (const char *[]){__VA_ARGS__, NULL})
size_t Q_concat_array(char *dest, size_t size, const char **arr);

size_t Q_vsnprintf(char *dest, size_t size, const char *fmt, va_list argptr);
size_t Q_vscnprintf(char *dest, size_t size, const char *fmt, va_list argptr);
size_t Q_snprintf(char *dest, size_t size, const char *fmt, ...) q_printf(3, 4);
size_t Q_scnprintf(char *dest, size_t size, const char *fmt, ...) q_printf(3, 4);

char    *va(const char *format, ...) q_printf(1, 2);
char    *vtos(const vec3_t v);

//=============================================

static inline uint16_t ShortSwap(uint16_t s)
{
#if q_has_builtin(__builtin_bswap16)
    return __builtin_bswap16(s);
#else
    s = (s >> 8) | (s << 8);
    return s;
#endif
}

static inline uint32_t LongSwap(uint32_t l)
{
#if q_has_builtin(__builtin_bswap32)
    return __builtin_bswap32(l);
#else
    l = ((l >> 8) & 0x00ff00ff) | ((l << 8) & 0xff00ff00);
    l = (l >> 16) | (l << 16);
    return l;
#endif
}

static inline float FloatSwap(float f)
{
    union {
        float f;
        uint32_t l;
    } dat1, dat2;

    dat1.f = f;
    dat2.l = LongSwap(dat1.l);
    return dat2.f;
}

static inline float LongToFloat(uint32_t l)
{
    union {
        float f;
        uint32_t l;
    } dat;

    dat.l = l;
    return dat.f;
}

static inline uint32_t FloatToLong(float f)
{
    union {
        float f;
        uint32_t l;
    } dat;

    dat.f = f;
    return dat.l;
}

static inline int32_t SignExtend(uint32_t v, int bits)
{
    return (int32_t)(v << (32 - bits)) >> (32 - bits);
}

static inline int64_t SignExtend64(uint64_t v, int bits)
{
    return (int64_t)(v << (64 - bits)) >> (64 - bits);
}

#if USE_LITTLE_ENDIAN
#define BigShort(x)     ShortSwap(x)
#define BigLong(x)      LongSwap(x)
#define BigFloat(x)     FloatSwap(x)
#define LittleShort(x)  ((uint16_t)(x))
#define LittleLong(x)   ((uint32_t)(x))
#define LittleFloat(x)  ((float)(x))
#define MakeRawLong(b1,b2,b3,b4) MakeLittleLong(b1,b2,b3,b4)
#define MakeRawShort(b1,b2) (((b2)<<8)|(b1))
#elif USE_BIG_ENDIAN
#define BigShort(x)     ((uint16_t)(x))
#define BigLong(x)      ((uint32_t)(x))
#define BigFloat(x)     ((float)(x))
#define LittleShort(x)  ShortSwap(x)
#define LittleLong(x)   LongSwap(x)
#define LittleFloat(x)  FloatSwap(x)
#define MakeRawLong(b1,b2,b3,b4) MakeBigLong(b1,b2,b3,b4)
#define MakeRawShort(b1,b2) (((b1)<<8)|(b2))
#else
#error Unknown byte order
#endif

#define MakeLittleShort(b1,b2) (((uint32_t)(b2)<<8)|(uint32_t)(b1))
#define MakeLittleLong(b1,b2,b3,b4) (((uint32_t)(b4)<<24)|((uint32_t)(b3)<<16)|((uint32_t)(b2)<<8)|(uint32_t)(b1))

#define MakeBigShort(b1,b2) (((uint32_t)(b1)<<8)|(uint32_t)(b2))
#define MakeBigLong(b1,b2,b3,b4) (((uint32_t)(b1)<<24)|((uint32_t)(b2)<<16)|((uint32_t)(b3)<<8)|(uint32_t)(b4))

#define LittleVector(a,b) \
    ((b)[0]=LittleFloat((a)[0]),\
     (b)[1]=LittleFloat((a)[1]),\
     (b)[2]=LittleFloat((a)[2]))

#if USE_BGRA
#define MakeColor(r, g, b, a)   MakeRawLong(b, g, r, a)
#else
#define MakeColor(r, g, b, a)   MakeRawLong(r, g, b, a)
#endif

#define U32_BLACK   MakeColor(  0,   0,   0, 255)
#define U32_RED     MakeColor(255,   0,   0, 255)
#define U32_GREEN   MakeColor(  0, 255,   0, 255)
#define U32_YELLOW  MakeColor(255, 255,   0, 255)
#define U32_BLUE    MakeColor(  0,   0, 255, 255)
#define U32_CYAN    MakeColor(  0, 255, 255, 255)
#define U32_MAGENTA MakeColor(255,   0, 255, 255)
#define U32_WHITE   MakeColor(255, 255, 255, 255)

//=============================================

//
// key / value info strings
//
#define MAX_INFO_KEY        64
#define MAX_INFO_VALUE      64
#define MAX_INFO_STRING     512

char    *Info_ValueForKey(const char *s, const char *key);
void    Info_RemoveKey(char *s, const char *key);
bool    Info_SetValueForKey(char *s, const char *key, const char *value);
bool    Info_Validate(const char *s);
size_t  Info_SubValidate(const char *s);
void    Info_NextPair(const char **string, char *key, char *value);
void    Info_Print(const char *infostring);

/*
==========================================================

CVARS (console variables)

==========================================================
*/

#define CVAR_ARCHIVE    BIT(0)  // set to cause it to be saved to vars.rc
#define CVAR_USERINFO   BIT(1)  // added to userinfo when changed
#define CVAR_SERVERINFO BIT(2)  // added to serverinfo when changed
#define CVAR_NOSET      BIT(3)  // don't allow change from console at all,
                                // but can be set from the command line
#define CVAR_LATCH      BIT(4)  // save changes until server restart

typedef struct {
    int integer;
    float value;
    bool modified;
    char string[MAX_QPATH];     // for longer strings trap_Cvar_VariableString
                                // must be used
} vm_cvar_t;


/*
==========================================================

REAL TIME

==========================================================
*/

typedef struct {
    int tm_sec;
    int tm_min;
    int tm_hour;
    int tm_mday;
    int tm_mon;
    int tm_year;
    int tm_wday;
    int tm_yday;
    int tm_isdst;
} vm_time_t;

/*
==============================================================

COLLISION DETECTION

==============================================================
*/

// lower bits are stronger, and will eat weaker brushes completely
#define CONTENTS_NONE           0U
#define CONTENTS_SOLID          BIT(0)      // an eye is never valid in a solid
#define CONTENTS_WINDOW         BIT(1)      // translucent, but not watery
#define CONTENTS_AUX            BIT(2)
#define CONTENTS_LAVA           BIT(3)
#define CONTENTS_SLIME          BIT(4)
#define CONTENTS_WATER          BIT(5)
#define CONTENTS_MIST           BIT(6)

// remaining contents are non-visible, and don't eat brushes

#define CONTENTS_NO_WATERJUMP   BIT(13)     // KEX
#define CONTENTS_PROJECTILECLIP BIT(14)     // KEX
#define CONTENTS_AREAPORTAL     BIT(15)

#define CONTENTS_PLAYERCLIP     BIT(16)
#define CONTENTS_MONSTERCLIP    BIT(17)

// currents can be added to any other contents, and may be mixed
#define CONTENTS_CURRENT_0      BIT(18)
#define CONTENTS_CURRENT_90     BIT(19)
#define CONTENTS_CURRENT_180    BIT(20)
#define CONTENTS_CURRENT_270    BIT(21)
#define CONTENTS_CURRENT_UP     BIT(22)
#define CONTENTS_CURRENT_DOWN   BIT(23)

#define CONTENTS_ORIGIN         BIT(24)     // removed before bsping an entity

#define CONTENTS_MONSTER        BIT(25)     // should never be on a brush, only in game
#define CONTENTS_DEADMONSTER    BIT(26)
#define CONTENTS_DETAIL         BIT(27)     // brushes to be added after vis leafs
#define CONTENTS_TRANSLUCENT    BIT(28)     // auto set if any surface has trans
#define CONTENTS_LADDER         BIT(29)

//KEX
#define CONTENTS_PLAYER         BIT(30)     // should never be on a brush, only in game
#define CONTENTS_PROJECTILE     BIT(31)
//KEX

#define SURF_LIGHT              BIT(0)      // value will hold the light strength
#define SURF_SLICK              BIT(1)      // effects game physics
#define SURF_SKY                BIT(2)      // don't draw, but add to skybox
#define SURF_WARP               BIT(3)      // turbulent water warp
#define SURF_TRANS33            BIT(4)
#define SURF_TRANS66            BIT(5)
#define SURF_FLOWING            BIT(6)      // scroll towards angle
#define SURF_NODRAW             BIT(7)      // don't bother referencing the texture

#define SURF_ALPHATEST          BIT(25)     // used by KMQuake2

//KEX
#define SURF_N64_UV             BIT(28)
#define SURF_N64_SCROLL_X       BIT(29)
#define SURF_N64_SCROLL_Y       BIT(30)
#define SURF_N64_SCROLL_FLIP    BIT(31)
//KEX

// content masks
#define MASK_ALL                (-1)
#define MASK_SOLID              (CONTENTS_SOLID|CONTENTS_WINDOW)
#define MASK_PLAYERSOLID        (CONTENTS_SOLID|CONTENTS_PLAYERCLIP|CONTENTS_WINDOW|CONTENTS_MONSTER|CONTENTS_PLAYER)
#define MASK_DEADSOLID          (CONTENTS_SOLID|CONTENTS_PLAYERCLIP|CONTENTS_WINDOW)
#define MASK_MONSTERSOLID       (CONTENTS_SOLID|CONTENTS_MONSTERCLIP|CONTENTS_WINDOW|CONTENTS_MONSTER|CONTENTS_PLAYER)
#define MASK_WATER              (CONTENTS_WATER|CONTENTS_LAVA|CONTENTS_SLIME)
#define MASK_OPAQUE             (CONTENTS_SOLID|CONTENTS_SLIME|CONTENTS_LAVA)
#define MASK_SHOT               (CONTENTS_SOLID|CONTENTS_MONSTER|CONTENTS_PLAYER|CONTENTS_WINDOW|CONTENTS_DEADMONSTER)
#define MASK_CURRENT            (CONTENTS_CURRENT_0|CONTENTS_CURRENT_90|CONTENTS_CURRENT_180|CONTENTS_CURRENT_270|CONTENTS_CURRENT_UP|CONTENTS_CURRENT_DOWN)
#define MASK_PROJECTILE         (MASK_SHOT|CONTENTS_PROJECTILECLIP)

// gi.BoxEdicts() can return a list of either solid or trigger entities
// FIXME: eliminate AREA_ distinction?
#define AREA_SOLID      1
#define AREA_TRIGGERS   2

// flags for CM_InVis()
typedef enum {
    VIS_PVS     = 0,
    VIS_PHS     = 1,
    VIS_NOAREAS = 2     // can be OR'ed with one of above
} vis_t;

// plane_t structure
typedef struct {
    vec3_t  normal;
    float   dist;
    byte    type;           // for fast side tests
    byte    signbits;       // signx + (signy<<1) + (signz<<1)
    byte    pad[2];
} cplane_t;

// 0-2 are axial planes
#define PLANE_X         0
#define PLANE_Y         1
#define PLANE_Z         2
#define PLANE_NON_AXIAL 6

typedef int contents_t;

typedef struct {
    char    name[32];
    char    material[16];
    int     footstep_id;
    int     flags;
    int     value;
    int     reserved;
} surface_info_t;

// a trace is returned when a box is swept through the world
typedef struct {
    bool            allsolid;       // if true, plane is not valid
    bool            startsolid;     // if true, the initial point was in a solid area
    float           fraction;       // time completed, 1.0 = didn't hit anything
    vec3_t          endpos;         // final position
    cplane_t        plane;          // surface normal at impact
    int             surface_flags;  // surface flags
    int             surface_id;     // surface id
    contents_t      contents;       // contents on other side of surface hit
    int             entnum;         // not set by CM_*() functions
} trace_t;

// pmove_state_t is the information necessary for client side movement
// prediction
typedef enum {
    // can accelerate and turn
    PM_NORMAL,
    PM_GRAPPLE, // [Paril-KEX] pull towards velocity, no gravity
    PM_NOCLIP,
    PM_SPECTATOR,
    // no acceleration or turning
    PM_DEAD,
    PM_GIB,     // different bounding box
    PM_FREEZE
} pmtype_t;

// pmove->pm_flags
#define PMF_NONE            0U
#define PMF_DUCKED          BIT(0)
#define PMF_JUMP_HELD       BIT(1)
#define PMF_ON_GROUND       BIT(2)
#define PMF_TIME_WATERJUMP  BIT(3)      // pm_time is waterjump
#define PMF_TIME_LAND       BIT(4)      // pm_time is time before rejump
#define PMF_TIME_TELEPORT   BIT(5)      // pm_time is non-moving time
#define PMF_NO_PREDICTION   BIT(6)      // temporarily disables prediction (used for grappling hook)

//KEX
#define PMF_ON_LADDER                   BIT(7)
#define PMF_NO_ANGULAR_PREDICTION       BIT(8)
#define PMF_IGNORE_PLAYER_COLLISION     BIT(9)
#define PMF_TIME_TRICK                  BIT(10)
#define PMF_NO_GROUND_SEEK              BIT(11)
//KEX

#define PM_TIME_SHIFT       0

// this structure needs to be communicated bit-accurate
// from the server to the client to guarantee that
// prediction stays in sync, so no floats are used.
// if any part of the game code modifies this struct, it
// will result in a prediction error of some degree.
typedef struct {
    pmtype_t    pm_type;

    vec3_t      origin;
    vec3_t      velocity;
    uint32_t    pm_flags;       // ducked, jump_held, etc
    uint32_t    pm_time;        // in msec
    int32_t     gravity;
    int32_t     delta_angles[3];    // add to command angles to get view direction
                                    // changed by spawns, rotating objects, and teleporters
} pmove_state_t;

//
// button bits
//
#define BUTTON_NONE     0U
#define BUTTON_ATTACK   BIT(0)
#define BUTTON_USE      BIT(1)
#define BUTTON_HOLSTER  BIT(2)
#define BUTTON_JUMP     BIT(3)
#define BUTTON_CROUCH   BIT(4)
#define BUTTON_ANY      BIT(7)  // any key whatsoever

// usercmd_t is sent to the server each client frame
typedef struct {
    byte    msec;
    byte    buttons;
    short   angles[3];
    short   forwardmove, sidemove, upmove;
    byte    impulse;        // remove?
    byte    lightlevel;     // light level the player is standing on
} usercmd_t;

typedef enum {
    WATER_NONE,
    WATER_FEET,
    WATER_WAIST,
    WATER_UNDER
} water_level_t;

#define MAXTOUCH    32

typedef struct {
    int num;
    trace_t traces[MAXTOUCH];
} touch_list_t;

typedef void (*trace_func_t)(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, unsigned passent, contents_t contentmask);
typedef void (*clip_func_t)(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, unsigned clipent, contents_t contentmask);

typedef struct {
    // state (in / out)
    pmove_state_t   s;

    // command (in)
    usercmd_t       cmd;
    bool            snapinitial;    // if s has been changed outside pmove

    // results (out)
    touch_list_t    touch;

    vec3_t      viewangles;         // clamped
    float       viewheight;

    vec3_t      mins, maxs;         // bounding box size

    int             groundentity;
    cplane_t        groundplane;
    contents_t      watertype;
    water_level_t   waterlevel;

    // callbacks to test the world
    trace_func_t    trace;
    clip_func_t     clip;
    int             (*pointcontents)(const vec3_t point);

    // [KEX] variables (in)
    vec3_t      viewoffset;
    int         playernum;

    // [KEX] results (out)
    vec4_t      screen_blend;
    int         rdflags;
    bool        jump_sound;
    bool        step_clip;
    float       impact_delta;
} pmove_t;

// entity_state_t->effects
// Effects are things handled on the client side (lights, particles, frame animations)
// that happen constantly on the given entity.
// An entity that has effects will be sent to the client
// even if it has a zero index model.
#define EF_NONE             0U
#define EF_ROTATE           BIT(0)      // rotate (bonus items)
#define EF_GIB              BIT(1)      // leave a trail
#define EF_BOB              BIT(2)      // used by KEX
#define EF_BLASTER          BIT(3)      // redlight + trail
#define EF_ROCKET           BIT(4)      // redlight + trail
#define EF_GRENADE          BIT(5)
#define EF_HYPERBLASTER     BIT(6)
#define EF_BFG              BIT(7)
#define EF_COLOR_SHELL      BIT(8)
#define EF_POWERSCREEN      BIT(9)
#define EF_ANIM01           BIT(10)     // automatically cycle between frames 0 and 1 at 2 hz
#define EF_ANIM23           BIT(11)     // automatically cycle between frames 2 and 3 at 2 hz
#define EF_ANIM_ALL         BIT(12)     // automatically cycle through all frames at 2hz
#define EF_ANIM_ALLFAST     BIT(13)     // automatically cycle through all frames at 10hz
#define EF_FLIES            BIT(14)
#define EF_QUAD             BIT(15)
#define EF_PENT             BIT(16)
#define EF_TELEPORTER       BIT(17)     // particle fountain
#define EF_FLAG1            BIT(18)
#define EF_FLAG2            BIT(19)

// RAFAEL
#define EF_IONRIPPER        BIT(20)
#define EF_GREENGIB         BIT(21)
#define EF_BLUEHYPERBLASTER BIT(22)
#define EF_SPINNINGLIGHTS   BIT(23)
#define EF_PLASMA           BIT(24)
#define EF_TRAP             BIT(25)

//ROGUE
#define EF_TRACKER          BIT(26)
#define EF_DOUBLE           BIT(27)
#define EF_SPHERETRANS      BIT(28)
#define EF_TAGTRAIL         BIT(29)
#define EF_HALF_DAMAGE      BIT(30)
#define EF_TRACKERTRAIL     BIT(31)
//ROGUE

// entity_state_t->morefx flags
//KEX
#define EFX_NONE                0U
#define EFX_DUALFIRE            BIT(0)
#define EFX_HOLOGRAM            BIT(1)
#define EFX_FLASHLIGHT          BIT(2)
#define EFX_BARREL_EXPLODING    BIT(3)
#define EFX_TELEPORTER2         BIT(4)
#define EFX_GRENADE_LIGHT       BIT(5)
//KEX
#define EFX_STEAM               BIT(6)

// entity_state_t->renderfx flags
#define RF_NONE             0U
#define RF_MINLIGHT         BIT(0)      // always have some light (viewmodel)
#define RF_VIEWERMODEL      BIT(1)      // don't draw through eyes, only mirrors
#define RF_WEAPONMODEL      BIT(2)      // only draw through eyes
#define RF_FULLBRIGHT       BIT(3)      // always draw full intensity
#define RF_DEPTHHACK        BIT(4)      // for view weapon Z crunching
#define RF_TRANSLUCENT      BIT(5)
#define RF_FRAMELERP        BIT(6)
#define RF_BEAM             BIT(7)
#define RF_CUSTOMSKIN       BIT(8)      // skin is an index in image_precache
#define RF_GLOW             BIT(9)      // pulse lighting for bonus items
#define RF_SHELL_RED        BIT(10)
#define RF_SHELL_GREEN      BIT(11)
#define RF_SHELL_BLUE       BIT(12)
#define RF_NOSHADOW         BIT(13)     // used by YQ2
#define RF_CASTSHADOW       BIT(14)     // used by KEX

//ROGUE
#define RF_IR_VISIBLE       BIT(15)
#define RF_SHELL_DOUBLE     BIT(16)
#define RF_SHELL_HALF_DAM   BIT(17)
#define RF_USE_DISGUISE     BIT(18)
//ROGUE

//KEX
#define RF_SHELL_LITE_GREEN BIT(19)
#define RF_CUSTOM_LIGHT     BIT(20)
#define RF_FLARE            BIT(21)
#define RF_OLD_FRAME_LERP   BIT(22)
#define RF_DOT_SHADOW       BIT(23)
#define RF_LOW_PRIORITY     BIT(24)
#define RF_NO_LOD           BIT(25)
#define RF_STAIR_STEP       BIT(26)

#define RF_NO_STEREO        RF_WEAPONMODEL
#define RF_FLARE_LOCK_ANGLE RF_MINLIGHT
#define RF_BEAM_LIGHTNING   (RF_BEAM | RF_GLOW)
//KEX
#define RF_BEAM_TEMP        (RF_BEAM | RF_MINLIGHT)

// player_state_t->refdef flags
#define RDF_NONE            0U
#define RDF_UNDERWATER      BIT(0)      // warp the screen as appropriate
#define RDF_NOWORLDMODEL    BIT(1)      // used for player configuration screen

//ROGUE
#define RDF_IRGOGGLES       BIT(2)
#define RDF_UVGOGGLES       BIT(3)
//ROGUE

#define RDF_TELEPORT_BIT    BIT(4)      // used by Q2PRO (extended servers)

//
// muzzle flashes / player effects
//
typedef enum {
    MZ_BLASTER,
    MZ_MACHINEGUN,
    MZ_SHOTGUN,
    MZ_CHAINGUN1,
    MZ_CHAINGUN2,
    MZ_CHAINGUN3,
    MZ_RAILGUN,
    MZ_ROCKET,
    MZ_GRENADE,
    MZ_LOGIN,
    MZ_LOGOUT,
    MZ_RESPAWN,
    MZ_BFG,
    MZ_SSHOTGUN,
    MZ_HYPERBLASTER,
    MZ_ITEMRESPAWN,

// RAFAEL
    MZ_IONRIPPER,
    MZ_BLUEHYPERBLASTER,
    MZ_PHALANX,

// KEX
    MZ_BFG2,
    MZ_PHALANX2,

//ROGUE
    MZ_ETF_RIFLE = 30,
    MZ_PROX,            // KEX
    MZ_ETF_RIFLE_2,     // MZ_ETF_RIFLE_2 in KEX
    MZ_HEATBEAM,
    MZ_BLASTER2,
    MZ_TRACKER,
    MZ_NUKE1,
    MZ_NUKE2,
    MZ_NUKE4,
    MZ_NUKE8,
//ROGUE

    MZ_SILENCED = BIT(7),  // bit flag ORed with one of the above numbers
    MZ_NONE = 0,
} player_muzzle_t;

// temp entity events
//
// Temp entity events are for things that happen
// at a location separate from any existing entity.
// Temporary entity messages are explicitly constructed
// and broadcast.
typedef enum {
    TE_GUNSHOT,
    TE_BLOOD,
    TE_BLASTER,
    TE_RAILTRAIL,
    TE_SHOTGUN,
    TE_EXPLOSION1,
    TE_EXPLOSION2,
    TE_ROCKET_EXPLOSION,
    TE_GRENADE_EXPLOSION,
    TE_SPARKS,
    TE_SPLASH,
    TE_BUBBLETRAIL,
    TE_SCREEN_SPARKS,
    TE_SHIELD_SPARKS,
    TE_BULLET_SPARKS,
    TE_LASER_SPARKS,
    TE_PARASITE_ATTACK,
    TE_ROCKET_EXPLOSION_WATER,
    TE_GRENADE_EXPLOSION_WATER,
    TE_MEDIC_CABLE_ATTACK,
    TE_BFG_EXPLOSION,
    TE_BFG_BIGEXPLOSION,
    TE_BOSSTPORT,           // used as '22' in a map, so DON'T RENUMBER!!!
    TE_BFG_LASER,
    TE_GRAPPLE_CABLE,
    TE_WELDING_SPARKS,
    TE_GREENBLOOD,
    TE_BLUEHYPERBLASTER,
    TE_PLASMA_EXPLOSION,
    TE_TUNNEL_SPARKS,

//ROGUE
    TE_BLASTER2,
    TE_RAILTRAIL2,
    TE_FLAME,
    TE_LIGHTNING,
    TE_DEBUGTRAIL,
    TE_PLAIN_EXPLOSION,
    TE_FLASHLIGHT,
    TE_FORCEWALL,
    TE_HEATBEAM,
    TE_MONSTER_HEATBEAM,
    TE_STEAM,
    TE_BUBBLETRAIL2,
    TE_MOREBLOOD,
    TE_HEATBEAM_SPARKS,
    TE_HEATBEAM_STEAM,
    TE_CHAINFIST_SMOKE,
    TE_ELECTRIC_SPARKS,
    TE_TRACKER_EXPLOSION,
    TE_TELEPORT_EFFECT,
    TE_DBALL_GOAL,
    TE_WIDOWBEAMOUT,
    TE_NUKEBLAST,
    TE_WIDOWSPLASH,
    TE_EXPLOSION1_BIG,
    TE_EXPLOSION1_NP,
    TE_FLECHETTE,
//ROGUE

//[Paril-KEX]
    TE_BLUEHYPERBLASTER_2,
    TE_BFG_ZAP,
    TE_BERSERK_SLAM,
    TE_GRAPPLE_CABLE_2,
    TE_POWER_SPLASH,
    TE_LIGHTNING_BEAM,
    TE_EXPLOSION1_NL,
    TE_EXPLOSION2_NL,
//[Paril-KEX]

    TE_DAMAGE_DEALT = 128,

    TE_NUM_ENTITIES
} temp_event_t;

typedef enum {
    SPLASH_UNKNOWN,
    SPLASH_SPARKS,
    SPLASH_BLUE_WATER,
    SPLASH_BROWN_WATER,
    SPLASH_SLIME,
    SPLASH_LAVA,
    SPLASH_BLOOD,
    SPLASH_ELECTRIC_N64, // KEX
} splash_color_t;

// sound channels
// channel 0 never willingly overrides
// other channels (1-7) always override a playing sound on that channel
typedef enum {
    CHAN_AUTO,
    CHAN_WEAPON,
    CHAN_VOICE,
    CHAN_ITEM,
    CHAN_BODY,
    CHAN_AUX,

    // modifier flags
    CHAN_NO_PHS_ADD     = BIT(3),   // send to all clients, not just ones in PHS (ATTN 0 will also do this)
    CHAN_RELIABLE       = BIT(4),   // send by reliable message, not datagram
} soundchan_t;

// hack to encode ATTN_STATIC more efficiently
#define ATTN_ESCAPE_CODE    (ATTN_STATIC * 64)

// sound attenuation values
#define ATTN_NONE               0   // full volume the entire level
#define ATTN_NORM               1
#define ATTN_IDLE               2
#define ATTN_STATIC             3   // diminish very rapidly with distance

// player_state->stats[] indexes
enum {
    STAT_HEALTH_ICON,
    STAT_HEALTH,
    STAT_AMMO_ICON,
    STAT_AMMO,
    STAT_ARMOR_ICON,
    STAT_ARMOR,
    STAT_SELECTED_ICON,
    STAT_PICKUP_ICON,
    STAT_PICKUP_STRING,
    STAT_TIMER_ICON,
    STAT_TIMER,
    STAT_HELPICON,
    STAT_SELECTED_ITEM,
    STAT_LAYOUTS,
    STAT_FRAGS,
    STAT_FLASHES,           // cleared each frame, 1 = health, 2 = armor
    STAT_CHASE,
    STAT_SPECTATOR,
    STAT_HITS = 24
};

#define MAX_STATS   64

// STAT_LAYOUTS flags
#define LAYOUTS_LAYOUT          BIT(0)
#define LAYOUTS_INVENTORY       BIT(1)
#define LAYOUTS_HIDE_HUD        BIT(2)
#define LAYOUTS_INTERMISSION    BIT(3)
#define LAYOUTS_HELP            BIT(4)
#define LAYOUTS_HIDE_CROSSHAIR  BIT(5)

// dmflags->value flags
#define DF_NO_HEALTH        BIT(0)
#define DF_NO_ITEMS         BIT(1)
#define DF_WEAPONS_STAY     BIT(2)
#define DF_NO_FALLING       BIT(3)
#define DF_INSTANT_ITEMS    BIT(4)
#define DF_SAME_LEVEL       BIT(5)
#define DF_SKINTEAMS        BIT(6)
#define DF_MODELTEAMS       BIT(7)
#define DF_NO_FRIENDLY_FIRE BIT(8)
#define DF_SPAWN_FARTHEST   BIT(9)
#define DF_FORCE_RESPAWN    BIT(10)
#define DF_NO_ARMOR         BIT(11)
#define DF_ALLOW_EXIT       BIT(12)
#define DF_INFINITE_AMMO    BIT(13)
#define DF_QUAD_DROP        BIT(14)
#define DF_FIXED_FOV        BIT(15)

// RAFAEL
#define DF_QUADFIRE_DROP    BIT(16)

//ROGUE
#define DF_NO_MINES         BIT(17)
#define DF_NO_STACK_DOUBLE  BIT(18)
#define DF_NO_NUKES         BIT(19)
#define DF_NO_SPHERES       BIT(20)
//ROGUE

#define UF_AUTOSCREENSHOT   BIT(0)
#define UF_AUTORECORD       BIT(1)
#define UF_LOCALFOV         BIT(2)
#define UF_MUTE_PLAYERS     BIT(3)
#define UF_MUTE_OBSERVERS   BIT(4)
#define UF_MUTE_MISC        BIT(5)
#define UF_PLAYERFOV        BIT(6)

/*
==========================================================

  ELEMENTS COMMUNICATED ACROSS THE NET

==========================================================
*/

// default server FPS
#define BASE_FRAMERATE          10
#define BASE_FRAMETIME          100
#define BASE_1_FRAMETIME        0.01f   // 1/BASE_FRAMETIME
#define BASE_FRAMETIME_1000     0.1f    // BASE_FRAMETIME/1000

// maximum variable FPS factor
#define MAX_FRAMEDIV    6

#define ANGLE2SHORT(x)  ((int)((x)*65536/360) & 65535)
#define SHORT2ANGLE(x)  ((x)*(360.0f/65536))

#define COORD2SHORT(x)  ((int)((x)*8.0f))
#define SHORT2COORD(x)  ((x)*(1.0f/8))

#define MAX_MAP_AREA_BYTES      32
#define MAX_PACKET_ENTITIES     512

//
// config strings are a general means of communication from
// the server to all connected clients.
// Each config string can be at most MAX_QPATH characters.
//
#define CS_NAME             0
#define CS_CDTRACK          1
#define CS_SKY              2
#define CS_SKYAXIS          3       // %f %f %f format
#define CS_SKYROTATE        4
#define CS_STATUSBAR        5       // display program string

#define CS_AIRACCEL         59
#define CS_MAXCLIENTS       60
#define CS_MAPCHECKSUM      61
#define CS_MODELS           62
#define CS_SOUNDS           (CS_MODELS + MAX_MODELS)
#define CS_IMAGES           (CS_SOUNDS + MAX_SOUNDS)
#define CS_LIGHTS           (CS_IMAGES + MAX_IMAGES)
#define CS_ITEMS            (CS_LIGHTS + MAX_LIGHTSTYLES)
#define CS_PLAYERSKINS      (CS_ITEMS + MAX_ITEMS)
#define CS_GENERAL          (CS_PLAYERSKINS + MAX_CLIENTS)
#define MAX_CONFIGSTRINGS   (CS_GENERAL + MAX_GENERAL)

//==============================================

// entity_state_t->event values
// entity events are for effects that take place relative
// to an existing entities origin.  Very network efficient.
// All muzzle flashes really should be converted to events...
typedef enum {
    EV_NONE,
    EV_ITEM_RESPAWN,
    EV_FOOTSTEP,
    EV_FALLSHORT,
    EV_FALL,
    EV_FALLFAR,
    EV_DEATH1,
    EV_DEATH2,
    EV_DEATH3,
    EV_DEATH4,
    EV_PAIN,
    EV_GURP,
    EV_DROWN,
    EV_JUMP,
    EV_PLAYER_TELEPORT,
    EV_OTHER_TELEPORT,
    EV_OTHER_FOOTSTEP,
    EV_LADDER_STEP,
    EV_MUZZLEFLASH,
    EV_MUZZLEFLASH2,
    EV_SOUND,
    EV_BERSERK_SLAM,
    EV_GUNCMDR_SLAM,
    EV_RAILTRAIL,
    EV_RAILTRAIL2,
    EV_BUBBLETRAIL,
    EV_BUBBLETRAIL2,
    EV_BFG_LASER,
    EV_BFG_ZAP,

    EV_SPLASH_UNKNOWN,
    EV_SPLASH_SPARKS,
    EV_SPLASH_BLUE_WATER,
    EV_SPLASH_BROWN_WATER,
    EV_SPLASH_SLIME,
    EV_SPLASH_LAVA,
    EV_SPLASH_BLOOD,
    EV_SPLASH_ELECTRIC_N64,

    EV_BLOOD,
    EV_MORE_BLOOD,
    EV_GREEN_BLOOD,
    EV_GUNSHOT,
    EV_SHOTGUN,
    EV_SPARKS,
    EV_BULLET_SPARKS,
    EV_HEATBEAM_SPARKS,
    EV_HEATBEAM_STEAM,
    EV_SCREEN_SPARKS,
    EV_SHIELD_SPARKS,
    EV_ELECTRIC_SPARKS,
    EV_LASER_SPARKS,
    EV_WELDING_SPARKS,
    EV_TUNNEL_SPARKS,

    EV_EXPLOSION_PLAIN,
    EV_EXPLOSION1,
    EV_EXPLOSION1_NL,
    EV_EXPLOSION1_NP,
    EV_EXPLOSION1_BIG,
    EV_EXPLOSION2,
    EV_EXPLOSION2_NL,
    EV_BLASTER,
    EV_BLASTER2,
    EV_FLECHETTE,
    EV_BLUEHYPERBLASTER,
    EV_GRENADE_EXPLOSION,
    EV_GRENADE_EXPLOSION_WATER,
    EV_ROCKET_EXPLOSION,
    EV_ROCKET_EXPLOSION_WATER,
    EV_BFG_EXPLOSION,
    EV_BFG_EXPLOSION_BIG,
    EV_TRACKER_EXPLOSION,

    EV_POWER_SPLASH,
    EV_BOSSTPORT,
    EV_TELEPORT_EFFECT,
    EV_CHAINFIST_SMOKE,
    EV_NUKEBLAST,
    EV_WIDOWBEAMOUT,
    EV_WIDOWSPLASH,
} entity_event_t;

// entity_state_t is the information conveyed from the server
// in an update message about entities that the client will
// need to render in some way
typedef struct {
    uint32_t    number;         // edict index

    vec3_t      origin;
    vec3_t      angles;
    vec3_t      old_origin;     // for lerping
    uint32_t    modelindex;
    uint32_t    modelindex2, modelindex3, modelindex4;  // weapons, CTF flags, etc
    uint32_t    frame;
    uint32_t    skinnum;
    uint32_t    effects;
    uint32_t    renderfx;
    uint32_t    solid;      // for client side prediction,
                            // gi.linkentity sets this properly
    uint32_t    sound;      // for looping sounds, to guarantee shutoff
    uint32_t    event[2];   // impulse events -- muzzle flashes, footsteps, etc
                            // events only go out for a single frame, they
                            // are automatically cleared each frame
    uint32_t    event_param[2];
    uint32_t    morefx;
    float       alpha;
    float       scale;
    uint32_t    othernum;
} entity_state_t;

//==============================================

// player_state_t is the information needed in addition to pmove_state_t
// to rendered a view.  There will only be 10 player_state_t sent each second,
// but the number of pmove_state_t changes will be relative to client
// frame rates

typedef struct {
    vec3_t color;
    float density;
    float sky_factor;
} player_fog_t;

typedef struct {
    struct {
        vec3_t color;
        float dist;
    } start, end;
    float density;
    float falloff;
} player_heightfog_t;

typedef struct {
    pmove_state_t   pmove;  // for prediction

    // these fields do not need to be communicated bit-precise
    int         clientnum;      // current POV number

    vec3_t      viewangles;     // for fixed views
    vec3_t      viewoffset;     // add to pmovestate->origin
    vec3_t      kick_angles;    // add to view direction to get render angles
                                // set by weapon kicks, pain effects, etc

    vec3_t      gunangles;
    vec3_t      gunoffset;
    int         gunindex;
    int         gunframe;

    vec4_t      screen_blend;       // rgba full screen effect
    vec4_t      damage_blend;

    player_fog_t        fog;
    player_heightfog_t  heightfog;

    int         fov;            // horizontal field of view

    int         rdflags;        // refdef flags

    int16_t     stats[MAX_STATS];   // fast status bar updates
} player_state_t;

//==============================================

#define ENTITYNUM_BITS      13
#define ENTITYNUM_MASK      MASK(ENTITYNUM_BITS)

#define GUNINDEX_BITS       13  // upper 3 bits are skinnum
#define GUNINDEX_MASK       MASK(GUNINDEX_BITS)
