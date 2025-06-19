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

cgame_state_t   cg;
cgame_static_t  cgs;

#ifndef Q2_VM
const cgame_import_t    *cgi;
#endif

centity_t   cl_entities[MAX_EDICTS];

vm_cvar_t   cl_noskins;
vm_cvar_t   cl_footsteps;
vm_cvar_t   cl_predict;
vm_cvar_t   cl_gun;
vm_cvar_t   cl_gunalpha;
vm_cvar_t   cl_gunfov;
vm_cvar_t   cl_gun_x;
vm_cvar_t   cl_gun_y;
vm_cvar_t   cl_gun_z;
vm_cvar_t   cl_kickangles;
vm_cvar_t   cl_rollhack;
vm_cvar_t   cl_noglow;
vm_cvar_t   cl_nobob;
vm_cvar_t   cl_nolerp;
#if USE_DEBUG
vm_cvar_t   cl_showmiss;
vm_cvar_t   cl_showclamp;
vm_cvar_t   cl_showstep;
#endif
vm_cvar_t   cl_thirdperson;
vm_cvar_t   cl_thirdperson_angle;
vm_cvar_t   cl_thirdperson_range;
vm_cvar_t   cl_gibs;
vm_cvar_t   cl_flares;
vm_cvar_t   cl_vwep;
vm_cvar_t   cl_disable_particles;
vm_cvar_t   cl_disable_explosions;
vm_cvar_t   cl_dlight_hacks;
vm_cvar_t   cl_smooth_explosions;
vm_cvar_t   cl_chat_notify;
vm_cvar_t   cl_chat_sound;
vm_cvar_t   cl_chat_filter;
vm_cvar_t   cl_adjustfov;
vm_cvar_t   cl_lerp_lightstyles;
vm_cvar_t   cl_muzzlelight_time;
vm_cvar_t   cl_muzzleflashes;
vm_cvar_t   cl_hit_markers;
vm_cvar_t   cl_railtrail_type;
vm_cvar_t   cl_railtrail_time;
vm_cvar_t   cl_railcore_color;
vm_cvar_t   cl_railcore_width;
vm_cvar_t   cl_railspiral_color;
vm_cvar_t   cl_railspiral_radius;
vm_cvar_t   cl_paused;
vm_cvar_t   sv_paused;
vm_cvar_t   com_timedemo;
vm_cvar_t   s_ambient;
vm_cvar_t   info_hand;
vm_cvar_t   info_fov;
vm_cvar_t   info_uf;

static vm_cvar_reg_t cg_cvars[] = {
    VM_CVAR(cl_gun, "1", 0),
    VM_CVAR(cl_gunalpha, "1", 0),
    VM_CVAR(cl_gunfov, "90", 0),
    VM_CVAR(cl_gun_x, "0", 0),
    VM_CVAR(cl_gun_y, "0", 0),
    VM_CVAR(cl_gun_z, "0", 0),
    VM_CVAR(cl_footsteps, "1", 0),
    VM_CVAR(cl_noskins, "0", 0),
    VM_CVAR(cl_predict, "1", 0),
    VM_CVAR(cl_kickangles, "1", CVAR_CHEAT),
    VM_CVAR(cl_rollhack, "1", 0),
    VM_CVAR(cl_noglow, "0", 0),
    VM_CVAR(cl_nobob, "0", 0),
    VM_CVAR(cl_nolerp, "0", 0),
#if USE_DEBUG
    VM_CVAR(cl_showmiss, "0", 0),
    VM_CVAR(cl_showclamp, "0", 0),
    VM_CVAR(cl_showstep, "0", 0),
#endif
    VM_CVAR(cl_thirdperson, "0", CVAR_CHEAT),
    VM_CVAR(cl_thirdperson_angle, "0", 0),
    VM_CVAR(cl_thirdperson_range, "60", 0),
    VM_CVAR(cl_disable_particles, "0", 0),
    VM_CVAR(cl_disable_explosions, "0", 0),
    VM_CVAR(cl_dlight_hacks, "0", 0),
    VM_CVAR(cl_smooth_explosions, "1", 0),
    VM_CVAR(cl_gibs, "1", 0),
    VM_CVAR(cl_flares, "1", 0),
    VM_CVAR(cl_vwep, "1", CVAR_ARCHIVE),
    VM_CVAR(cl_chat_notify, "1", 0),
    VM_CVAR(cl_chat_sound, "1", 0),
    VM_CVAR(cl_chat_filter, "0", 0),
    VM_CVAR(cl_adjustfov, "1", 0),
    VM_CVAR(cl_lerp_lightstyles, "0", 0),
    VM_CVAR(cl_muzzlelight_time, "16", 0),
    VM_CVAR(cl_muzzleflashes, "1", 0),
    VM_CVAR(cl_hit_markers, "2", 0),
    VM_CVAR(cl_railtrail_type, "0", 0),
    VM_CVAR(cl_railtrail_time, "1.0", 0),
    VM_CVAR(cl_railcore_color, "red", 0),
    VM_CVAR(cl_railcore_width, "2", 0),
    VM_CVAR(cl_railspiral_color, "blue", 0),
    VM_CVAR(cl_railspiral_radius, "3", 0),
    VM_CVAR(cl_paused, "0", CVAR_ROM),
    VM_CVAR(sv_paused, "0", CVAR_ROM),
    VM_CVAR(s_ambient, "1", 0),

    { &com_timedemo, "timedemo", "0", CVAR_CHEAT },
    { &info_hand, "hand", "0", CVAR_USERINFO | CVAR_ARCHIVE },
    { &info_fov, "fov", "90", CVAR_USERINFO | CVAR_ARCHIVE },
    { &info_uf, "uf", "", CVAR_USERINFO },
};

qvm_exported void CG_Init(void)
{
    memset(&cg, 0, sizeof(cg));

    // seed RNG
    Q_srand(trap_RealTime());

    for (int i = 0; i < q_countof(cg_cvars); i++) {
        const vm_cvar_reg_t *reg = &cg_cvars[i];
        trap_Cvar_Register(reg->var, reg->name, reg->default_string, reg->flags);
    }

    cgs.demoplayback = trap_GetDemoInfo(NULL);

    CG_UpdateConfigstring(CS_MAXCLIENTS);
    CG_UpdateConfigstring(CS_AIRACCEL);
    CG_UpdateConfigstring(CS_STATUSBAR);

    SCR_Init();
    CG_InitEffects();
    CG_RegisterVWepModels();
    CG_PrepRefresh();
    CG_RegisterSounds();
}

qvm_exported void CG_Shutdown(void)
{
}

qvm_exported bool CG_KeyEvent(unsigned key, bool down)
{
    if (key != K_ESCAPE)
        return false;
    if (cgs.demoplayback)
        return false;
    if (!cg.frame)
        return false;
    if (!(cg.frame->ps.stats[STAT_LAYOUTS] & (LAYOUTS_LAYOUT | LAYOUTS_INVENTORY | LAYOUTS_HELP)))
        return false;

    trap_ClientCommand("putaway");
    return true;
}

qvm_exported void CG_CharEvent(unsigned key)
{
}

qvm_exported void CG_MouseEvent(int x, int y)
{
}

/*
=================
GetCGameAPI

Returns a pointer to the structure with all entry points
and global variables
=================
*/
#ifndef Q2_VM
q_exported const cgame_export_t *GetCGameAPI(const cgame_import_t *import)
{
    static const cgame_export_t cge = {
        .apiversion = CGAME_API_VERSION,
        .structsize = sizeof(cge),

        .Init = CG_Init,
        .Shutdown = CG_Shutdown,
        .ConsoleCommand = CG_ConsoleCommand,
        .ServerCommand = CG_ServerCommand,
        .UpdateConfigstring = CG_UpdateConfigstring,
        .DrawActiveFrame = CG_DrawActiveFrame,
        .ModeChanged = CG_ModeChanged,
        .KeyEvent = CG_KeyEvent,
        .CharEvent = CG_CharEvent,
        .MouseEvent = CG_MouseEvent,
    };

    cgi = import;
    return &cge;
}
#endif

#ifndef GAME_HARD_LINKED
// this is only here so the functions in q_shared.c can link
void Com_LPrintf(print_type_t type, const char *fmt, ...)
{
    va_list     argptr;
    char        text[MAX_STRING_CHARS];

    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    trap_Print(type, text);
}

void Com_Error(error_type_t type, const char *fmt, ...)
{
    va_list     argptr;
    char        text[MAX_STRING_CHARS];

    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    trap_Error(text);
}
#endif
