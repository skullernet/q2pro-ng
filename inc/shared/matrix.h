/*
Copyright (C) 2025 Andrey Nazarov

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

typedef union {
    float v[16];
    float m[4][4];
    vec4_t cols[4];
} mat4_t;

static inline mat4_t Mat4_FromCols(vec4_t a, vec4_t b, vec4_t c, vec4_t d)
{
    return (mat4_t){ .cols = { a, b, c, d } };
}

static inline mat4_t Mat4_FromRows(vec4_t a, vec4_t b, vec4_t c, vec4_t d)
{
    return Mat4_FromCols(
        Vec4(a.x, b.x, c.x, d.x),
        Vec4(a.y, b.y, c.y, d.y),
        Vec4(a.z, b.z, c.z, d.z),
        Vec4(a.w, b.w, c.w, d.w)
    );
}

static inline mat4_t Mat4_Identity(void)
{
    return Mat4_FromCols(
        Vec4(1, 0, 0, 0),
        Vec4(0, 1, 0, 0),
        Vec4(0, 0, 1, 0),
        Vec4(0, 0, 0, 1)
    );
}

static inline mat4_t Mat4_Ortho(float xmin, float xmax, float ymin, float ymax, float znear, float zfar)
{
    float width, height, depth;

    width  = xmax - xmin;
    height = ymax - ymin;
    depth  = zfar - znear;

    return Mat4_FromRows(
        Vec4(2 / width, 0, 0, -(xmax + xmin) / width),
        Vec4(0, 2 / height, 0, -(ymax + ymin) / height),
        Vec4(0, 0, -2 / depth, -(zfar + znear) / depth),
        Vec4(0, 0, 0, 1)
    );
}

static inline mat4_t Mat4_Frustum(float fov_x, float fov_y, float znear, float zfar)
{
    float xmin, xmax, ymin, ymax, width, height, depth;

    xmax = znear * tanf(fov_x * (M_PIf / 360));
    xmin = -xmax;

    ymax = znear * tanf(fov_y * (M_PIf / 360));
    ymin = -ymax;

    width  = xmax - xmin;
    height = ymax - ymin;
    depth  = zfar - znear;

    return Mat4_FromRows(
        Vec4(2 * znear / width, 0, (xmax + xmin) / width, 0),
        Vec4(0, 2 * znear / height, (ymax + ymin) / height, 0),
        Vec4(0, 0, -(zfar + znear) / depth, -2 * zfar * znear / depth),
        Vec4(0, 0, -1, 0)
    );
}

static inline mat4_t Mat4_RotateForViewer(vec3_t origin, const vec3_t axis[3])
{
    return Mat4_FromRows(
        Vec4_FromVec3(Vec3_Negate(axis[1]), Vec3_Dot(axis[1], origin)),
        Vec4_FromVec3(            axis[2], -Vec3_Dot(axis[2], origin)),
        Vec4_FromVec3(Vec3_Negate(axis[0]), Vec3_Dot(axis[0], origin)),
        Vec4(0, 0, 0, 1)
    );
}

static inline vec4_t Mat4_TransformVector(mat4_t a, vec4_t b)
{
    return (vec4_t) {
        .x = a.m[0][0] * b.x + a.m[1][0] * b.y + a.m[2][0] * b.z + a.m[3][0] * b.w,
        .y = a.m[0][1] * b.x + a.m[1][1] * b.y + a.m[2][1] * b.z + a.m[3][1] * b.w,
        .z = a.m[0][2] * b.x + a.m[1][2] * b.y + a.m[2][2] * b.z + a.m[3][2] * b.w,
        .w = a.m[0][3] * b.x + a.m[1][3] * b.y + a.m[2][3] * b.z + a.m[3][3] * b.w
    };
}

static inline mat4_t Mat4_Multiply(mat4_t a, mat4_t b)
{
    return Mat4_FromCols(
        Mat4_TransformVector(a, b.cols[0]),
        Mat4_TransformVector(a, b.cols[1]),
        Mat4_TransformVector(a, b.cols[2]),
        Mat4_TransformVector(a, b.cols[3])
    );
}
