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

    cg_custom_fog.color[0]   = args[0];
    cg_custom_fog.color[1]   = args[1];
    cg_custom_fog.color[2]   = args[2];
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
            axis[i] = Q_atof(buf);
        }
    } else
        VectorSet(axis, 0, 0, 1);

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

    trap_SetCompletionOptions(CMPL_CASELESS | CMPL_STRIPQUOTES);
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

typedef struct {
    const char *name;
    void (*func)(void);
    void (*comp)(int firstarg, int argnum);
} vm_cmd_reg_t;

static vm_cmd_reg_t cg_consolecmds[] = {
    // use anytime commands
    { "viewpos", V_Viewpos_f },
    { "fog", V_Fog_f },
    { "sizeup", SCR_SizeUp_f },
    { "sizedown", SCR_SizeDown_f },
    { "sky", SCR_Sky_f },
    { "clearchathud", SCR_ClearChatHUD_f },

    // commands below this separator are never executed during demos or if
    // there is no valid server frame
    { NULL },

    { "cl_weapprev", CG_WeapPrev_f },
    { "cl_weapnext", CG_WeapNext_f },
    { "+wheel", CG_ShowWeaponWheel_f },
    { "-wheel", CG_HideWeaponWheel_f },
    { "+wheel2", CG_ShowPowerupWheel_f },
    { "-wheel2", CG_HidePowerupWheel_f },

    // forward to server commands
    { "players" }, { "score" }, { "help" },
    { "say", NULL, CG_Say_c }, { "say_team", NULL, CG_Say_c },
    { "showsecrets" }, { "showmonsters" }, { "listmonsters" },
    { "target" }, { "spawn" }, { "teleport" },
    { "wave" }, { "kill" }, { "use", NULL, CG_Item_c },
    { "drop", NULL, CG_Item_c }, { "give", NULL, CG_Item_c },
    { "god" }, { "notarget" }, { "noclip" }, { "immortal" },
    { "novisible" }, { "inven" }, { "invuse" }, { "invprev" },
    { "invnext" }, { "invdrop" }, { "invnextw" }, { "invprevw" },
    { "invnextp" }, { "invprevp" }, { "weapnext" }, { "weapprev" },
    { "weaplast" }
};

// Register cgame commands for command completion.
void CG_RegisterCommands(void)
{
    for (int i = 0; i < q_countof(cg_consolecmds); i++)
        if (cg_consolecmds[i].name)
            trap_RegisterCommand(cg_consolecmds[i].name);
}

// Cgame must return true if it handles the command, otherwise it will be
// forwarded to server.
qvm_exported bool CG_ConsoleCommand(void)
{
    char cmd[MAX_QPATH];
    trap_Argv(0, cmd, sizeof(cmd));

    for (int i = 0; i < q_countof(cg_consolecmds); i++) {
        const vm_cmd_reg_t *reg = &cg_consolecmds[i];
        if (!reg->name) {
            if (cgs.demoplayback || !cg.frame)
                break;
            continue;
        }
        if (strcmp(cmd, reg->name))
            continue;
        if (reg->func) {
            reg->func();
            return true;
        }
        break;
    }

    return false;
}

// firstarg is absolute argument index of command being completed.
// argnum is relative argument number to that command.
qvm_exported void CG_CompleteCommand(int firstarg, int argnum)
{
    char cmd[MAX_QPATH];
    trap_Argv(firstarg, cmd, sizeof(cmd));

    for (int i = 0; i < q_countof(cg_consolecmds); i++) {
        const vm_cmd_reg_t *reg = &cg_consolecmds[i];
        if (!reg->name) {
            if (cgs.demoplayback || !cg.frame)
                break;
            continue;
        }
        if (strcmp(cmd, reg->name))
            continue;
        if (reg->comp)
            reg->comp(firstarg, argnum);
        break;
    }
}
