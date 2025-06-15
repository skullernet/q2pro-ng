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

#include "cg_local.h"

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
    int argc = trap_Argc();
    float args[5];

    if (argc == 1) {
        if (cg.custom_fog.density || cg.custom_fog.sky_factor) {
            Com_Printf("User set global fog:\n");
            dump_fog(&cg.custom_fog);
            return;
        }
        if (cg.frame->ps.fog.density || cg.frame->ps.fog.sky_factor) {
            Com_Printf("Global fog:\n");
            dump_fog(&cg.frame->ps.fog);
        }
        if (cg.frame->ps.heightfog.density) {
            Com_Printf("Height fog:\n");
            dump_heightfog(&cg.frame->ps.heightfog);
        }
        if (!(cg.frame->ps.fog.density || cg.frame->ps.fog.sky_factor || cg.frame->ps.heightfog.density))
            Com_Printf("No fog.\n");
        return;
    }

    if (argc < 5) {
        Com_Printf("Usage: fog <r g b density> [sky_factor]\n");
        return;
    }

    for (int i = 0; i < 5; i++) {
        char buf[MAX_QPATH];
        trap_Argv(i + 1, buf, sizeof(buf));
        args[i] = Q_clipf(Q_atof(buf), 0, 1);
    }

    cg.custom_fog.color[0]   = args[0];
    cg.custom_fog.color[1]   = args[1];
    cg.custom_fog.color[2]   = args[2];
    cg.custom_fog.density    = args[3];
    cg.custom_fog.sky_factor = args[4];

    cg.refdef.fog = cg.custom_fog;
    cg.refdef.heightfog = (player_heightfog_t){ 0 };
}

/*
=================
SCR_SizeUp_f

Keybinding command
=================
*/
static void SCR_SizeUp_f(void)
{
    //trap_Cvar_Set("viewsize", va("%d", scr_viewsize.integer + 10));
}

/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
static void SCR_SizeDown_f(void)
{
    //trap_Cvar_Set("viewsize", va("%d", scr_viewsize.integer - 10));
}

/*
=================
SCR_Sky_f

Set a specific sky and rotation speed. If empty sky name is provided, falls
back to server defaults.
=================
*/
static void SCR_Sky_f(void)
{
    char    name[MAX_QPATH], buf[MAX_QPATH];
    float   rotate;
    vec3_t  axis;
    int     argc = trap_Argc();

    if (argc < 2) {
        Com_Printf("Usage: sky <basename> [rotate] [axis x y z]\n");
        return;
    }

    trap_Argv(1, name, sizeof(name));
    if (!name[0]) {
        CG_SetSky();
        return;
    }

    if (argc > 2) {
        trap_Argv(2, buf, sizeof(buf));
        rotate = Q_atof(buf);
    } else
        rotate = 0;

    if (argc == 6) {
        for (int i = 0; i < 3; i++) {
            trap_Argv(3 + i, buf, sizeof(buf));
            axis[i] = Q_atof(buf);
        }
    } else
        VectorSet(axis, 0, 0, 1);

    trap_R_SetSky(name, rotate, true, axis);
}

typedef struct {
    const char *name;
    void (*func)(void);
} vm_cmd_reg_t;

static vm_cmd_reg_t cg_consolecmds[] = {
    { "viewpos", V_Viewpos_f },
    { "fog", V_Fog_f },
    { "sizeup", SCR_SizeUp_f },
    { "sizedown", SCR_SizeDown_f },
    { "sky", SCR_Sky_f },
    { "clearchathud", SCR_ClearChatHUD_f },
};

qvm_exported bool CG_ConsoleCommand(void)
{
    char cmd[MAX_QPATH];
    trap_Argv(0, cmd, sizeof(cmd));

    for (int i = 0; i < q_countof(cg_consolecmds); i++) {
        if (!strcmp(cmd, cg_consolecmds[i].name)) {
            cg_consolecmds[i].func();
            return true;
        }
    }

    return false;
}
