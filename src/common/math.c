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

#if USE_CLIENT

/*
======
vectoangles2 - this is duplicated in the game DLL, but I need it here.
======
*/
void vectoangles2(const vec3_t value1, vec3_t angles)
{
    float   forward;
    float   yaw, pitch;

    if (value1[1] == 0 && value1[0] == 0) {
        yaw = 0;
        if (value1[2] > 0)
            pitch = 90;
        else
            pitch = 270;
    } else {
        if (value1[0])
            yaw = RAD2DEG(atan2f(value1[1], value1[0]));
        else if (value1[1] > 0)
            yaw = 90;
        else
            yaw = 270;

        if (yaw < 0)
            yaw += 360;

        forward = sqrtf(value1[0] * value1[0] + value1[1] * value1[1]);
        pitch = RAD2DEG(atan2f(value1[2], forward));
        if (pitch < 0)
            pitch += 360;
    }

    angles[PITCH] = -pitch;
    angles[YAW] = yaw;
    angles[ROLL] = 0;
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

#endif  // USE_CLIENT

const vec3_t bytedirs[NUMVERTEXNORMALS] = {
    {-0.525731, 0.000000, 0.850651}, 
    {-0.442863, 0.238856, 0.864188}, 
    {-0.295242, 0.000000, 0.955423}, 
    {-0.309017, 0.500000, 0.809017}, 
    {-0.162460, 0.262866, 0.951056}, 
    {0.000000, 0.000000, 1.000000}, 
    {0.000000, 0.850651, 0.525731}, 
    {-0.147621, 0.716567, 0.681718}, 
    {0.147621, 0.716567, 0.681718}, 
    {0.000000, 0.525731, 0.850651}, 
    {0.309017, 0.500000, 0.809017}, 
    {0.525731, 0.000000, 0.850651}, 
    {0.295242, 0.000000, 0.955423}, 
    {0.442863, 0.238856, 0.864188}, 
    {0.162460, 0.262866, 0.951056}, 
    {-0.681718, 0.147621, 0.716567}, 
    {-0.809017, 0.309017, 0.500000}, 
    {-0.587785, 0.425325, 0.688191}, 
    {-0.850651, 0.525731, 0.000000}, 
    {-0.864188, 0.442863, 0.238856}, 
    {-0.716567, 0.681718, 0.147621}, 
    {-0.688191, 0.587785, 0.425325}, 
    {-0.500000, 0.809017, 0.309017}, 
    {-0.238856, 0.864188, 0.442863}, 
    {-0.425325, 0.688191, 0.587785}, 
    {-0.716567, 0.681718, -0.147621}, 
    {-0.500000, 0.809017, -0.309017}, 
    {-0.525731, 0.850651, 0.000000}, 
    {0.000000, 0.850651, -0.525731}, 
    {-0.238856, 0.864188, -0.442863}, 
    {0.000000, 0.955423, -0.295242}, 
    {-0.262866, 0.951056, -0.162460}, 
    {0.000000, 1.000000, 0.000000}, 
    {0.000000, 0.955423, 0.295242}, 
    {-0.262866, 0.951056, 0.162460}, 
    {0.238856, 0.864188, 0.442863}, 
    {0.262866, 0.951056, 0.162460}, 
    {0.500000, 0.809017, 0.309017}, 
    {0.238856, 0.864188, -0.442863}, 
    {0.262866, 0.951056, -0.162460}, 
    {0.500000, 0.809017, -0.309017}, 
    {0.850651, 0.525731, 0.000000}, 
    {0.716567, 0.681718, 0.147621}, 
    {0.716567, 0.681718, -0.147621}, 
    {0.525731, 0.850651, 0.000000}, 
    {0.425325, 0.688191, 0.587785}, 
    {0.864188, 0.442863, 0.238856}, 
    {0.688191, 0.587785, 0.425325}, 
    {0.809017, 0.309017, 0.500000}, 
    {0.681718, 0.147621, 0.716567}, 
    {0.587785, 0.425325, 0.688191}, 
    {0.955423, 0.295242, 0.000000}, 
    {1.000000, 0.000000, 0.000000}, 
    {0.951056, 0.162460, 0.262866}, 
    {0.850651, -0.525731, 0.000000}, 
    {0.955423, -0.295242, 0.000000}, 
    {0.864188, -0.442863, 0.238856}, 
    {0.951056, -0.162460, 0.262866}, 
    {0.809017, -0.309017, 0.500000}, 
    {0.681718, -0.147621, 0.716567}, 
    {0.850651, 0.000000, 0.525731}, 
    {0.864188, 0.442863, -0.238856}, 
    {0.809017, 0.309017, -0.500000}, 
    {0.951056, 0.162460, -0.262866}, 
    {0.525731, 0.000000, -0.850651}, 
    {0.681718, 0.147621, -0.716567}, 
    {0.681718, -0.147621, -0.716567}, 
    {0.850651, 0.000000, -0.525731}, 
    {0.809017, -0.309017, -0.500000}, 
    {0.864188, -0.442863, -0.238856}, 
    {0.951056, -0.162460, -0.262866}, 
    {0.147621, 0.716567, -0.681718}, 
    {0.309017, 0.500000, -0.809017}, 
    {0.425325, 0.688191, -0.587785}, 
    {0.442863, 0.238856, -0.864188}, 
    {0.587785, 0.425325, -0.688191}, 
    {0.688191, 0.587785, -0.425325}, 
    {-0.147621, 0.716567, -0.681718}, 
    {-0.309017, 0.500000, -0.809017}, 
    {0.000000, 0.525731, -0.850651}, 
    {-0.525731, 0.000000, -0.850651}, 
    {-0.442863, 0.238856, -0.864188}, 
    {-0.295242, 0.000000, -0.955423}, 
    {-0.162460, 0.262866, -0.951056}, 
    {0.000000, 0.000000, -1.000000}, 
    {0.295242, 0.000000, -0.955423}, 
    {0.162460, 0.262866, -0.951056}, 
    {-0.442863, -0.238856, -0.864188}, 
    {-0.309017, -0.500000, -0.809017}, 
    {-0.162460, -0.262866, -0.951056}, 
    {0.000000, -0.850651, -0.525731}, 
    {-0.147621, -0.716567, -0.681718}, 
    {0.147621, -0.716567, -0.681718}, 
    {0.000000, -0.525731, -0.850651}, 
    {0.309017, -0.500000, -0.809017}, 
    {0.442863, -0.238856, -0.864188}, 
    {0.162460, -0.262866, -0.951056}, 
    {0.238856, -0.864188, -0.442863}, 
    {0.500000, -0.809017, -0.309017}, 
    {0.425325, -0.688191, -0.587785}, 
    {0.716567, -0.681718, -0.147621}, 
    {0.688191, -0.587785, -0.425325}, 
    {0.587785, -0.425325, -0.688191}, 
    {0.000000, -0.955423, -0.295242}, 
    {0.000000, -1.000000, 0.000000}, 
    {0.262866, -0.951056, -0.162460}, 
    {0.000000, -0.850651, 0.525731}, 
    {0.000000, -0.955423, 0.295242}, 
    {0.238856, -0.864188, 0.442863}, 
    {0.262866, -0.951056, 0.162460}, 
    {0.500000, -0.809017, 0.309017}, 
    {0.716567, -0.681718, 0.147621}, 
    {0.525731, -0.850651, 0.000000}, 
    {-0.238856, -0.864188, -0.442863}, 
    {-0.500000, -0.809017, -0.309017}, 
    {-0.262866, -0.951056, -0.162460}, 
    {-0.850651, -0.525731, 0.000000}, 
    {-0.716567, -0.681718, -0.147621}, 
    {-0.716567, -0.681718, 0.147621}, 
    {-0.525731, -0.850651, 0.000000}, 
    {-0.500000, -0.809017, 0.309017}, 
    {-0.238856, -0.864188, 0.442863}, 
    {-0.262866, -0.951056, 0.162460}, 
    {-0.864188, -0.442863, 0.238856}, 
    {-0.809017, -0.309017, 0.500000}, 
    {-0.688191, -0.587785, 0.425325}, 
    {-0.681718, -0.147621, 0.716567}, 
    {-0.442863, -0.238856, 0.864188}, 
    {-0.587785, -0.425325, 0.688191}, 
    {-0.309017, -0.500000, 0.809017}, 
    {-0.147621, -0.716567, 0.681718}, 
    {-0.425325, -0.688191, 0.587785}, 
    {-0.162460, -0.262866, 0.951056}, 
    {0.442863, -0.238856, 0.864188}, 
    {0.162460, -0.262866, 0.951056}, 
    {0.309017, -0.500000, 0.809017}, 
    {0.147621, -0.716567, 0.681718}, 
    {0.000000, -0.525731, 0.850651}, 
    {0.425325, -0.688191, 0.587785}, 
    {0.587785, -0.425325, 0.688191}, 
    {0.688191, -0.587785, 0.425325}, 
    {-0.955423, 0.295242, 0.000000}, 
    {-0.951056, 0.162460, 0.262866}, 
    {-1.000000, 0.000000, 0.000000}, 
    {-0.850651, 0.000000, 0.525731}, 
    {-0.955423, -0.295242, 0.000000}, 
    {-0.951056, -0.162460, 0.262866}, 
    {-0.864188, 0.442863, -0.238856}, 
    {-0.951056, 0.162460, -0.262866}, 
    {-0.809017, 0.309017, -0.500000}, 
    {-0.864188, -0.442863, -0.238856}, 
    {-0.951056, -0.162460, -0.262866}, 
    {-0.809017, -0.309017, -0.500000}, 
    {-0.681718, 0.147621, -0.716567}, 
    {-0.681718, -0.147621, -0.716567}, 
    {-0.850651, 0.000000, -0.525731}, 
    {-0.688191, 0.587785, -0.425325}, 
    {-0.587785, 0.425325, -0.688191}, 
    {-0.425325, 0.688191, -0.587785}, 
    {-0.425325, -0.688191, -0.587785}, 
    {-0.587785, -0.425325, -0.688191}, 
    {-0.688191, -0.587785, -0.425325}, 
};

int DirToByte(const vec3_t dir)
{
    int     i, best;
    float   d, bestd;

    if (!dir) {
        return 0;
    }

    bestd = 0;
    best = 0;
    for (i = 0; i < NUMVERTEXNORMALS; i++) {
        d = DotProduct(dir, bytedirs[i]);
        if (d > bestd) {
            bestd = d;
            best = i;
        }
    }

    return best;
}

#if 0
void ByteToDir(int index, vec3_t dir)
{
    if (index < 0 || index >= NUMVERTEXNORMALS) {
        Com_Error(ERR_FATAL, "ByteToDir: illegal index");
    }

    VectorCopy(bytedirs[index], dir);
}
#endif

void SetPlaneType(cplane_t *plane)
{
    vec_t *normal = plane->normal;

    if (normal[0] == 1) {
        plane->type = PLANE_X;
        return;
    }
    if (normal[1] == 1) {
        plane->type = PLANE_Y;
        return;
    }
    if (normal[2] == 1) {
        plane->type = PLANE_Z;
        return;
    }

    plane->type = PLANE_NON_AXIAL;
}

void SetPlaneSignbits(cplane_t *plane)
{
    int bits = 0;

    if (plane->normal[0] < 0) {
        bits |= 1;
    }
    if (plane->normal[1] < 0) {
        bits |= 2;
    }
    if (plane->normal[2] < 0) {
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
box_plane_t BoxOnPlaneSide(const vec3_t emins, const vec3_t emaxs, const cplane_t *p)
{
    const vec_t *bounds[2] = { emins, emaxs };
    int     i = p->signbits & 1;
    int     j = (p->signbits >> 1) & 1;
    int     k = (p->signbits >> 2) & 1;

#define P(i, j, k) \
    p->normal[0] * bounds[i][0] + \
    p->normal[1] * bounds[j][1] + \
    p->normal[2] * bounds[k][2]

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

#define X 0
#define Y 1
#define Z 2
#define W 3

void Quat_ComputeW(quat_t q)
{
    float t = 1.0f - (q[X] * q[X]) - (q[Y] * q[Y]) - (q[Z] * q[Z]);

    if (t < 0.0f) {
        q[W] = 0.0f;
    } else {
        q[W] = -sqrtf(t);
    }
}

#define DOT_THRESHOLD   0.9995f

void Quat_SLerp(const quat_t qa, const quat_t qb, float backlerp, float frontlerp, quat_t out)
{
    if (backlerp <= 0.0f) {
        Vector4Copy(qb, out);
        return;
    } else if (backlerp >= 1.0f) {
        Vector4Copy(qa, out);
        return;
    }

    // compute "cosine of angle between quaternions" using dot product
    float cosOmega = Dot4Product(qa, qb);

    /* If negative dot, use -q1.  Two quaternions q and -q
       represent the same rotation, but may produce
       different slerp.  We chose q or -q to rotate using
       the acute angle. */
    float q1w = qb[W];
    float q1x = qb[X];
    float q1y = qb[Y];
    float q1z = qb[Z];

    if (cosOmega < 0.0f) {
        q1w = -q1w;
        q1x = -q1x;
        q1y = -q1y;
        q1z = -q1z;
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

    out[W] = (k0 * qa[W]) + (k1 * q1w);
    out[X] = (k0 * qa[X]) + (k1 * q1x);
    out[Y] = (k0 * qa[Y]) + (k1 * q1y);
    out[Z] = (k0 * qa[Z]) + (k1 * q1z);
}

float Quat_Normalize(quat_t q)
{
    float length = sqrtf(Dot4Product(q, q));

    if (length) {
        float ilength = 1 / length;
        q[X] *= ilength;
        q[Y] *= ilength;
        q[Z] *= ilength;
        q[W] *= ilength;
    }

    return length;
}

void Quat_MultiplyQuat(const float *restrict qa, const float *restrict qb, quat_t out)
{
    out[W] = (qa[W] * qb[W]) - (qa[X] * qb[X]) - (qa[Y] * qb[Y]) - (qa[Z] * qb[Z]);
    out[X] = (qa[X] * qb[W]) + (qa[W] * qb[X]) + (qa[Y] * qb[Z]) - (qa[Z] * qb[Y]);
    out[Y] = (qa[Y] * qb[W]) + (qa[W] * qb[Y]) + (qa[Z] * qb[X]) - (qa[X] * qb[Z]);
    out[Z] = (qa[Z] * qb[W]) + (qa[W] * qb[Z]) + (qa[X] * qb[Y]) - (qa[Y] * qb[X]);
}

void Quat_MultiplyVector(const float *restrict q, const float *restrict v, quat_t out)
{
    out[W] = -(q[X] * v[X]) - (q[Y] * v[Y]) - (q[Z] * v[Z]);
    out[X] = (q[W] * v[X]) + (q[Y] * v[Z]) - (q[Z] * v[Y]);
    out[Y] = (q[W] * v[Y]) + (q[Z] * v[X]) - (q[X] * v[Z]);
    out[Z] = (q[W] * v[Z]) + (q[X] * v[Y]) - (q[Y] * v[X]);
}

// Conjugate quaternion. Also, inverse, for unit quaternions (which MD5 quats are)
void Quat_Conjugate(const quat_t in, quat_t out)
{
    out[W] = in[W];
    out[X] = -in[X];
    out[Y] = -in[Y];
    out[Z] = -in[Z];
}

void Quat_RotatePoint(const quat_t q, const vec3_t in, vec3_t out)
{
    quat_t tmp, inv, output;

    // Assume q is unit quaternion
    Quat_Conjugate(q, inv);
    Quat_MultiplyVector(q, in, tmp);
    Quat_MultiplyQuat(tmp, inv, output);

    out[X] = output[X];
    out[Y] = output[Y];
    out[Z] = output[Z];
}

void Quat_ToAxis(const quat_t q, vec3_t axis[3])
{
    float q0 = q[W];
    float q1 = q[X];
    float q2 = q[Y];
    float q3 = q[Z];

    axis[0][0] = 2 * (q0 * q0 + q1 * q1) - 1;
    axis[0][1] = 2 * (q1 * q2 - q0 * q3);
    axis[0][2] = 2 * (q1 * q3 + q0 * q2);

    axis[1][0] = 2 * (q1 * q2 + q0 * q3);
    axis[1][1] = 2 * (q0 * q0 + q2 * q2) - 1;
    axis[1][2] = 2 * (q2 * q3 - q0 * q1);

    axis[2][0] = 2 * (q1 * q3 - q0 * q2);
    axis[2][1] = 2 * (q2 * q3 + q0 * q1);
    axis[2][2] = 2 * (q0 * q0 + q3 * q3) - 1;
}

#endif  // USE_MD5
