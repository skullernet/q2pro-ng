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

const vec3_t vec3_origin = { 0, 0, 0 };

void AngleVectors(const vec3_t angles, vec3_t forward, vec3_t right, vec3_t up)
{
    float        angle;
    float        sr, sp, sy, cr, cp, cy;

    angle = DEG2RAD(angles[YAW]);
    sy = sinf(angle);
    cy = cosf(angle);
    angle = DEG2RAD(angles[PITCH]);
    sp = sinf(angle);
    cp = cosf(angle);
    angle = DEG2RAD(angles[ROLL]);
    sr = sinf(angle);
    cr = cosf(angle);

    if (forward) {
        forward[0] = cp * cy;
        forward[1] = cp * sy;
        forward[2] = -sp;
    }
    if (right) {
        right[0] = (-1 * sr * sp * cy + -1 * cr * -sy);
        right[1] = (-1 * sr * sp * sy + -1 * cr * cy);
        right[2] = -1 * sr * cp;
    }
    if (up) {
        up[0] = (cr * sp * cy + -sr * -sy);
        up[1] = (cr * sp * sy + -sr * cy);
        up[2] = cr * cp;
    }
}

vec_t VectorNormalize(vec3_t v)
{
    float    length, ilength;

    length = VectorLength(v);

    if (length) {
        ilength = 1 / length;
        v[0] *= ilength;
        v[1] *= ilength;
        v[2] *= ilength;
    }

    return length;
}

vec_t VectorNormalize2(const vec3_t v, vec3_t out)
{
    VectorCopy(v, out);
    return VectorNormalize(out);
}

void ClearBounds(vec3_t mins, vec3_t maxs)
{
    mins[0] = mins[1] = mins[2] = 99999;
    maxs[0] = maxs[1] = maxs[2] = -99999;
}

void AddPointToBounds(const vec3_t v, vec3_t mins, vec3_t maxs)
{
    int        i;
    vec_t    val;

    for (i = 0; i < 3; i++) {
        val = v[i];
        mins[i] = min(mins[i], val);
        maxs[i] = max(maxs[i], val);
    }
}

void UnionBounds(const vec3_t a[2], const vec3_t b[2], vec3_t c[2])
{
    int        i;

    for (i = 0; i < 3; i++) {
        c[0][i] = min(a[0][i], b[0][i]);
        c[1][i] = max(a[1][i], b[1][i]);
    }
}

/*
=================
RadiusFromBounds
=================
*/
vec_t RadiusFromBounds(const vec3_t mins, const vec3_t maxs)
{
    int     i;
    vec3_t  corner;
    vec_t   a, b;

    for (i = 0; i < 3; i++) {
        a = fabsf(mins[i]);
        b = fabsf(maxs[i]);
        corner[i] = max(a, b);
    }

    return VectorLength(corner);
}

/*
==================
SetupRotationMatrix

Setup rotation matrix given the normalized direction vector and angle to rotate
around this vector. Adapted from Mesa 3D implementation of _math_matrix_rotate.
==================
*/
void SetupRotationMatrix(vec3_t matrix[3], const vec3_t dir, float degrees)
{
    vec_t   angle, s, c, one_c, xx, yy, zz, xy, yz, zx, xs, ys, zs;

    angle = DEG2RAD(degrees);
    s = sinf(angle);
    c = cosf(angle);
    one_c = 1.0F - c;

    xx = dir[0] * dir[0];
    yy = dir[1] * dir[1];
    zz = dir[2] * dir[2];
    xy = dir[0] * dir[1];
    yz = dir[1] * dir[2];
    zx = dir[2] * dir[0];
    xs = dir[0] * s;
    ys = dir[1] * s;
    zs = dir[2] * s;

    matrix[0][0] = (one_c * xx) + c;
    matrix[0][1] = (one_c * xy) - zs;
    matrix[0][2] = (one_c * zx) + ys;

    matrix[1][0] = (one_c * xy) + zs;
    matrix[1][1] = (one_c * yy) + c;
    matrix[1][2] = (one_c * yz) - xs;

    matrix[2][0] = (one_c * zx) - ys;
    matrix[2][1] = (one_c * yz) + xs;
    matrix[2][2] = (one_c * zz) + c;
}

void RotatePointAroundVector(vec3_t out, const vec3_t dir, const vec3_t in, float degrees)
{
    vec3_t matrix[3];
    vec3_t temp;

    SetupRotationMatrix(matrix, dir, degrees);

    VectorCopy(in, temp);
    VectorRotate(temp, matrix, out);
}

void MakeNormalVectors(const vec3_t forward, vec3_t right, vec3_t up)
{
    float       d;

    // this rotate and negate guarantees a vector
    // not colinear with the original
    right[1] = -forward[0];
    right[2] = forward[1];
    right[0] = forward[2];

    d = DotProduct(right, forward);
    VectorMA(right, -d, forward, right);
    VectorNormalize(right);
    CrossProduct(right, forward, up);
}

/*
====================
V_CalcFov
====================
*/
float V_CalcFov(float fov_x, float width, float height)
{
    float x = width / tanf(fov_x * (M_PIf / 360));
    float a = atanf(height / x) * (360 / M_PIf);

    return a;
}

const vec3_t bytedirs[NUMVERTEXNORMALS] = {
    { -0.525731, 0.000000, 0.850651 },
    { -0.442863, 0.238856, 0.864188 },
    { -0.295242, 0.000000, 0.955423 },
    { -0.309017, 0.500000, 0.809017 },
    { -0.162460, 0.262866, 0.951056 },
    { 0.000000, 0.000000, 1.000000 },
    { 0.000000, 0.850651, 0.525731 },
    { -0.147621, 0.716567, 0.681718 },
    { 0.147621, 0.716567, 0.681718 },
    { 0.000000, 0.525731, 0.850651 },
    { 0.309017, 0.500000, 0.809017 },
    { 0.525731, 0.000000, 0.850651 },
    { 0.295242, 0.000000, 0.955423 },
    { 0.442863, 0.238856, 0.864188 },
    { 0.162460, 0.262866, 0.951056 },
    { -0.681718, 0.147621, 0.716567 },
    { -0.809017, 0.309017, 0.500000 },
    { -0.587785, 0.425325, 0.688191 },
    { -0.850651, 0.525731, 0.000000 },
    { -0.864188, 0.442863, 0.238856 },
    { -0.716567, 0.681718, 0.147621 },
    { -0.688191, 0.587785, 0.425325 },
    { -0.500000, 0.809017, 0.309017 },
    { -0.238856, 0.864188, 0.442863 },
    { -0.425325, 0.688191, 0.587785 },
    { -0.716567, 0.681718, -0.147621 },
    { -0.500000, 0.809017, -0.309017 },
    { -0.525731, 0.850651, 0.000000 },
    { 0.000000, 0.850651, -0.525731 },
    { -0.238856, 0.864188, -0.442863 },
    { 0.000000, 0.955423, -0.295242 },
    { -0.262866, 0.951056, -0.162460 },
    { 0.000000, 1.000000, 0.000000 },
    { 0.000000, 0.955423, 0.295242 },
    { -0.262866, 0.951056, 0.162460 },
    { 0.238856, 0.864188, 0.442863 },
    { 0.262866, 0.951056, 0.162460 },
    { 0.500000, 0.809017, 0.309017 },
    { 0.238856, 0.864188, -0.442863 },
    { 0.262866, 0.951056, -0.162460 },
    { 0.500000, 0.809017, -0.309017 },
    { 0.850651, 0.525731, 0.000000 },
    { 0.716567, 0.681718, 0.147621 },
    { 0.716567, 0.681718, -0.147621 },
    { 0.525731, 0.850651, 0.000000 },
    { 0.425325, 0.688191, 0.587785 },
    { 0.864188, 0.442863, 0.238856 },
    { 0.688191, 0.587785, 0.425325 },
    { 0.809017, 0.309017, 0.500000 },
    { 0.681718, 0.147621, 0.716567 },
    { 0.587785, 0.425325, 0.688191 },
    { 0.955423, 0.295242, 0.000000 },
    { 1.000000, 0.000000, 0.000000 },
    { 0.951056, 0.162460, 0.262866 },
    { 0.850651, -0.525731, 0.000000 },
    { 0.955423, -0.295242, 0.000000 },
    { 0.864188, -0.442863, 0.238856 },
    { 0.951056, -0.162460, 0.262866 },
    { 0.809017, -0.309017, 0.500000 },
    { 0.681718, -0.147621, 0.716567 },
    { 0.850651, 0.000000, 0.525731 },
    { 0.864188, 0.442863, -0.238856 },
    { 0.809017, 0.309017, -0.500000 },
    { 0.951056, 0.162460, -0.262866 },
    { 0.525731, 0.000000, -0.850651 },
    { 0.681718, 0.147621, -0.716567 },
    { 0.681718, -0.147621, -0.716567 },
    { 0.850651, 0.000000, -0.525731 },
    { 0.809017, -0.309017, -0.500000 },
    { 0.864188, -0.442863, -0.238856 },
    { 0.951056, -0.162460, -0.262866 },
    { 0.147621, 0.716567, -0.681718 },
    { 0.309017, 0.500000, -0.809017 },
    { 0.425325, 0.688191, -0.587785 },
    { 0.442863, 0.238856, -0.864188 },
    { 0.587785, 0.425325, -0.688191 },
    { 0.688191, 0.587785, -0.425325 },
    { -0.147621, 0.716567, -0.681718 },
    { -0.309017, 0.500000, -0.809017 },
    { 0.000000, 0.525731, -0.850651 },
    { -0.525731, 0.000000, -0.850651 },
    { -0.442863, 0.238856, -0.864188 },
    { -0.295242, 0.000000, -0.955423 },
    { -0.162460, 0.262866, -0.951056 },
    { 0.000000, 0.000000, -1.000000 },
    { 0.295242, 0.000000, -0.955423 },
    { 0.162460, 0.262866, -0.951056 },
    { -0.442863, -0.238856, -0.864188 },
    { -0.309017, -0.500000, -0.809017 },
    { -0.162460, -0.262866, -0.951056 },
    { 0.000000, -0.850651, -0.525731 },
    { -0.147621, -0.716567, -0.681718 },
    { 0.147621, -0.716567, -0.681718 },
    { 0.000000, -0.525731, -0.850651 },
    { 0.309017, -0.500000, -0.809017 },
    { 0.442863, -0.238856, -0.864188 },
    { 0.162460, -0.262866, -0.951056 },
    { 0.238856, -0.864188, -0.442863 },
    { 0.500000, -0.809017, -0.309017 },
    { 0.425325, -0.688191, -0.587785 },
    { 0.716567, -0.681718, -0.147621 },
    { 0.688191, -0.587785, -0.425325 },
    { 0.587785, -0.425325, -0.688191 },
    { 0.000000, -0.955423, -0.295242 },
    { 0.000000, -1.000000, 0.000000 },
    { 0.262866, -0.951056, -0.162460 },
    { 0.000000, -0.850651, 0.525731 },
    { 0.000000, -0.955423, 0.295242 },
    { 0.238856, -0.864188, 0.442863 },
    { 0.262866, -0.951056, 0.162460 },
    { 0.500000, -0.809017, 0.309017 },
    { 0.716567, -0.681718, 0.147621 },
    { 0.525731, -0.850651, 0.000000 },
    { -0.238856, -0.864188, -0.442863 },
    { -0.500000, -0.809017, -0.309017 },
    { -0.262866, -0.951056, -0.162460 },
    { -0.850651, -0.525731, 0.000000 },
    { -0.716567, -0.681718, -0.147621 },
    { -0.716567, -0.681718, 0.147621 },
    { -0.525731, -0.850651, 0.000000 },
    { -0.500000, -0.809017, 0.309017 },
    { -0.238856, -0.864188, 0.442863 },
    { -0.262866, -0.951056, 0.162460 },
    { -0.864188, -0.442863, 0.238856 },
    { -0.809017, -0.309017, 0.500000 },
    { -0.688191, -0.587785, 0.425325 },
    { -0.681718, -0.147621, 0.716567 },
    { -0.442863, -0.238856, 0.864188 },
    { -0.587785, -0.425325, 0.688191 },
    { -0.309017, -0.500000, 0.809017 },
    { -0.147621, -0.716567, 0.681718 },
    { -0.425325, -0.688191, 0.587785 },
    { -0.162460, -0.262866, 0.951056 },
    { 0.442863, -0.238856, 0.864188 },
    { 0.162460, -0.262866, 0.951056 },
    { 0.309017, -0.500000, 0.809017 },
    { 0.147621, -0.716567, 0.681718 },
    { 0.000000, -0.525731, 0.850651 },
    { 0.425325, -0.688191, 0.587785 },
    { 0.587785, -0.425325, 0.688191 },
    { 0.688191, -0.587785, 0.425325 },
    { -0.955423, 0.295242, 0.000000 },
    { -0.951056, 0.162460, 0.262866 },
    { -1.000000, 0.000000, 0.000000 },
    { -0.850651, 0.000000, 0.525731 },
    { -0.955423, -0.295242, 0.000000 },
    { -0.951056, -0.162460, 0.262866 },
    { -0.864188, 0.442863, -0.238856 },
    { -0.951056, 0.162460, -0.262866 },
    { -0.809017, 0.309017, -0.500000 },
    { -0.864188, -0.442863, -0.238856 },
    { -0.951056, -0.162460, -0.262866 },
    { -0.809017, -0.309017, -0.500000 },
    { -0.681718, 0.147621, -0.716567 },
    { -0.681718, -0.147621, -0.716567 },
    { -0.850651, 0.000000, -0.525731 },
    { -0.688191, 0.587785, -0.425325 },
    { -0.587785, 0.425325, -0.688191 },
    { -0.425325, 0.688191, -0.587785 },
    { -0.425325, -0.688191, -0.587785 },
    { -0.587785, -0.425325, -0.688191 },
    { -0.688191, -0.587785, -0.425325 },
};

int DirToByte(const vec3_t dir)
{
    int     i, best;
    float   d, bestd;

    bestd = 0;
    best = 0;
    for (i = 0; i < NUMVERTEXNORMALS; i++) {
        d = DotProduct(dir, bytedirs[i]);
        if (d > bestd) {
            bestd = d;
            best = i + 1;
        }
    }

    return best;
}

void ByteToDir(unsigned index, vec3_t dir)
{
    if (index) {
        Q_assert_soft(index <= NUMVERTEXNORMALS);
        VectorCopy(bytedirs[index - 1], dir);
    } else {
        VectorClear(dir);
    }
}

//====================================================================================

/*
============
COM_SkipPath
============
*/
char *COM_SkipPath(const char *pathname)
{
    char    *last;

    Q_assert(pathname);

    last = (char *)pathname;
    while (*pathname) {
        if (*pathname == '/')
            last = (char *)pathname + 1;
        pathname++;
    }
    return last;
}

/*
============
COM_StripExtension
============
*/
size_t COM_StripExtension(char *out, const char *in, size_t size)
{
    size_t ret = COM_FileExtension(in) - in;

    if (size) {
        size_t len = min(ret, size - 1);
        memcpy(out, in, len);
        out[len] = 0;
    }

    return ret;
}

/*
============
COM_FileExtension
============
*/
char *COM_FileExtension(const char *in)
{
    const char *last, *s;

    Q_assert(in);

    for (last = s = in + strlen(in); s != in; s--) {
        if (*s == '/') {
            break;
        }
        if (*s == '.') {
            return (char *)s;
        }
    }

    return (char *)last;
}

/*
==================
COM_DefaultExtension

if path doesn't have .EXT, append extension
(extension should include the .)
==================
*/
size_t COM_DefaultExtension(char *path, const char *ext, size_t size)
{
    if (*COM_FileExtension(path))
        return strlen(path);
    else
        return Q_strlcat(path, ext, size);
}

/*
============
COM_SplitPath

Splits an input filename into file name and path components
============
*/
void COM_SplitPath(const char *in, char *name, size_t name_size,
                   char *path, size_t path_size, bool strip_ext)
{
    const char *p = COM_SkipPath(in);

    if (strip_ext)
        COM_StripExtension(name, p, name_size);
    else
        Q_strlcpy(name, p, name_size);

    Q_strlcpy(path, in, min(path_size, p - in + 1));
}

/*
==================
COM_IsFloat

Returns true if the given string is valid representation
of floating point number.
==================
*/
bool COM_IsFloat(const char *s)
{
    int c, dot = '.';

    if (*s == '-') {
        s++;
    }
    if (!*s) {
        return false;
    }

    do {
        c = *s++;
        if (c == dot) {
            dot = 0;
        } else if (!Q_isdigit(c)) {
            return false;
        }
    } while (*s);

    return true;
}

bool COM_IsUint(const char *s)
{
    int c;

    if (!*s) {
        return false;
    }

    do {
        c = *s++;
        if (!Q_isdigit(c)) {
            return false;
        }
    } while (*s);

    return true;
}

bool COM_IsPath(const char *s)
{
    int c;

    if (!*s) {
        return false;
    }

    do {
        c = *s++;
        if (!Q_ispath(c)) {
            return false;
        }
    } while (*s);

    return true;
}

bool COM_IsWhite(const char *s)
{
    int c;

    while (*s) {
        c = *s++;
        if (Q_isgraph(c)) {
            return false;
        }
    }

    return true;
}

int SortStrcmp(const void *p1, const void *p2)
{
    return strcmp(*(const char **)p1, *(const char **)p2);
}

int SortStricmp(const void *p1, const void *p2)
{
    return Q_stricmp(*(const char **)p1, *(const char **)p2);
}

/*
================
COM_strclr

Operates inplace, normalizing high-bit and removing unprintable characters.
Returns final number of characters, not including the NUL character.
================
*/
size_t COM_strclr(char *s)
{
    char *p;
    int c;
    size_t len;

    p = s;
    len = 0;
    while (*s) {
        c = *s++;
        c &= 127;
        if (Q_isprint(c)) {
            *p++ = c;
            len++;
        }
    }

    *p = 0;

    return len;
}

char *COM_StripQuotes(char *s)
{
    if (*s == '"') {
        size_t p = strlen(s) - 1;

        if (s[p] == '"') {
            s[p] = 0;
            return s + 1;
        }
    }

    return s;
}

char *COM_TrimSpace(char *s)
{
    size_t len;

    while (*s && *s <= ' ')
        s++;

    len = strlen(s);
    while (len > 0 && s[len - 1] <= ' ')
        len--;

    s[len] = 0;
    return s;
}

/*
============
va

does a varargs printf into a temp buffer, so I don't need to have
varargs versions of all text functions.
============
*/
char *va(const char *format, ...)
{
    va_list         argptr;
    static char     buffers[4][MAX_STRING_CHARS];
    static int      index;

    index = (index + 1) & 3;

    va_start(argptr, format);
    Q_vsnprintf(buffers[index], sizeof(buffers[0]), format, argptr);
    va_end(argptr);

    return buffers[index];
}

/*
=============
vtos

This is just a convenience function for printing vectors.
=============
*/
char *vtos(const vec3_t v)
{
    static char str[8][32];
    static int  index;

    index = (index + 1) & 7;

    Q_snprintf(str[index], sizeof(str[0]), "(%.f %.f %.f)", v[0], v[1], v[2]);

    return str[index];
}

unsigned com_linenum;

/*
==============
COM_Parse

Parse a token out of a string.
Handles C and C++ comments.
==============
*/
size_t COM_ParseToken(const char **data_p, char *buffer, size_t size)
{
    int         c;
    size_t      len;
    const char  *data;

    data = *data_p;
    len = 0;
    if (size)
        *buffer = 0;

    if (!data) {
        *data_p = NULL;
        return len;
    }

// skip whitespace
skipwhite:
    while ((c = *data) <= ' ') {
        if (c == 0) {
            *data_p = NULL;
            return len;
        }
        if (c == '\n') {
            com_linenum++;
        }
        data++;
    }

// skip // comments
    if (c == '/' && data[1] == '/') {
        data += 2;
        while (*data && *data != '\n')
            data++;
        goto skipwhite;
    }

// skip /* */ comments
    if (c == '/' && data[1] == '*') {
        data += 2;
        while (*data) {
            if (data[0] == '*' && data[1] == '/') {
                data += 2;
                break;
            }
            if (data[0] == '\n') {
                com_linenum++;
            }
            data++;
        }
        goto skipwhite;
    }

// handle quoted strings specially
    if (c == '\"') {
        data++;
        while (1) {
            c = *data++;
            if (c == '\"' || !c) {
                goto finish;
            }
            if (c == '\n') {
                com_linenum++;
            }
            if (len + 1 < size) {
                *buffer++ = c;
            }
            len++;
        }
    }

// parse a regular word
    do {
        if (len + 1 < size) {
            *buffer++ = c;
        }
        len++;
        data++;
        c = *data;
    } while (c > 32);

finish:
    if (size)
        *buffer = 0;

    *data_p = data;
    return len;
}

char *COM_Parse(const char **data_p)
{
    static char     com_token[4][MAX_TOKEN_CHARS];
    static int      com_tokidx;
    char            *s = com_token[com_tokidx];

    COM_ParseToken(data_p, s, sizeof(com_token[0]));
    com_tokidx = (com_tokidx + 1) & 3;
    return s;
}

/*
==============
COM_Compress

Operates in place, removing excess whitespace and comments.
Non-contiguous line feeds are preserved.

Returns resulting data length.
==============
*/
size_t COM_Compress(char *data)
{
    int     c, n = 0;
    char    *s = data, *d = data;

    while (*s) {
        // skip whitespace
        if (*s <= ' ') {
            if (n == 0) {
                n = ' ';
            }
            do {
                c = *s++;
                if (c == '\n') {
                    n = '\n';
                }
                if (!c) {
                    goto finish;
                }
            } while (*s <= ' ');
        }

        // skip // comments
        if (s[0] == '/' && s[1] == '/') {
            n = ' ';
            s += 2;
            while (*s && *s != '\n') {
                s++;
            }
            continue;
        }

        // skip /* */ comments
        if (s[0] == '/' && s[1] == '*') {
            n = ' ';
            s += 2;
            while (*s) {
                if (s[0] == '*' && s[1] == '/') {
                    s += 2;
                    break;
                }
                if (*s == '\n') {
                    n = '\n';
                }
                s++;
            }
            continue;
        }

        // add whitespace character
        if (n) {
            *d++ = n;
            n = 0;
        }

        // handle quoted strings specially
        if (*s == '\"') {
            s++;
            *d++ = '\"';
            do {
                c = *s++;
                if (!c) {
                    goto finish;
                }
                *d++ = c;
            } while (c != '\"');
            continue;
        }

        // handle line feed escape
        if (*s == '\\' && s[1] == '\n') {
            s += 2;
            continue;
        }
        if (*s == '\\' && s[1] == '\r' && s[2] == '\n') {
            s += 3;
            continue;
        }

        // parse a regular word
        do {
            *d++ = *s++;
        } while (*s > ' ');
    }

finish:
    *d = 0;

    return d - data;
}

static int escape_char(int c)
{
    switch (c) {
        case '\a': return 'a';
        case '\b': return 'b';
        case '\t': return 't';
        case '\n': return 'n';
        case '\v': return 'v';
        case '\f': return 'f';
        case '\r': return 'r';
        case '\\': return '\\';
        case '"': return '"';
    }
    return 0;
}

const char com_hexchars[16] = "0123456789ABCDEF";

size_t COM_EscapeString(char *dst, const char *src, size_t size)
{
    char *p, *end;

    if (!size)
        return 0;

    p = dst;
    end = dst + size;
    while (*src) {
        byte c = *src++;
        int e = escape_char(c);

        if (e) {
            if (end - p <= 2)
                break;
            *p++ = '\\';
            *p++ = e;
        } else if (Q_isprint(c)) {
            if (end - p <= 1)
                break;
            *p++ = c;
        } else {
            if (end - p <= 4)
                break;
            *p++ = '\\';
            *p++ = 'x';
            *p++ = com_hexchars[c >> 4];
            *p++ = com_hexchars[c & 15];
        }
    }

    *p = 0;
    return p - dst;
}

char *COM_MakePrintable(const char *s)
{
    static char buffer[4096];
    COM_EscapeString(buffer, s, sizeof(buffer));
    return buffer;
}

/*
============================================================================

                    COLORS PARSING

============================================================================
*/

const char *const colorNames[COLOR_COUNT] = {
    "black", "red", "green", "yellow",
    "blue", "cyan", "magenta", "white",
    "alt", "none"
};

const uint32_t colorTable[8] = {
    U32_BLACK, U32_RED, U32_GREEN, U32_YELLOW,
    U32_BLUE, U32_CYAN, U32_MAGENTA, U32_WHITE
};

/*
================
COM_ParseColorIndex

Parses color name or index.
Returns COLOR_NONE in case of error.
================
*/
color_index_t COM_ParseColorIndex(const char *s)
{
    color_index_t i;

    if (COM_IsUint(s)) {
        i = Q_atoi(s);
        if (i < 0 || i >= COLOR_COUNT) {
            return COLOR_NONE;
        }
        return i;
    }

    for (i = 0; i < COLOR_COUNT; i++) {
        if (!strcmp(colorNames[i], s)) {
            return i;
        }
    }

    return COLOR_NONE;
}

bool COM_ParseColor(const char *s, color_t *color)
{
    int i;
    int c[8];

    // parse generic color
    if (*s == '#') {
        s++;
        for (i = 0; s[i]; i++) {
            if (i == 8) {
                return false;
            }
            c[i] = Q_charhex(s[i]);
            if (c[i] == -1) {
                return false;
            }
        }

        switch (i) {
        case 3:
            color->u8[0] = c[0] | (c[0] << 4);
            color->u8[1] = c[1] | (c[1] << 4);
            color->u8[2] = c[2] | (c[2] << 4);
            color->u8[3] = 255;
            break;
        case 6:
            color->u8[0] = c[1] | (c[0] << 4);
            color->u8[1] = c[3] | (c[2] << 4);
            color->u8[2] = c[5] | (c[4] << 4);
            color->u8[3] = 255;
            break;
        case 8:
            color->u8[0] = c[1] | (c[0] << 4);
            color->u8[1] = c[3] | (c[2] << 4);
            color->u8[2] = c[5] | (c[4] << 4);
            color->u8[3] = c[7] | (c[6] << 4);
            break;
        default:
            return false;
        }

        return true;
    }

    // parse name or index
    i = COM_ParseColorIndex(s);
    if (i >= q_countof(colorTable)) {
        return false;
    }

    color->u32 = colorTable[i];
    return true;
}

/*
============================================================================

                    LIBRARY REPLACEMENT FUNCTIONS

============================================================================
*/

int Q_strncasecmp(const char *s1, const char *s2, size_t n)
{
    int        c1, c2;

    do {
        c1 = *s1++;
        c2 = *s2++;

        if (!n--)
            return 0;        /* strings are equal until end point */

        if (c1 != c2) {
            c1 = Q_tolower(c1);
            c2 = Q_tolower(c2);
            if (c1 < c2)
                return -1;
            if (c1 > c2)
                return 1;        /* strings not equal */
        }
    } while (c1);

    return 0;        /* strings are equal */
}

int Q_strcasecmp(const char *s1, const char *s2)
{
    int        c1, c2;

    do {
        c1 = *s1++;
        c2 = *s2++;

        if (c1 != c2) {
            c1 = Q_tolower(c1);
            c2 = Q_tolower(c2);
            if (c1 < c2)
                return -1;
            if (c1 > c2)
                return 1;        /* strings not equal */
        }
    } while (c1);

    return 0;        /* strings are equal */
}

char *Q_strcasestr(const char *s1, const char *s2)
{
    size_t l1, l2;

    l2 = strlen(s2);
    if (!l2) {
        return (char *)s1;
    }

    l1 = strlen(s1);
    while (l1 >= l2) {
        l1--;
        if (!Q_strncasecmp(s1, s2, l2)) {
            return (char *)s1;
        }
        s1++;
    }

    return NULL;
}

/*
===============
Q_strlcpy

Returns length of the source string.
===============
*/
size_t Q_strlcpy(char *dst, const char *src, size_t size)
{
    size_t ret = strlen(src);

    if (size) {
        size_t len = min(ret, size - 1);
        memcpy(dst, src, len);
        dst[len] = 0;
    }

    return ret;
}

/*
===============
Q_strlcat

Returns length of the source and destinations strings combined.
===============
*/
size_t Q_strlcat(char *dst, const char *src, size_t size)
{
    size_t len = strlen(dst);

    Q_assert(len < size);

    return len + Q_strlcpy(dst + len, src, size - len);
}

/*
===============
Q_concat_array

Returns number of characters that would be written into the buffer,
excluding trailing '\0'. If the returned value is equal to or greater than
buffer size, resulting string is truncated.
===============
*/
size_t Q_concat_array(char *dest, size_t size, const char **arr)
{
    size_t total = 0;

    while (*arr) {
        const char *s = *arr++;
        size_t len = strlen(s);
        if (total < size) {
            size_t l = min(size - total - 1, len);
            memcpy(dest, s, l);
            dest += l;
        }
        total += len;
    }

    if (size) {
        *dest = 0;
    }

    return total;
}

/*
===============
Q_vsnprintf

Returns number of characters that would be written into the buffer,
excluding trailing '\0'. If the returned value is equal to or greater than
buffer size, resulting string is truncated.
===============
*/
size_t Q_vsnprintf(char *dest, size_t size, const char *fmt, va_list argptr)
{
    int ret;

    Q_assert(size <= INT_MAX);
    ret = vsnprintf(dest, size, fmt, argptr);
    Q_assert(ret >= 0);

    return ret;
}

/*
===============
Q_vscnprintf

Returns number of characters actually written into the buffer,
excluding trailing '\0'. If buffer size is 0, this function does nothing
and returns 0.
===============
*/
size_t Q_vscnprintf(char *dest, size_t size, const char *fmt, va_list argptr)
{
    if (size) {
        size_t ret = Q_vsnprintf(dest, size, fmt, argptr);
        return min(ret, size - 1);
    }

    return 0;
}

/*
===============
Q_snprintf

Returns number of characters that would be written into the buffer,
excluding trailing '\0'. If the returned value is equal to or greater than
buffer size, resulting string is truncated.
===============
*/
size_t Q_snprintf(char *dest, size_t size, const char *fmt, ...)
{
    va_list argptr;
    size_t  ret;

    va_start(argptr, fmt);
    ret = Q_vsnprintf(dest, size, fmt, argptr);
    va_end(argptr);

    return ret;
}

/*
===============
Q_scnprintf

Returns number of characters actually written into the buffer,
excluding trailing '\0'. If buffer size is 0, this function does nothing
and returns 0.
===============
*/
size_t Q_scnprintf(char *dest, size_t size, const char *fmt, ...)
{
    va_list argptr;
    size_t  ret;

    va_start(argptr, fmt);
    ret = Q_vscnprintf(dest, size, fmt, argptr);
    va_end(argptr);

    return ret;
}

#ifndef HAVE_STRCHRNUL
char *Q_strchrnul(const char *s, int c)
{
    while (*s && *s != c) {
        s++;
    }
    return (char *)s;
}
#endif

#ifndef HAVE_MEMCCPY
/*
===============
Q_memccpy

Copies no more than 'size' bytes stopping when 'c' character is found.
Returns pointer to next byte after 'c' in 'dst', or NULL if 'c' was not found.
===============
*/
void *Q_memccpy(void *dst, const void *src, int c, size_t size)
{
    byte *d = dst;
    const byte *s = src;

    while (size--) {
        if ((*d++ = *s++) == c) {
            return d;
        }
    }

    return NULL;
}
#endif

#ifndef HAVE_STRNLEN
size_t Q_strnlen(const char *s, size_t maxlen)
{
    char *p = memchr(s, 0, maxlen);
    return p ? p - s : maxlen;
}
#endif

#ifndef _WIN32
int Q_atoi(const char *s)
{
    return Q_clipl_int32(strtol(s, NULL, 10));
}
#endif

/*
=====================================================================

  MT19337 PRNG

=====================================================================
*/

#define N 624
#define M 397

static uint32_t mt_state[N];
static uint32_t mt_index;

/*
==================
Q_srand

Seed PRNG with initial value
==================
*/
void Q_srand(uint32_t seed)
{
    mt_index = N;
    mt_state[0] = seed;
    for (int i = 1; i < N; i++)
        mt_state[i] = seed = 1812433253 * (seed ^ seed >> 30) + i;
}

/*
==================
Q_rand

Generate random integer in range [0, 2^32)
==================
*/
uint32_t Q_rand(void)
{
    uint32_t x, y;
    int i;

    if (mt_index >= N) {
        mt_index = 0;

#define STEP(j, k) do {                 \
        x  = mt_state[i] & BIT(31);     \
        x |= mt_state[j] & MASK(31);    \
        y  = x >> 1;                    \
        y ^= 0x9908B0DF & -(x & 1);     \
        mt_state[i] = mt_state[k] ^ y;  \
    } while (0)

        for (i = 0; i < N - M; i++)
            STEP(i + 1, i + M);
        for (     ; i < N - 1; i++)
            STEP(i + 1, i - N + M);
        STEP(0, M - 1);
    }

    y = mt_state[mt_index++];
    y ^= y >> 11;
    y ^= y <<  7 & 0x9D2C5680;
    y ^= y << 15 & 0xEFC60000;
    y ^= y >> 18;

    return y;
}

/*
==================
Q_rand_uniform

Generate random integer in range [0, n) avoiding modulo bias
==================
*/
uint32_t Q_rand_uniform(uint32_t n)
{
    uint32_t r, m;

    if (n < 2)
        return 0;

    m = -n % n; // m = 2^32 mod n
    do {
        r = Q_rand();
    } while (r < m);

    return r % n;
}

/*
=====================================================================

  INFO STRINGS

=====================================================================
*/

/*
===============
Info_ValueForKey

Searches the string for the given
key and returns the associated value, or an empty string.
===============
*/
char *Info_ValueForKey(const char *s, const char *key)
{
    // use 4 buffers so compares work without stomping on each other
    static char value[4][MAX_INFO_STRING];
    static int  valueindex;
    char        pkey[MAX_INFO_STRING];
    char        *o;

    valueindex = (valueindex + 1) & 3;
    if (*s == '\\')
        s++;
    while (1) {
        o = pkey;
        while (*s != '\\') {
            if (!*s)
                goto fail;
            *o++ = *s++;
        }
        *o = 0;
        s++;

        o = value[valueindex];
        while (*s != '\\' && *s) {
            *o++ = *s++;
        }
        *o = 0;

        if (!strcmp(key, pkey))
            return value[valueindex];

        if (!*s)
            goto fail;
        s++;
    }

fail:
    o = value[valueindex];
    *o = 0;
    return o;
}

/*
==================
Info_RemoveKey
==================
*/
void Info_RemoveKey(char *s, const char *key)
{
    char    *start;
    char    pkey[MAX_INFO_STRING];
    char    *o;

    while (1) {
        start = s;
        if (*s == '\\')
            s++;
        o = pkey;
        while (*s != '\\') {
            if (!*s)
                return;
            *o++ = *s++;
        }
        *o = 0;
        s++;

        while (*s != '\\' && *s) {
            s++;
        }

        if (!strcmp(key, pkey)) {
            o = start; // remove this part
            while (*s) {
                *o++ = *s++;
            }
            *o = 0;
            s = start;
            continue; // search for duplicates
        }

        if (!*s)
            return;
    }

}


/*
==================
Info_Validate

Some characters are illegal in info strings because they
can mess up the server's parsing.
Also checks the length of keys/values and the whole string.
==================
*/
bool Info_Validate(const char *s)
{
    size_t len, total;
    int c;

    total = 0;
    while (1) {
        //
        // validate key
        //
        if (*s == '\\') {
            s++;
            if (++total == MAX_INFO_STRING) {
                return false;   // oversize infostring
            }
        }
        if (!*s) {
            return false;   // missing key
        }
        len = 0;
        while (*s != '\\') {
            c = *s++;
            if (!Q_isprint(c) || c == '\"' || c == ';') {
                return false;   // illegal characters
            }
            if (++len == MAX_INFO_KEY) {
                return false;   // oversize key
            }
            if (++total == MAX_INFO_STRING) {
                return false;   // oversize infostring
            }
            if (!*s) {
                return false;   // missing value
            }
        }

        //
        // validate value
        //
        s++;
        if (++total == MAX_INFO_STRING) {
            return false;   // oversize infostring
        }
        if (!*s) {
            return false;   // missing value
        }
        len = 0;
        while (*s != '\\') {
            c = *s++;
            if (!Q_isprint(c) || c == '\"' || c == ';') {
                return false;   // illegal characters
            }
            if (++len == MAX_INFO_VALUE) {
                return false;   // oversize value
            }
            if (++total == MAX_INFO_STRING) {
                return false;   // oversize infostring
            }
            if (!*s) {
                return true;    // end of string
            }
        }
    }

    return false; // quiet compiler warning
}

/*
============
Info_SubValidate
============
*/
size_t Info_SubValidate(const char *s)
{
    size_t len;
    int c;

    len = 0;
    while (*s) {
        c = *s++;
        c &= 127;       // strip high bits
        if (c == '\\' || c == '\"' || c == ';') {
            return SIZE_MAX;  // illegal characters
        }
        if (++len == MAX_QPATH) {
            return MAX_QPATH;  // oversize value
        }
    }

    return len;
}

/*
==================
Info_SetValueForKey
==================
*/
bool Info_SetValueForKey(char *s, const char *key, const char *value)
{
    char    newi[MAX_INFO_STRING], *v;
    size_t  l, kl, vl;
    int     c;

    // validate key
    kl = Info_SubValidate(key);
    if (kl >= MAX_QPATH) {
        return false;
    }

    // validate value
    vl = Info_SubValidate(value);
    if (vl >= MAX_QPATH) {
        return false;
    }

    Info_RemoveKey(s, key);
    if (!vl) {
        return true;
    }

    l = strlen(s);
    if (l + kl + vl + 2 >= MAX_INFO_STRING) {
        return false;
    }

    newi[0] = '\\';
    memcpy(newi + 1, key, kl);
    newi[kl + 1] = '\\';
    memcpy(newi + kl + 2, value, vl + 1);

    // only copy ascii values
    s += l;
    v = newi;
    while (*v) {
        c = *v++;
        c &= 127;        // strip high bits
        if (Q_isprint(c))
            *s++ = c;
    }
    *s = 0;

    return true;
}

/*
==================
Info_NextPair
==================
*/
void Info_NextPair(const char **string, char *key, char *value)
{
    char        *o;
    const char  *s;

    *value = *key = 0;

    s = *string;
    if (!s) {
        return;
    }

    if (*s == '\\')
        s++;

    if (!*s) {
        *string = NULL;
        return;
    }

    o = key;
    while (*s && *s != '\\') {
        *o++ = *s++;
    }
    *o = 0;

    if (!*s) {
        *string = NULL;
        return;
    }

    o = value;
    s++;
    while (*s && *s != '\\') {
        *o++ = *s++;
    }
    *o = 0;

    *string = s;
}

/*
==================
Info_Print
==================
*/
void Info_Print(const char *infostring)
{
    char    key[MAX_INFO_STRING];
    char    value[MAX_INFO_STRING];

    while (1) {
        Info_NextPair(&infostring, key, value);
        if (!infostring)
            break;

        if (!key[0])
            strcpy(key, "<MISSING KEY>");

        if (!value[0])
            strcpy(value, "<MISSING VALUE>");

        Com_Printf("%-20s %s\n", key, value);
    }
}
