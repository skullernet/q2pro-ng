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
// cl_view.c -- player rendering positioning

#include "client.h"

cvar_t   *cl_adjustfov;

int         r_numparticles;
particle_t  r_particles[MAX_PARTICLES];

//============================================================================

static void V_SetLightLevel(void)
{
    vec3_t shadelight;

    // save off light value for server to look at (BIG HACK!)
    R_LightPoint(cg.refdef.vieworg, shadelight);

    // pick the greatest component, which should be the same
    // as the mono value returned by software
    if (shadelight[0] > shadelight[1]) {
        if (shadelight[0] > shadelight[2]) {
            cg.lightlevel = 150.0f * shadelight[0];
        } else {
            cg.lightlevel = 150.0f * shadelight[2];
        }
    } else {
        if (shadelight[1] > shadelight[2]) {
            cg.lightlevel = 150.0f * shadelight[1];
        } else {
            cg.lightlevel = 150.0f * shadelight[2];
        }
    }
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


/*
==================
V_RenderView

==================
*/
void V_RenderView(void)
{
    // an invalid frame will just use the exact previous refdef
    // we can't use the old frame if the video mode has changed, though...
    if (cg.frame.valid) {
        V_ClearScene();

        // build a refresh entity list and calc cg.sim*
        // this also calls CG_CalcViewValues which loads
        // v_forward, etc.
        CG_AddEntities();

        // never let it sit exactly on a node line, because a water plane can
        // disappear when viewed with the eye exactly on it.
        // the server protocol only specifies to 1/8 pixel, so add 1/16 in each axis
        cg.refdef.vieworg[0] += 1.0f / 16;
        cg.refdef.vieworg[1] += 1.0f / 16;
        cg.refdef.vieworg[2] += 1.0f / 16;

        cg.refdef.x = scr_vrect.x;
        cg.refdef.y = scr_vrect.y;
        cg.refdef.width = scr_vrect.width;
        cg.refdef.height = scr_vrect.height;

        // adjust for non-4/3 screens
        if (cl_adjustfov->integer) {
            cg.refdef.fov_y = cg.fov_y;
            cg.refdef.fov_x = V_CalcFov(cg.refdef.fov_y, cg.refdef.height, cg.refdef.width);
        } else {
            cg.refdef.fov_x = cg.fov_x;
            cg.refdef.fov_y = V_CalcFov(cg.refdef.fov_x, cg.refdef.width, cg.refdef.height);
        }

        cg.refdef.frametime = cgs.frametime;
        cg.refdef.time = cg.time * 0.001f;
        memcpy(cg.refdef.areabits, cg.frame.areabits, sizeof(cg.refdef.areabits));

        if (cg.custom_fog.density) {
            cg.refdef.fog = cg.custom_fog;
            cg.refdef.heightfog = (player_heightfog_t){ 0 };
        }

        cg.refdef.rdflags = cg.frame.ps.rdflags;
    }

    R_RenderFrame(&cg.refdef);

    V_SetLightLevel();
}


/*
=============
V_Viewpos_f
=============
*/
static void V_Viewpos_f(void)
{
    Com_Printf("%s : %.f\n", vtos(cg.refdef.vieworg), cg.refdef.viewangles[YAW]);
}

/*
=============
V_Fog_f
=============
*/
static void dump_fog(const player_fog_t *fog)
{
    Com_Printf("(%.3f %.3f %.3f) %f %f\n",
               fog->color[0], fog->color[1], fog->color[2],
               fog->density, fog->sky_factor);
}

static void dump_heightfog(const player_heightfog_t *fog)
{
    Com_Printf("Start  : (%.3f %.3f %.3f) %.f\n",
               fog->start.color[0], fog->start.color[1], fog->start.color[2], fog->start.dist);
    Com_Printf("End    : (%.3f %.3f %.3f) %.f\n",
               fog->end.color[0], fog->end.color[1], fog->end.color[2], fog->end.dist);
    Com_Printf("Density: %f\n", fog->density);
    Com_Printf("Falloff: %f\n", fog->falloff);
}

static void V_Fog_f(void)
{
    int argc = Cmd_Argc();
    float args[5];

    if (argc == 1) {
        if (cg.custom_fog.density || cg.custom_fog.sky_factor) {
            Com_Printf("User set global fog:\n");
            dump_fog(&cg.custom_fog);
            return;
        }
        if (cg.frame.ps.fog.density || cg.frame.ps.fog.sky_factor) {
            Com_Printf("Global fog:\n");
            dump_fog(&cg.frame.ps.fog);
        }
        if (cg.frame.ps.heightfog.density) {
            Com_Printf("Height fog:\n");
            dump_heightfog(&cg.frame.ps.heightfog);
        }
        if (!(cg.frame.ps.fog.density || cg.frame.ps.fog.sky_factor || cg.frame.ps.heightfog.density))
            Com_Printf("No fog.\n");
        return;
    }

    if (argc < 5) {
        Com_Printf("Usage: %s <r g b density> [sky_factor]\n", Cmd_Argv(0));
        return;
    }

    for (int i = 0; i < 5; i++)
        args[i] = Q_clipf(Q_atof(Cmd_Argv(i + 1)), 0, 1);

    cg.custom_fog.color[0]   = args[0];
    cg.custom_fog.color[1]   = args[1];
    cg.custom_fog.color[2]   = args[2];
    cg.custom_fog.density    = args[3];
    cg.custom_fog.sky_factor = args[4];

    cg.refdef.fog = cg.custom_fog;
    cg.refdef.heightfog = (player_heightfog_t){ 0 };
}

static const cmdreg_t v_cmds[] = {
    { "viewpos", V_Viewpos_f },
    { "fog", V_Fog_f },
    { NULL }
};

/*
=============
V_Init
=============
*/
void V_Init(void)
{
    Cmd_Register(v_cmds);

    cl_adjustfov = Cvar_Get("cl_adjustfov", "1", 0);
}

void V_Shutdown(void)
{
    Cmd_Deregister(v_cmds);
}
