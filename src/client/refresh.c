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

// Main windowed and fullscreen graphics interface module. This module
// is used for both the software and OpenGL rendering versions of the
// Quake refresh engine.


#include "client.h"

// Console variables that we need to access from this module
cvar_t      *vid_geometry;
cvar_t      *vid_fullscreen;

const vid_driver_t  *vid;

#define MODE_GEOMETRY   1
#define MODE_FULLSCREEN 2

static int  mode_changed;

/*
==========================================================================

HELPER FUNCTIONS

==========================================================================
*/

// 640x480
// 640x480+0
// 640x480+0+0
// 640x480-100-100
bool VID_GetGeometry(vrect_t *rc)
{
    unsigned long w, h;
    long x, y;
    char *s;

    // fill in default parameters
    rc->x = 0;
    rc->y = 0;
    rc->width = 640;
    rc->height = 480;

    if (!vid_geometry)
        return false;

    s = vid_geometry->string;
    if (!*s)
        return false;

    w = strtoul(s, &s, 10);
    if (*s != 'x' && *s != 'X') {
        Com_DPrintf("Geometry string is malformed\n");
        return false;
    }
    h = strtoul(s + 1, &s, 10);
    x = y = 0;
    if (*s == '+' || *s == '-') {
        x = strtol(s, &s, 10);
        if (*s == '+' || *s == '-') {
            y = strtol(s, &s, 10);
        }
    }

    // sanity check
    if (w < 320 || w > 8192 || h < 240 || h > 8192) {
        Com_DPrintf("Geometry %lux%lu doesn't look sane\n", w, h);
        return false;
    }

    rc->x = x;
    rc->y = y;
    rc->width = w;
    rc->height = h;

    return true;
}

void VID_SetGeometry(const vrect_t *rc)
{
    char buffer[MAX_QPATH];

    if (!vid_geometry)
        return;

    Q_snprintf(buffer, sizeof(buffer), "%dx%d%+d%+d",
               rc->width, rc->height, rc->x, rc->y);
    Cvar_SetByVar(vid_geometry, buffer, FROM_CODE);
}

void VID_ToggleFullscreen(void)
{
    if (!vid_fullscreen)
        return;

    if (vid_fullscreen->integer)
        Cbuf_AddText(&cmd_buffer, "set vid_fullscreen 0\n");
    else
        Cbuf_AddText(&cmd_buffer, "set vid_fullscreen 1\n");
}

/*
==========================================================================

LOADING / SHUTDOWN

==========================================================================
*/

#ifdef _WIN32
extern const vid_driver_t   vid_win32wgl;
#endif

#if USE_WIN32EGL
extern const vid_driver_t   vid_win32egl;
#endif

#if USE_WAYLAND
extern const vid_driver_t   vid_wayland;
#endif

#if USE_X11
extern const vid_driver_t   vid_x11;
#endif

#if USE_SDL
extern const vid_driver_t   vid_sdl;
#endif

static const vid_driver_t *const vid_drivers[] = {
#ifdef _WIN32
    &vid_win32wgl,
#endif
#if USE_WIN32EGL
    &vid_win32egl,
#endif
#if USE_WAYLAND
    &vid_wayland,
#endif
#if USE_X11
    &vid_x11,
#endif
#if USE_SDL
    &vid_sdl,
#endif
    NULL
};

/*
============
CL_RunResfresh
============
*/
void CL_RunRefresh(void)
{
    if (!cls.ref_initialized) {
        return;
    }

    vid->pump_events();

    if (mode_changed) {
        if ((mode_changed & MODE_FULLSCREEN) ||
            ((mode_changed & MODE_GEOMETRY) && !vid_fullscreen->integer)) {
            vid->set_mode();
        }
        mode_changed = 0;
    }

    if (cvar_modified & CVAR_REFRESH) {
        CL_RestartRefresh(true);
        cvar_modified &= ~CVAR_REFRESH;
    } else if (cvar_modified & CVAR_FILES) {
        CL_RestartRefresh(false);
        cvar_modified &= ~CVAR_FILES;
    }
}

static void vid_geometry_changed(cvar_t *self)
{
    mode_changed |= MODE_GEOMETRY;
}

static void vid_fullscreen_changed(cvar_t *self)
{
    mode_changed |= MODE_FULLSCREEN;
}

static void vid_driver_g(void)
{
    for (int i = 0; vid_drivers[i]; i++)
        Prompt_AddMatch(vid_drivers[i]->name);
}

/*
============
CL_InitRefresh
============
*/
void CL_InitRefresh(void)
{
    int i;

    if (cls.ref_initialized) {
        return;
    }

    Cvar_Get("vid_ref", "gl", CVAR_ROM);

    // Create the video variables so we know how to start the graphics drivers
    cvar_t *vid_driver = Cvar_Get("vid_driver", "", CVAR_REFRESH);
    vid_driver->generator = vid_driver_g;
    vid_fullscreen = Cvar_Get("vid_fullscreen", "1", CVAR_ARCHIVE);
    vid_geometry = Cvar_Get("vid_geometry", VID_GEOMETRY, CVAR_ARCHIVE);

    Com_SetLastError("No available video driver");

    // Try to initialize selected driver first
    bool ok = false;
    for (i = 0; vid_drivers[i]; i++) {
        if (!strcmp(vid_drivers[i]->name, vid_driver->string)) {
            vid = vid_drivers[i];
            ok = R_Init(true);
            break;
        }
    }

    if (!vid_drivers[i] && vid_driver->string[0]) {
        Com_Printf("No such video driver: %s.\n"
                   "Available video drivers: ", vid_driver->string);
        for (int j = 0; vid_drivers[j]; j++) {
            if (j)
                Com_Printf(", ");
            Com_Printf("%s", vid_drivers[j]->name);
        }
        Com_Printf(".\n");
    }

    // Fall back to other available drivers
    if (!ok) {
        int tried = i;
        for (i = 0; vid_drivers[i]; i++) {
            if (i == tried || !vid_drivers[i]->probe || !vid_drivers[i]->probe())
                continue;
            vid = vid_drivers[i];
            if ((ok = R_Init(true)))
                break;
        }
        Cvar_Reset(vid_driver);
    }

    if (!ok)
        Com_Error(ERR_FATAL, "Couldn't initialize refresh: %s", Com_GetLastError());

    vid->set_mode();

    cls.ref_initialized = true;

    vid_geometry->changed = vid_geometry_changed;
    vid_fullscreen->changed = vid_fullscreen_changed;

    mode_changed = 0;

    // Initialize the rest of graphics subsystems
    SCR_Init();
    UI_Init();

    SCR_RegisterMedia();
    Con_RegisterMedia();

    cvar_modified &= ~(CVAR_FILES | CVAR_REFRESH);
}

/*
============
CL_ShutdownRefresh
============
*/
void CL_ShutdownRefresh(void)
{
    if (!cls.ref_initialized) {
        return;
    }

    // Shutdown the rest of graphics subsystems
    SCR_Shutdown();
    UI_Shutdown();

    vid_geometry->changed = NULL;
    vid_fullscreen->changed = NULL;

    R_Shutdown(true);

    vid = NULL;

    cls.ref_initialized = false;

    // no longer active
    cls.active = ACT_MINIMIZED;

    Z_LeakTest(TAG_RENDERER);
}
