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
#include "common/math.h"

void SetPlaneType(cplane_t *plane)
{
    if (plane->normal.x == 1) {
        plane->type = PLANE_X;
        return;
    }
    if (plane->normal.y == 1) {
        plane->type = PLANE_Y;
        return;
    }
    if (plane->normal.z == 1) {
        plane->type = PLANE_Z;
        return;
    }

    plane->type = PLANE_NON_AXIAL;
}

void SetPlaneSignbits(cplane_t *plane)
{
    int bits = 0;

    if (plane->normal.x < 0) {
        bits |= 1;
    }
    if (plane->normal.y < 0) {
        bits |= 2;
    }
    if (plane->normal.z < 0) {
        bits |= 4;
    }

    plane->signbits = bits;
}

/*
==================
BoxOnPlaneSide

Returns 1, 2, or 1 + 2
==================
*/
box_plane_t BoxOnPlaneSide(const box3_t *box, const cplane_t *p)
{
    int     i = p->signbits & 1;
    int     j = (p->signbits >> 1) & 1;
    int     k = (p->signbits >> 2) & 1;

#define P(i, j, k) \
    p->normal.x * box->bounds[i].x + \
    p->normal.y * box->bounds[j].y + \
    p->normal.z * box->bounds[k].z

    vec_t       dist1 = P(i ^ 1, j ^ 1, k ^ 1);
    vec_t       dist2 = P(i, j, k);
    box_plane_t sides = 0;

#undef P

    if (dist1 >= p->dist)
        sides = BOX_INFRONT;
    if (dist2 < p->dist)
        sides |= BOX_BEHIND;

    return sides;
}

#if USE_MD5

void Quat_ComputeW(quat_t *q)
{
    float t = 1.0f - (q->x * q->x) - (q->y * q->y) - (q->z * q->z);

    if (t < 0.0f) {
        q->w = 0.0f;
    } else {
        q->w = -sqrtf(t);
    }
}

static float Quat_DotProduct(quat_t a, quat_t b)
{
    return (a.w * b.w) + (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
}

#define DOT_THRESHOLD   0.9995f

quat_t Quat_SLerp(quat_t a, quat_t b, float backlerp, float frontlerp)
{
    if (backlerp <= 0.0f)
        return b;
    if (backlerp >= 1.0f)
        return a;

    // compute "cosine of angle between quaternions" using dot product
    float cosOmega = Quat_DotProduct(a, b);

    /* If negative dot, use -q1.  Two quaternions q and -q
       represent the same rotation, but may produce
       different slerp.  We chose q or -q to rotate using
       the acute angle. */
    if (cosOmega < 0.0f) {
        b.w = -b.w;
        b.x = -b.x;
        b.y = -b.y;
        b.z = -b.z;
        cosOmega = -cosOmega;
    }

    // compute interpolation fraction
    float k0, k1;

    if (cosOmega > DOT_THRESHOLD) {
        // very close - just use linear interpolation
        k0 = backlerp;
        k1 = frontlerp;
    } else {
        // compute the sin of the angle using the trig identity sin^2(omega) + cos^2(omega) = 1
        float sinOmega = sqrtf(1.0f - (cosOmega * cosOmega));

        // compute the angle from its sin and cosine
        float omega = atan2f(sinOmega, cosOmega);
        float oneOverSinOmega = 1.0f / sinOmega;

        k0 = sinf(backlerp * omega) * oneOverSinOmega;
        k1 = sinf(frontlerp * omega) * oneOverSinOmega;
    }

    return (quat_t) {
        .w = (k0 * a.w) + (k1 * b.w),
        .x = (k0 * a.x) + (k1 * b.x),
        .y = (k0 * a.y) + (k1 * b.y),
        .z = (k0 * a.z) + (k1 * b.z)
    };
}

float Quat_Normalize(quat_t *q)
{
    float length = sqrtf(Quat_DotProduct(*q, *q));

    if (length) {
        float ilength = 1 / length;
        q->x *= ilength;
        q->y *= ilength;
        q->z *= ilength;
        q->w *= ilength;
    }

    return length;
}

quat_t Quat_MultiplyQuat(quat_t a, quat_t b)
{
    return (quat_t) {
        .w = (a.w * b.w) - (a.x * b.x) - (a.y * b.y) - (a.z * b.z),
        .x = (a.x * b.w) + (a.w * b.x) + (a.y * b.z) - (a.z * b.y),
        .y = (a.y * b.w) + (a.w * b.y) + (a.z * b.x) - (a.x * b.z),
        .z = (a.z * b.w) + (a.w * b.z) + (a.x * b.y) - (a.y * b.x)
    };
}

quat_t Quat_MultiplyVector(quat_t q, vec3_t v)
{
    return (quat_t) {
        .w = -(q.x * v.x) - (q.y * v.y) - (q.z * v.z),
        .x =  (q.w * v.x) + (q.y * v.z) - (q.z * v.y),
        .y =  (q.w * v.y) + (q.z * v.x) - (q.x * v.z),
        .z =  (q.w * v.z) + (q.x * v.y) - (q.y * v.x)
    };
}

// Conjugate quaternion. Also, inverse, for unit quaternions (which MD5 quats are)
quat_t Quat_Conjugate(quat_t q)
{
    return (quat_t){ .w = q.w, .x = -q.x, .y = -q.y, .z = -q.z };
}

vec3_t Quat_RotatePoint(quat_t q, vec3_t in)
{
    // Assume q is unit quaternion
    quat_t inv = Quat_Conjugate(q);
    quat_t tmp = Quat_MultiplyVector(q, in);
    quat_t out = Quat_MultiplyQuat(tmp, inv);

    return Vec3(out.x, out.y, out.z);
}

void Quat_ToAxis(quat_t q, vec3_t axis[3])
{
    axis[0].x = 2 * (q.w * q.w + q.x * q.x) - 1;
    axis[0].y = 2 * (q.x * q.y - q.w * q.z);
    axis[0].z = 2 * (q.x * q.z + q.w * q.y);

    axis[1].x = 2 * (q.x * q.y + q.w * q.z);
    axis[1].y = 2 * (q.w * q.w + q.y * q.y) - 1;
    axis[1].z = 2 * (q.y * q.z - q.w * q.x);

    axis[2].x = 2 * (q.x * q.z - q.w * q.y);
    axis[2].y = 2 * (q.y * q.z + q.w * q.x);
    axis[2].z = 2 * (q.w * q.w + q.z * q.z) - 1;
}

#endif  // USE_MD5
