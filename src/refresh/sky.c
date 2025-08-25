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

static void DefaultSkyMatrix(GLfloat *matrix)
{
    if (skyautorotate) {
        SetupRotationMatrix(skymatrix, skyaxis, glr.fd.time * skyrotate);
        TransposeAxis(skymatrix);
    }

    matrix[ 0] = skymatrix[0][0];
    matrix[ 4] = skymatrix[0][1];
    matrix[ 8] = skymatrix[0][2];
    matrix[12] = -DotProduct(skymatrix[0], glr.fd.vieworg);

    matrix[ 1] = skymatrix[2][0];
    matrix[ 5] = skymatrix[2][1];
    matrix[ 9] = skymatrix[2][2];
    matrix[13] = -DotProduct(skymatrix[2], glr.fd.vieworg);

    matrix[ 2] = skymatrix[1][0];
    matrix[ 6] = skymatrix[1][1];
    matrix[10] = skymatrix[1][2];
    matrix[14] = -DotProduct(skymatrix[1], glr.fd.vieworg);

    matrix[ 3] = 0;
    matrix[ 7] = 0;
    matrix[11] = 0;
    matrix[15] = 1;
}

// classic skies don't rotate
static void ClassicSkyMatrix(GLfloat *matrix)
{
    matrix[ 0] = 1;
    matrix[ 4] = 0;
    matrix[ 8] = 0;
    matrix[12] = -glr.fd.vieworg[0];

    matrix[ 1] = 0;
    matrix[ 5] = 1;
    matrix[ 9] = 0;
    matrix[13] = -glr.fd.vieworg[1];

    matrix[ 2] = 0;
    matrix[ 6] = 0;
    matrix[10] = 3;
    matrix[14] = -glr.fd.vieworg[2] * 3;

    matrix[ 3] = 0;
    matrix[ 7] = 0;
    matrix[11] = 0;
    matrix[15] = 1;
}

/*
============
R_RotateForSky
============
*/
void R_RotateForSky(void)
{
    DefaultSkyMatrix(glr.skymatrix[0]);
    ClassicSkyMatrix(glr.skymatrix[1]);
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
void R_SetSky(const char *name, float rotate, bool autorotate, const vec3_t axis)
{
    char            pathname[MAX_QPATH];
    const image_t   *image;

    if (!gl_drawsky->integer) {
        R_UnsetSky();
        return;
    }

    Com_DDPrintf("%s: %s %.1f %d (%.1f %.1f %.1f)\n", __func__,
                 name, rotate, autorotate, axis[0], axis[1], axis[2]);

    // check for no rotation
    if (VectorNormalize2(axis, skyaxis) < 0.001f)
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
