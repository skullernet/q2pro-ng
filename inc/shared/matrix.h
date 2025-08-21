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

static inline void Matrix_Ortho(float xmin, float xmax, float ymin, float ymax, float znear, float zfar, mat4_t out)
{
    float width, height, depth;

    width  = xmax - xmin;
    height = ymax - ymin;
    depth  = zfar - znear;

    out[ 0] = 2 / width;
    out[ 4] = 0;
    out[ 8] = 0;
    out[12] = -(xmax + xmin) / width;

    out[ 1] = 0;
    out[ 5] = 2 / height;
    out[ 9] = 0;
    out[13] = -(ymax + ymin) / height;

    out[ 2] = 0;
    out[ 6] = 0;
    out[10] = -2 / depth;
    out[14] = -(zfar + znear) / depth;

    out[ 3] = 0;
    out[ 7] = 0;
    out[11] = 0;
    out[15] = 1;
}

static inline void Matrix_Frustum(float fov_x, float fov_y, float znear, float zfar, mat4_t out)
{
    float xmin, xmax, ymin, ymax, width, height, depth;

    xmax = znear * tanf(fov_x * (M_PIf / 360));
    xmin = -xmax;

    ymax = znear * tanf(fov_y * (M_PIf / 360));
    ymin = -ymax;

    width  = xmax - xmin;
    height = ymax - ymin;
    depth  = zfar - znear;

    out[ 0] = 2 * znear / width;
    out[ 4] = 0;
    out[ 8] = (xmax + xmin) / width;
    out[12] = 0;

    out[ 1] = 0;
    out[ 5] = 2 * znear / height;
    out[ 9] = (ymax + ymin) / height;
    out[13] = 0;

    out[ 2] = 0;
    out[ 6] = 0;
    out[10] = -(zfar + znear) / depth;
    out[14] = -2 * zfar * znear / depth;

    out[ 3] = 0;
    out[ 7] = 0;
    out[11] = -1;
    out[15] = 0;
}

static inline void Matrix_RotateForViewer(const vec3_t origin, const vec3_t axis[3], mat4_t out)
{
    out[ 0] = -axis[1][0];
    out[ 4] = -axis[1][1];
    out[ 8] = -axis[1][2];
    out[12] = DotProduct(axis[1], origin);

    out[ 1] = axis[2][0];
    out[ 5] = axis[2][1];
    out[ 9] = axis[2][2];
    out[13] = -DotProduct(axis[2], origin);

    out[ 2] = -axis[0][0];
    out[ 6] = -axis[0][1];
    out[10] = -axis[0][2];
    out[14] = DotProduct(axis[0], origin);

    out[ 3] = 0;
    out[ 7] = 0;
    out[11] = 0;
    out[15] = 1;
}

static inline void Matrix_Multiply(const mat4_t a, const mat4_t b, mat4_t out)
{
    for (int i = 0; i < 4; i++) {
        const float b0 = b[i * 4 + 0];
        const float b1 = b[i * 4 + 1];
        const float b2 = b[i * 4 + 2];
        const float b3 = b[i * 4 + 3];
        out[i * 4 + 0] = a[0] * b0 + a[4] * b1 + a[ 8] * b2 + a[12] * b3;
        out[i * 4 + 1] = a[1] * b0 + a[5] * b1 + a[ 9] * b2 + a[13] * b3;
        out[i * 4 + 2] = a[2] * b0 + a[6] * b1 + a[10] * b2 + a[14] * b3;
        out[i * 4 + 3] = a[3] * b0 + a[7] * b1 + a[11] * b2 + a[15] * b3;
    }
}

static inline void Matrix_TransformVector3(const mat4_t a, const vec3_t b, vec4_t out)
{
    const float b0 = b[0];
    const float b1 = b[1];
    const float b2 = b[2];
    out[0] = a[0] * b0 + a[4] * b1 + a[ 8] * b2 + a[12];
    out[1] = a[1] * b0 + a[5] * b1 + a[ 9] * b2 + a[13];
    out[2] = a[2] * b0 + a[6] * b1 + a[10] * b2 + a[14];
    out[3] = a[3] * b0 + a[7] * b1 + a[11] * b2 + a[15];
}
