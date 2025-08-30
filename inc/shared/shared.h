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
#include "bgame/bg_lib.h"
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

#define CONST_STR_LEN(x)    x, sizeof("" x) - 1

#define STRINGIFY2(x)   #x
#define STRINGIFY(x)    STRINGIFY2(x)

typedef unsigned char byte;
typedef unsigned int qhandle_t;

// angle indexes
#define PITCH               0       // up / down
#define YAW                 1       // left / right
#define ROLL                2       // fall over

#define MAX_STRING_CHARS    1024    // max length of a string passed to Cmd_TokenizeString
#define MAX_STRING_TOKENS   256     // max tokens resulting from Cmd_TokenizeString
#define MAX_TOKEN_CHARS     1024    // max length of an individual token
#define MAX_NET_STRING      4096    // max length of a string used in network protocol

#define MAX_QPATH           64      // max length of a quake game pathname
#define MAX_OSPATH          256     // max length of a filesystem pathname

//
// per-level limits
//
#define ENTITYNUM_BITS      13
#define MODELINDEX_BITS     12

#define MAX_CLIENTS         256     // absolute limit
#define MAX_EDICTS_OLD      1024
#define MAX_EDICTS          8192    // sent as ENTITYNUM_BITS
#define MAX_MODELS          4096    // sent as MODELINDEX_BITS
#define MAX_SOUNDS          2048
#define MAX_IMAGES          2048
#define MAX_LIGHTSTYLES     256

#define MODELINDEX_WORLD    0
#define MODELINDEX_DUMMY    1   // dummy index for beams
#define MODELINDEX_PLAYER   (MAX_MODELS - 1)

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
    PRINT_NOTICE,       // print in cyan color
    PRINT_SKIPNOTIFY = 16
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
#define VectorInit(v)       { (v)[0], (v)[1], (v)[2] }
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

#define Vector2Subtract(a,b,c)  ((c)[0]=(a)[0]-(b)[0],(c)[1]=(a)[1]-(b)[1])
#define Dot2Product(x,y)        ((x)[0]*(y)[0]+(x)[1]*(y)[1])
#define Vector2Length(v)        (sqrtf(Dot2Product((v),(v))))
#define Distance2Squared(v1,v2) \
    (((v1)[0]-(v2)[0])*((v1)[0]-(v2)[0])+ \
     ((v1)[1]-(v2)[1])*((v1)[1]-(v2)[1]))

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
void MakeNormalVectors(const vec3_t forward, vec3_t right, vec3_t up);
float V_CalcFov(float fov_x, float width, float height);

#define NUMVERTEXNORMALS    162

extern const vec3_t bytedirs[NUMVERTEXNORMALS];

#define DIRTOBYTE_NONE  0
#define DIRTOBYTE_UP    6   // DirToByte({0, 0, 1})

int DirToByte(const vec3_t dir);
void ByteToDir(unsigned index, vec3_t dir);

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

static inline void AxisClear(vec3_t axis[3])
{
    VectorSet(axis[0], 1, 0, 0);
    VectorSet(axis[1], 0, 1, 0);
    VectorSet(axis[2], 0, 0, 1);
}

static inline void RotatePoint(vec3_t point, const vec3_t axis[3])
{
    vec3_t temp = VectorInit(point);
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
    return 31 - __builtin_clz(k | 1);
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

#if LONG_MAX > INT32_MAX
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

static inline int Q_strcmp_null(const char *s1, const char *s2)
{
    if (!s1)
        s1 = "";
    if (!s2)
        s2 = "";
    return strcmp(s1, s2);
}

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

static inline size_t Q_strlcpy_null(char *dst, const char *src, size_t size)
{
    if (!src)
        src = "";
    return Q_strlcpy(dst, src, size);
}

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

#define CONCHAR_WIDTH   8
#define CONCHAR_HEIGHT  8

#define UI_LEFT             BIT(0)
#define UI_RIGHT            BIT(1)
#define UI_CENTER           (UI_LEFT | UI_RIGHT)
#define UI_BOTTOM           BIT(2)
#define UI_TOP              BIT(3)
#define UI_MIDDLE           (UI_BOTTOM | UI_TOP)
#define UI_DROPSHADOW       BIT(4)
#define UI_ALTCOLOR         BIT(5)
#define UI_XORCOLOR         BIT(6)
#define UI_IGNORECOLOR      (UI_ALTCOLOR | UI_XORCOLOR)
#define UI_MULTILINE        BIT(7)
#define UI_DRAWCURSOR       BIT(8)

//=============================================

static inline float FloatSwap(float f)
{
    union {
        float f;
        uint32_t l;
    } dat1, dat2;

    dat1.f = f;
    dat2.l = __builtin_bswap32(dat1.l);
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

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define BigShort(x)     __builtin_bswap16(x)
#define BigLong(x)      __builtin_bswap32(x)
#define BigFloat(x)     FloatSwap(x)
#define LittleShort(x)  ((uint16_t)(x))
#define LittleLong(x)   ((uint32_t)(x))
#define LittleFloat(x)  ((float)(x))
#define MakeRawLong(b1,b2,b3,b4) MakeLittleLong(b1,b2,b3,b4)
#define MakeRawShort(b1,b2) MakeLittleShort(b1,b2)
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define BigShort(x)     ((uint16_t)(x))
#define BigLong(x)      ((uint32_t)(x))
#define BigFloat(x)     ((float)(x))
#define LittleShort(x)  __builtin_bswap16(x)
#define LittleLong(x)   __builtin_bswap32(x)
#define LittleFloat(x)  FloatSwap(x)
#define MakeRawLong(b1,b2,b3,b4) MakeBigLong(b1,b2,b3,b4)
#define MakeRawShort(b1,b2) MakeBigShort(b1,b2)
#else
#error Unknown byte order
#endif

#define MakeLittleShort(b1,b2) \
    (((uint32_t)(b2)<<8)|(uint32_t)(b1))

#define MakeBigShort(b1,b2) \
    (((uint32_t)(b1)<<8)|(uint32_t)(b2))

#define MakeLittleLong(b1,b2,b3,b4) \
    (((uint32_t)(b4)<<24)|((uint32_t)(b3)<<16)|((uint32_t)(b2)<<8)|(uint32_t)(b1))

#define MakeBigLong(b1,b2,b3,b4) \
    (((uint32_t)(b1)<<24)|((uint32_t)(b2)<<16)|((uint32_t)(b3)<<8)|(uint32_t)(b4))

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
#define MAX_INFO_STRING     1024

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

#define CVAR_ARCHIVE        BIT(0)      // set to cause it to be saved to vars.rc
#define CVAR_USERINFO       BIT(1)      // added to userinfo when changed
#define CVAR_SERVERINFO     BIT(2)      // added to serverinfo when changed
#define CVAR_NOSET          BIT(3)      // don't allow change from console at all,
                                        // but can be set from the command line
#define CVAR_LATCH          BIT(4)      // save changes until server restart
#define CVAR_CHEAT          BIT(5)      // can't be changed when connected
#define CVAR_PRIVATE        BIT(6)      // never macro expanded or saved to config
#define CVAR_ROM            BIT(7)      // can't be changed even from cmdline
#define CVAR_MODIFIED       BIT(8)      // modified by user
#define CVAR_CUSTOM         BIT(9)      // created by user
#define CVAR_WEAK           BIT(10)     // doesn't have value
#define CVAR_GAME           BIT(11)     // created by game library
#define CVAR_NOARCHIVE      BIT(12)     // never saved to config
#define CVAR_FILES          BIT(13)     // r_reload when changed
#define CVAR_REFRESH        BIT(14)     // vid_restart when changed
#define CVAR_SOUND          BIT(15)     // snd_restart when changed

#define CVAR_INFOMASK       (CVAR_USERINFO | CVAR_SERVERINFO)
#define CVAR_MODIFYMASK     (CVAR_INFOMASK | CVAR_FILES | CVAR_REFRESH | CVAR_SOUND)
#define CVAR_NOARCHIVEMASK  (CVAR_NOSET | CVAR_CHEAT | CVAR_PRIVATE | CVAR_ROM | CVAR_NOARCHIVE)

typedef struct {
    int integer;
    float value;
    bool modified;
    char string[MAX_QPATH];     // for longer strings use trap_Cvar_VariableString
} vm_cvar_t;

typedef struct {
    vm_cvar_t *var;
    const char *name;
    const char *default_string;
    unsigned flags;
} vm_cvar_reg_t;

#define VM_CVAR(name, def, flags) { &name, #name, def, flags }

typedef enum {
    CMPL_CASELESS    = BIT(0),
    CMPL_CHECKDUPS   = BIT(1),
    CMPL_STRIPQUOTES = BIT(2),
} completion_option_t;

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
    byte    signbits;       // signx + (signy<<1) + (signz<<2)
    byte    dir;            // DirToByte(normal)
    byte    pad;
} cplane_t;

// 0-2 are axial planes
#define PLANE_X         0
#define PLANE_Y         1
#define PLANE_Z         2
#define PLANE_NON_AXIAL 6

typedef int contents_t;

enum {
    MATERIAL_ID_DEFAULT,
    MATERIAL_ID_LADDER,
    MATERIAL_RESERVED_COUNT
};

typedef struct {
    char    material[16];
} material_info_t;

typedef struct {
    char    name[32];
    char    material[16];
    int     material_id;
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

static inline void CM_ClipEntity(trace_t *dst, const trace_t *src, int entnum)
{
    dst->allsolid |= src->allsolid;
    dst->startsolid |= src->startsolid;
    if (src->fraction < dst->fraction) {
        dst->fraction = src->fraction;
        VectorCopy(src->endpos, dst->endpos);
        dst->plane = src->plane;
        dst->surface_flags = src->surface_flags;
        dst->surface_id = src->surface_id;
        dst->contents = src->contents;
        dst->entnum = entnum;
    }
}

/*
==========================================================

  ELEMENTS COMMUNICATED ACROSS THE NET

==========================================================
*/

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

#define RF_FLARE_LOCK_ANGLE RF_MINLIGHT
#define RF_BEAM_LIGHTNING   (RF_BEAM | RF_GLOW)
//KEX

// player_state_t->refdef flags
#define RDF_NONE            0U
#define RDF_UNDERWATER      BIT(0)      // warp the screen as appropriate
#define RDF_NOWORLDMODEL    BIT(1)      // used for player configuration screen

//ROGUE
#define RDF_IRGOGGLES       BIT(2)
#define RDF_UVGOGGLES       BIT(3)
//ROGUE

#define RDF_TELEPORT_BIT    BIT(4)      // used by Q2PRO (extended servers)
#define RDF_NO_WEAPON_BOB   BIT(5)

// hack to encode ATTN_STATIC more efficiently
#define ATTN_ESCAPE_CODE    (ATTN_STATIC * 64)

// sound attenuation values
#define ATTN_NONE               0   // full volume the entire level
#define ATTN_NORM               1
#define ATTN_IDLE               2
#define ATTN_STATIC             3   // diminish very rapidly with distance

#define STAT_FRAGS  0   // server and game both reference!!!
#define MAX_STATS   64

#define MAX_AMMO    32
#define AMMO_BITS   10

// default server FPS
#define BASE_FRAMERATE          10
#define BASE_FRAMETIME          100
#define BASE_1_FRAMETIME        0.01f   // 1/BASE_FRAMETIME
#define BASE_FRAMETIME_1000     0.1f    // BASE_FRAMETIME/1000

#define ANGLE2SHORT(x)  ((int)((x)*65536/360) & 65535)
#define SHORT2ANGLE(x)  ((x)*(360.0f/65536))

#define MAX_MAP_AREA_BYTES      32
#define MAX_PACKET_ENTITIES     MAX_EDICTS_OLD

#define CS_NAME             0   // server and game both reference!!!
#define MAX_CONFIGSTRINGS   0x4000

// server frame flags
#define FF_SUPPRESSED   BIT(0)
#define FF_CLIENTDROP   BIT(1)
#define FF_CLIENTPRED   BIT(2)
#define FF_RESERVED     BIT(3)

#define MAX_EVENTS      4

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
    uint32_t    event[MAX_EVENTS]; // impulse events -- muzzle flashes, footsteps, etc
                                   // events only go out for a single frame, they
                                   // are automatically cleared each frame
    uint32_t    event_param[MAX_EVENTS];
    uint32_t    morefx;
    float       alpha;
    float       scale;
    uint32_t    othernum;
} entity_state_t;

//==============================================

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

// player_state_t is the information needed to render a view.
typedef struct {
    // if any part of the game code modifies these fields, it
    // will result in a prediction error of some degree.
    uint32_t    pm_type;
    vec3_t      origin;
    vec3_t      velocity;
    uint32_t    pm_flags;       // ducked, jump_held, etc
    uint32_t    pm_time;        // in msec
    int32_t     gravity;
    int32_t     delta_angles[3];    // add to command angles to get view direction
                                    // changed by spawns, rotating objects, and teleporters

    // these fields do not need to be communicated bit-precise
    uint32_t    clientnum;      // current POV number

    vec3_t      viewangles;     // for fixed views
    int32_t     viewheight;

    int32_t     bobtime;

    uint32_t    gunindex;
    uint32_t    gunskin;
    uint32_t    gunframe;
    uint32_t    gunrate;

    vec4_t      screen_blend;       // rgba full screen effect
    vec4_t      damage_blend;

    player_fog_t        fog;
    player_heightfog_t  heightfog;

    int32_t     fov;            // horizontal field of view

    uint32_t    rdflags;        // refdef flags

    uint32_t    ammo[MAX_AMMO];
    int32_t     stats[MAX_STATS];   // fast status bar updates
} player_state_t;

//==============================================

// a SOLID_BBOX will never create this value
#define PACKED_BSP      255

static inline uint32_t MSG_PackSolid(const vec3_t mins, const vec3_t maxs)
{
    int x = maxs[0];
    int y = maxs[1];
    int zd = -mins[2];
    int zu = maxs[2] + 32;

    x = Q_clip(x, 1, 255);
    y = Q_clip(y, 1, 255);
    zd = Q_clip_uint8(zd);
    zu = Q_clip_uint8(zu);

    return MakeLittleLong(x, y, zd, zu);
}

static inline void MSG_UnpackSolid(uint32_t solid, vec3_t mins, vec3_t maxs)
{
    int x = solid & 255;
    int y = (solid >> 8) & 255;
    int zd = (solid >> 16) & 255;
    int zu = ((solid >> 24) & 255) - 32;

    VectorSet(mins, -x, -y, -zd);
    VectorSet(maxs,  x,  y,  zu);
}
