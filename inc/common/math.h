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

void SetPlaneType(cplane_t *plane);
void SetPlaneSignbits(cplane_t *plane);

typedef enum {
    BOX_INFRONT     = 1,
    BOX_BEHIND      = 2,
    BOX_INTERSECTS  = 3
} box_plane_t;

box_plane_t BoxOnPlaneSide(const box3_t *box, const cplane_t *p);

static inline box_plane_t BoxOnPlaneSideFast(const box3_t *box, const cplane_t *p)
{
    // fast axial cases
    if (p->type < 3) {
        if (p->dist <= box->mins.xyz[p->type])
            return BOX_INFRONT;
        if (p->dist >= box->maxs.xyz[p->type])
            return BOX_BEHIND;
        return BOX_INTERSECTS;
    }

    // slow generic case
    return BoxOnPlaneSide(box, p);
}

static inline vec_t PlaneDiffFast(vec3_t v, const cplane_t *p)
{
    // fast axial cases
    if (p->type < 3) {
        return v.xyz[p->type] - p->dist;
    }

    // slow generic case
    return PlaneDiff(v, p);
}

#if USE_MD5
// quaternion routines, for MD5 skeletons
typedef struct {
    float x, y, z, w;
} quat_t;

void Quat_ComputeW(quat_t *q);
quat_t Quat_SLerp(quat_t a, quat_t b, float backlerp, float frontlerp);
float Quat_Normalize(quat_t *q);
quat_t Quat_MultiplyQuat(quat_t a, quat_t b);
quat_t Quat_MultiplyVector(quat_t q, vec3_t v);
quat_t Quat_Conjugate(quat_t q);
vec3_t Quat_RotatePoint(quat_t q, vec3_t in);
void Quat_ToAxis(quat_t q, vec3_t axis[3]);
#endif  // USE_MD5
