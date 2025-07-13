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

player_fog_t    cg_custom_fog;

#ifndef Q2_VM
const cgame_import_t    *cgi;
#endif

centity_t   cg_entities[MAX_EDICTS];

vm_cvar_t   cg_noskins;
vm_cvar_t   cg_footsteps;
vm_cvar_t   cg_predict;
vm_cvar_t   cg_gun;
vm_cvar_t   cg_gunalpha;
vm_cvar_t   cg_gunfov;
vm_cvar_t   cg_gun_x;
vm_cvar_t   cg_gun_y;
vm_cvar_t   cg_gun_z;
vm_cvar_t   cg_run_pitch;
vm_cvar_t   cg_run_roll;
vm_cvar_t   cg_bob_up;
vm_cvar_t   cg_bob_pitch;
vm_cvar_t   cg_bob_roll;
vm_cvar_t   cg_skip_view_modifiers;
vm_cvar_t   cg_rollhack;
vm_cvar_t   cg_noglow;
vm_cvar_t   cg_nobob;
vm_cvar_t   cg_nolerp;
#if USE_DEBUG
vm_cvar_t   cg_showmiss;
vm_cvar_t   cg_showclamp;
vm_cvar_t   cg_showstep;
#endif
vm_cvar_t   cg_thirdperson;
vm_cvar_t   cg_thirdperson_angle;
vm_cvar_t   cg_thirdperson_range;
vm_cvar_t   cg_gibs;
vm_cvar_t   cg_flares;
vm_cvar_t   cg_vwep;
vm_cvar_t   cg_disable_particles;
vm_cvar_t   cg_disable_explosions;
vm_cvar_t   cg_dlight_hacks;
vm_cvar_t   cg_smooth_explosions;
vm_cvar_t   cg_chat_notify;
vm_cvar_t   cg_chat_sound;
vm_cvar_t   cg_chat_filter;
vm_cvar_t   cg_adjustfov;
vm_cvar_t   cg_lerp_lightstyles;
vm_cvar_t   cg_muzzlelight_time;
vm_cvar_t   cg_muzzleflashes;
vm_cvar_t   cg_hit_markers;
vm_cvar_t   cg_railtrail_type;
vm_cvar_t   cg_railtrail_time;
vm_cvar_t   cg_railcore_color;
vm_cvar_t   cg_railcore_width;
vm_cvar_t   cg_railspiral_color;
vm_cvar_t   cg_railspiral_radius;
vm_cvar_t   cl_paused;
vm_cvar_t   sv_paused;
vm_cvar_t   com_timedemo;
vm_cvar_t   s_ambient;
vm_cvar_t   info_hand;
vm_cvar_t   info_fov;
vm_cvar_t   info_uf;

static const vm_cvar_reg_t cg_cvars[] = {
    VM_CVAR(cg_gun, "1", 0),
    VM_CVAR(cg_gunalpha, "1", 0),
    VM_CVAR(cg_gunfov, "90", 0),
    VM_CVAR(cg_gun_x, "0", 0),
    VM_CVAR(cg_gun_y, "0", 0),
    VM_CVAR(cg_gun_z, "0", 0),
    VM_CVAR(cg_run_pitch, "0.002", 0),
    VM_CVAR(cg_run_roll, "0.005", 0),
    VM_CVAR(cg_bob_up, "0.005", 0),
    VM_CVAR(cg_bob_pitch, "0.002", 0),
    VM_CVAR(cg_bob_roll, "0.002", 0),
    VM_CVAR(cg_skip_view_modifiers, "0", CVAR_CHEAT),
    VM_CVAR(cg_footsteps, "1", 0),
    VM_CVAR(cg_noskins, "0", 0),
    VM_CVAR(cg_predict, "1", 0),
    VM_CVAR(cg_rollhack, "1", 0),
    VM_CVAR(cg_noglow, "0", 0),
    VM_CVAR(cg_nobob, "0", 0),
    VM_CVAR(cg_nolerp, "0", 0),
#if USE_DEBUG
    VM_CVAR(cg_showmiss, "0", 0),
    VM_CVAR(cg_showclamp, "0", 0),
    VM_CVAR(cg_showstep, "0", 0),
#endif
    VM_CVAR(cg_thirdperson, "0", CVAR_CHEAT),
    VM_CVAR(cg_thirdperson_angle, "0", 0),
    VM_CVAR(cg_thirdperson_range, "60", 0),
    VM_CVAR(cg_disable_particles, "0", 0),
    VM_CVAR(cg_disable_explosions, "0", 0),
    VM_CVAR(cg_dlight_hacks, "0", 0),
    VM_CVAR(cg_smooth_explosions, "1", 0),
    VM_CVAR(cg_gibs, "1", 0),
    VM_CVAR(cg_flares, "1", 0),
    VM_CVAR(cg_vwep, "1", CVAR_ARCHIVE),
    VM_CVAR(cg_chat_notify, "1", 0),
    VM_CVAR(cg_chat_sound, "1", 0),
    VM_CVAR(cg_chat_filter, "0", 0),
    VM_CVAR(cg_adjustfov, "1", 0),
    VM_CVAR(cg_lerp_lightstyles, "0", 0),
    VM_CVAR(cg_muzzlelight_time, "16", 0),
    VM_CVAR(cg_muzzleflashes, "1", 0),
    VM_CVAR(cg_hit_markers, "2", 0),
    VM_CVAR(cg_railtrail_type, "0", 0),
    VM_CVAR(cg_railtrail_time, "1.0", 0),
    VM_CVAR(cg_railcore_color, "red", 0),
    VM_CVAR(cg_railcore_width, "2", 0),
    VM_CVAR(cg_railspiral_color, "blue", 0),
    VM_CVAR(cg_railspiral_radius, "3", 0),
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
    // seed RNG
    Q_srand(trap_RealTime());

    for (int i = 0; i < q_countof(cg_cvars); i++) {
        const vm_cvar_reg_t *reg = &cg_cvars[i];
        trap_Cvar_Register(reg->var, reg->name, reg->default_string, reg->flags);
    }

    SCR_Init();
    CG_InitEffects();
}

qvm_exported void CG_Shutdown(void)
{
}

// Called after demo seek
qvm_exported void CG_ClearState(void)
{
    memset(&cg, 0, sizeof(cg));

    CG_ClearEffects();
    CG_ClearTEnts();

    // refresh current frame
    cg.serverframe = trap_GetServerFrameNumber() - 1;
}

// Called before entering a new level, or after subsystem restart
qvm_exported void CG_PrepRefresh(bool demoplayback)
{
    memset(&cgs, 0, sizeof(cgs));
    cgs.demoplayback = demoplayback;

    CG_UpdateConfigstring(CS_MAXCLIENTS);
    CG_UpdateConfigstring(CS_AIRACCEL);
    CG_UpdateConfigstring(CS_STATUSBAR);
    CG_UpdateConfigstring(CONFIG_PHYSICS_FLAGS);

    SCR_RegisterMedia();
    CG_RegisterMedia();

    CG_ClearState();
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
        .PrepRefresh = CG_PrepRefresh,
        .ClearState = CG_ClearState,
        .ConsoleCommand = CG_ConsoleCommand,
        .ServerCommand = CG_ServerCommand,
        .UpdateConfigstring = CG_UpdateConfigstring,
        .DrawFrame = CG_DrawFrame,
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
