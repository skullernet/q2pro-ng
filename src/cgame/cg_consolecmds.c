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
    Com_Printf("%s : %.f\n", vtos(cg.refdef.vieworg), cg.refdef.viewangles.yaw);
}

/*
=============
V_Fog_f
=============
*/
static void dump_fog(const player_fog_t *fog)
{
    Com_Printf("(%.3f %.3f %.3f) %f %f\n",
               fog->color.r, fog->color.g, fog->color.b,
               fog->density, fog->sky_factor);
}

static void dump_heightfog(const player_heightfog_t *fog)
{
    Com_Printf("Start  : (%.3f %.3f %.3f) %.f\n",
               fog->start.color.r, fog->start.color.g, fog->start.color.b, fog->start.dist);
    Com_Printf("End    : (%.3f %.3f %.3f) %.f\n",
               fog->end.color.r, fog->end.color.g, fog->end.color.b, fog->end.dist);
    Com_Printf("Density: %f\n", fog->density);
    Com_Printf("Falloff: %f\n", fog->falloff);
}

static void V_Fog_f(void)
{
    int argc = trap_Argc();
    float args[5];

    if (argc == 1) {
        if (cg_custom_fog.density || cg_custom_fog.sky_factor) {
            Com_Printf("User set global fog:\n");
            dump_fog(&cg_custom_fog);
            return;
        }
        if (!cg.frame) {
            Com_Printf("No fog.\n");
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

    cg_custom_fog.color.r    = args[0];
    cg_custom_fog.color.g    = args[1];
    cg_custom_fog.color.b    = args[2];
    cg_custom_fog.density    = args[3];
    cg_custom_fog.sky_factor = args[4];

    cg.refdef.fog = cg_custom_fog;
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
    trap_Cvar_Set("viewsize", va("%d", scr_viewsize.integer + 10));
}

/*
=================
SCR_SizeDown_f

Keybinding command
=================
*/
static void SCR_SizeDown_f(void)
{
    trap_Cvar_Set("viewsize", va("%d", scr_viewsize.integer - 10));
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
            axis.xyz[i] = Q_atof(buf);
        }
    } else
        axis = Vec3(0, 0, 1);

    trap_R_SetSky(name, rotate, true, axis);
}

static void CG_Say_c(int firstarg, int argnum)
{
    for (int i = 0; i < MAX_CLIENTS; i++) {
        char buffer[MAX_CLIENT_NAME];
        Q_strlcpy(buffer, cgs.clientinfo[i].name, sizeof(buffer));
        if (COM_strclr(buffer))
            trap_AddCommandCompletion(buffer);
    }
}

static void CG_Item_c(int firstarg, int argnum)
{
    if (argnum != 1)
        return;

    trap_SetCompletionOptions(CMPL_CASELESS | CMPL_CHECKDUPS | CMPL_STRIPQUOTES);
    for (int i = 0; i < MAX_ITEMS; i++) {
        char buffer[MAX_QPATH];
        if (trap_GetConfigstring(CS_ITEMS + i, buffer, sizeof(buffer)))
            trap_AddCommandCompletion(buffer);
    }
}

static bool CG_CanSelectWeapon(int index)
{
    int owned = cg.frame->ps.stats[STAT_WEAPONS_OWNED];
    if (!(owned & BIT(index)))
        return false;

    const cg_wheel_item_t *weap = &cgs.wheel.weapons[index];
    if (weap->ammo < MAX_AMMO) {
        int ammo = cg.frame->ps.ammo[weap->ammo];
        if (ammo < weap->quantity)
            return false;
    }

    return true;
}

static void CG_WeapPrev_f(void)
{
    int weapon = cg.frame->ps.stats[STAT_ACTIVE_WHEEL_WEAPON];
    if (weapon < 1 || weapon > cgs.wheel.num_weapons) {
        trap_ClientCommand("weapprev");
        return;
    }

    if (!cg.weapon_select) {
        trap_InsertCommandString("+holster");
        cg.weapon_select = weapon;
    } else {
        weapon = cg.weapon_select;
    }

    while (1) {
        if (cg.weapon_select > 1)
            cg.weapon_select--;
        else
            cg.weapon_select = cgs.wheel.num_weapons;
        if (cg.weapon_select == weapon)
            break;
        if (CG_CanSelectWeapon(cg.weapon_select - 1))
            break;
    }

    cg.weapon_select_time = cg.time + Q_clip(cg_weapon_select_msec.integer, 100, 2000);
}

static void CG_WeapNext_f(void)
{
    int weapon = cg.frame->ps.stats[STAT_ACTIVE_WHEEL_WEAPON];
    if (weapon < 1 || weapon > cgs.wheel.num_weapons) {
        trap_ClientCommand("weapnext");
        return;
    }

    if (!cg.weapon_select) {
        trap_InsertCommandString("+holster");
        cg.weapon_select = weapon;
    } else {
        weapon = cg.weapon_select;
    }

    while (1) {
        if (cg.weapon_select < cgs.wheel.num_weapons)
            cg.weapon_select++;
        else
            cg.weapon_select = 1;
        if (cg.weapon_select == weapon)
            break;
        if (CG_CanSelectWeapon(cg.weapon_select - 1))
            break;
    }

    cg.weapon_select_time = cg.time + Q_clip(cg_weapon_select_msec.integer, 100, 2000);
}

static void CG_TestModel_f(void)
{
    cg.test_model = (entity_t){ 0 };

    if (trap_Argc() < 2)
        return;

    char buf[MAX_QPATH];
    trap_Argv(1, buf, sizeof(buf));

    cg.test_model.model = trap_R_RegisterModel(buf);
    if (!cg.test_model.model) {
        Com_Printf("Can't register model %s\n", buf);
        return;
    }

    if (trap_Argc() > 2) {
        trap_Argv(2, buf, sizeof(buf));
        cg.test_model.frame = Q_atoi(buf);
    }

    if (trap_Argc() > 3) {
        trap_Argv(3, buf, sizeof(buf));
        cg.test_model.scale = Q_atof(buf);
    }

    cg.test_model.origin = Vec3_MA(cg.refdef.vieworg, 100, cg.v_forward);
    cg.test_model.angles = Vec3(0, cg.refdef.viewangles.yaw + 180.0f, 0);
}

static void CG_TestModelNextFrame_f(void)
{
    if (!cg.test_model.model)
        return;
    cg.test_model.frame++;
    Com_Printf("frame %u\n", cg.test_model.frame);
}

static void CG_TestModelPrevFrame_f(void)
{
    if (!cg.test_model.model)
        return;
    if (cg.test_model.frame > 0)
        cg.test_model.frame--;
    Com_Printf("frame %u\n", cg.test_model.frame);
}

static void CG_TestModelNextSkin_f(void)
{
    if (!cg.test_model.model)
        return;
    cg.test_model.skinnum++;
    Com_Printf("skin %u\n", cg.test_model.skinnum);
}

static void CG_TestModelPrevSkin_f(void)
{
    if (!cg.test_model.model)
        return;
    if (cg.test_model.skinnum > 0)
        cg.test_model.skinnum--;
    Com_Printf("skin %u\n", cg.test_model.skinnum);
}

typedef enum {
    NEED_FRAME = BIT(0),
    NO_DEMO    = BIT(1),
} cg_cmdflags_t;

typedef struct {
    const char *name;
    void (*func)(void);
    cg_cmdflags_t flags;
    void (*comp)(int firstarg, int argnum);
} cg_consolecmd_t;

static cg_consolecmd_t cg_consolecmds[] = {
    // use anytime commands
    { "viewpos", V_Viewpos_f },
    { "fog", V_Fog_f },
    { "sizeup", SCR_SizeUp_f },
    { "sizedown", SCR_SizeDown_f },
    { "sky", SCR_Sky_f },
    { "clearchathud", SCR_ClearChatHUD_f },

    { "testmodel", CG_TestModel_f },
    { "nextframe", CG_TestModelNextFrame_f },
    { "prevframe", CG_TestModelPrevFrame_f },
    { "nextskin", CG_TestModelNextSkin_f },
    { "prevskin", CG_TestModelPrevSkin_f },

    { "cl_weapprev", CG_WeapPrev_f, NEED_FRAME | NO_DEMO },
    { "cl_weapnext", CG_WeapNext_f, NEED_FRAME | NO_DEMO },
    { "+wheel", CG_ShowWeaponWheel_f, NEED_FRAME | NO_DEMO },
    { "-wheel", CG_HideWeaponWheel_f, NEED_FRAME | NO_DEMO },
    { "+wheel2", CG_ShowPowerupWheel_f, NEED_FRAME | NO_DEMO },
    { "-wheel2", CG_HidePowerupWheel_f, NEED_FRAME | NO_DEMO },

    // forward to server commands. these are only here for
    // command completion if *not* running a local server.
    { "drop", .comp = CG_Item_c },
    { "give", .comp = CG_Item_c },
    { "god" },
    { "help" },
    { "immortal" },
    { "invdrop" },
    { "inven" },
    { "invnext" },
    { "invprev" },
    { "invuse" },
    { "kill" },
    { "noclip" },
    { "notarget" },
    { "novisible" },
    { "players" },
    { "resurrect" },
    { "say", .comp = CG_Say_c },
    { "say_team", .comp = CG_Say_c },
    { "score" },
    { "use", .comp = CG_Item_c },
    { "wave" },
    { "weaplast" },
    { "weapnext" },
    { "weapprev" },
};

static const cg_consolecmd_t *CG_FindConsoleCommand(const char *name)
{
    for (int i = 0; i < q_countof(cg_consolecmds); i++)
        if (strcmp(name, cg_consolecmds[i].name) == 0)
            return &cg_consolecmds[i];
    return NULL;
}

static bool CG_CommandAllowed(const cg_consolecmd_t *cmd)
{
    if (cmd->flags & NEED_FRAME && !cg.frame)
        return false;
    if (cmd->flags & NO_DEMO && cgs.demoplayback)
        return false;
    if (!cmd->func && (sv_running.integer || cgs.demoplayback))
        return false;
    return true;
}

/*
=================
CG_ConsoleCommand

Returns true if cgame handles the command, otherwise it will be
forwarded to server.
=================
*/
qvm_exported bool CG_ConsoleCommand(void)
{
    char buf[MAX_QPATH];
    trap_Argv(0, buf, sizeof(buf));

    const cg_consolecmd_t *cmd = CG_FindConsoleCommand(buf);
    if (!cmd || !cmd->func)
        return false;
    if (CG_CommandAllowed(cmd))
        cmd->func();
    return true;
}

/*
=================
CG_CompleteCommand

Completes command name or argument for cgame recognized command.
=================
*/
qvm_exported void CG_CompleteCommand(int firstarg, int argnum)
{
    if (argnum == 0) {
        for (int i = 0; i < q_countof(cg_consolecmds); i++) {
            const cg_consolecmd_t *cmd = &cg_consolecmds[i];
            if (CG_CommandAllowed(cmd))
                trap_AddCommandCompletion(cmd->name);
        }
        return;
    }

    char buf[MAX_QPATH];
    trap_Argv(firstarg, buf, sizeof(buf));

    const cg_consolecmd_t *cmd = CG_FindConsoleCommand(buf);
    if (cmd && cmd->comp && CG_CommandAllowed(cmd))
        cmd->comp(firstarg, argnum);
}
