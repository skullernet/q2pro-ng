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

#include "gl.h"

static float    skyrotate;
static bool     skyautorotate;
static vec3_t   skyaxis;
static vec3_t   skymatrix[3];

/*
============
R_RotateForSky
============
*/
void R_RotateForSky(void)
{
    if (skyautorotate) {
        SetupRotationMatrix(skymatrix, skyaxis, glr.fd.time * skyrotate);
        TransposeAxis(skymatrix);
    }

    glr.skymatrix[0] = Mat4_FromRows(
        Vec4_FromVec3(skymatrix[0], -Vec3_Dot(skymatrix[0], glr.fd.vieworg)),
        Vec4_FromVec3(skymatrix[2], -Vec3_Dot(skymatrix[2], glr.fd.vieworg)),
        Vec4_FromVec3(skymatrix[1], -Vec3_Dot(skymatrix[1], glr.fd.vieworg)),
        Vec4(0, 0, 0, 1)
    );

    // classic skies don't rotate
    glr.skymatrix[1] = Mat4_FromRows(
        Vec4(1, 0, 0, -glr.fd.vieworg.x),
        Vec4(0, 1, 0, -glr.fd.vieworg.y),
        Vec4(0, 0, 3, -glr.fd.vieworg.z * 3),
        Vec4(0, 0, 0, 1)
    );
}

static void R_UnsetSky(void)
{
    skyrotate = 0;
    skyautorotate = false;
    AxisClear(skymatrix);
    R_SKYTEXTURE->texnum = TEXNUM_CUBEMAP_BLACK;
}

static const char com_env_suf[6][3] = { "rt", "lf", "bk", "ft", "up", "dn" };

/*
============
R_SetSky
============
*/
void R_SetSky(const char *name, float rotate, bool autorotate, vec3_t axis)
{
    char            pathname[MAX_QPATH];
    const image_t   *image;
    float           length;

    if (!gl_drawsky->integer) {
        R_UnsetSky();
        return;
    }

    Com_DDPrintf("%s: %s %.1f %d (%.1f %.1f %.1f)\n", __func__,
                 name, rotate, autorotate, axis.x, axis.y, axis.z);

    // check for no rotation
    skyaxis = Vec3_NormalizeLength(axis, &length);
    if (length < 0.001f)
        rotate = 0;
    if (!rotate)
        autorotate = false;

    skyrotate = rotate;
    skyautorotate = autorotate;

    if (!skyautorotate) {
        SetupRotationMatrix(skymatrix, skyaxis, skyrotate);
        TransposeAxis(skymatrix);
    }

    // try to load cubemap image first
    if (Q_concat(pathname, sizeof(pathname), "sky/", name, ".tga") >= sizeof(pathname)) {
        R_UnsetSky();
        return;
    }

    image = IMG_Find(pathname, IT_SKY, IF_CUBEMAP);
    if (image != R_SKYTEXTURE) {
        R_SKYTEXTURE->texnum = image->texnum;
        return;
    }

    // load legacy skybox
    R_SKYTEXTURE->texnum = TEXNUM_CUBEMAP_DEFAULT;

    for (int i = 0; i < 6; i++) {
        if (Q_concat(pathname, sizeof(pathname), "env/", name,
                     com_env_suf[i], ".tga") >= sizeof(pathname)) {
            R_UnsetSky();
            return;
        }
        image = IMG_Find(pathname, IT_SKY, IF_CUBEMAP | IF_TURBULENT);
        if (image == R_SKYTEXTURE) {
            R_UnsetSky();
            return;
        }
    }
}
