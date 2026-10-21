/*
Copyright (C) 2013 Andrey Nazarov

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

//
// video.c
//

#include "shared/shared.h"
#include "common/cvar.h"
#include "common/common.h"
#include "client/client.h"
#include "client/input.h"
#include "client/keys.h"
#include "client/video.h"
#include "refresh/refresh.h"
#include "system/system.h"
#include "../res/q2pro.xbm"
#include <SDL3/SDL.h>

static struct {
    SDL_Window      *window;
    SDL_GLContext   context;
    vidFlags_t      flags;
} sdl;

/*
===============================================================================

OPENGL STUFF

===============================================================================
*/

static void set_gl_attributes(void)
{
    r_opengl_config_t cfg = R_GetGLConfig();

    int colorbits = cfg.colorbits > 16 ? 8 : 5;
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, colorbits);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, colorbits);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, colorbits);

    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, cfg.depthbits);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, cfg.stencilbits);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

    if (cfg.multisamples) {
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 1);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, cfg.multisamples);
    }

    if (cfg.debug)
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);

    if (cfg.profile) {
        if (cfg.profile == QGL_PROFILE_ES)
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        else
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, cfg.major_ver);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, cfg.minor_ver);
    }
}

static void *get_proc_addr(const char *sym)
{
    return SDL_GL_GetProcAddress(sym);
}

static void swap_buffers(void)
{
    SDL_GL_SwapWindow(sdl.window);
}

static void swap_interval(int val)
{
    if (!SDL_GL_SetSwapInterval(val))
        Com_EPrintf("Couldn't set swap interval %d: %s\n", val, SDL_GetError());
}

/*
===============================================================================

VIDEO

===============================================================================
*/

static void mode_changed(void)
{
    int width, height;
    SDL_GetWindowSizeInPixels(sdl.window, &width, &height);

    Uint32 flags = SDL_GetWindowFlags(sdl.window);
    if (flags & SDL_WINDOW_FULLSCREEN)
        sdl.flags |= QVF_FULLSCREEN;
    else
        sdl.flags &= ~QVF_FULLSCREEN;
    Cvar_SetInteger(vid_fullscreen, !!(flags & SDL_WINDOW_FULLSCREEN), FROM_CODE);

    R_ModeChanged(width, height, sdl.flags);
    SCR_ModeChanged();
}

static void set_mode(void)
{
    vrect_t rc;

    if (!vid_fullscreen->integer && VID_GetGeometry(&rc)) {
        SDL_SetWindowSize(sdl.window, rc.width, rc.height);
        SDL_SetWindowPosition(sdl.window, rc.x, rc.y);
    }

    SDL_SetWindowFullscreen(sdl.window, vid_fullscreen->integer);
}

static void fatal_shutdown(void)
{
    SDL_SetWindowMouseGrab(sdl.window, false);
    SDL_SetWindowRelativeMouseMode(sdl.window, false);
    SDL_ShowCursor();
    SDL_Quit();
}

static char *get_selection_data(void)
{
    char *text = SDL_GetPrimarySelectionText();
    char *copy = UTF8_TranslitString(text);
    SDL_free(text);
    return copy;
}

static char *get_clipboard_data(void)
{
    char *text = SDL_GetClipboardText();
    char *copy = UTF8_TranslitString(text);
    SDL_free(text);
    return copy;
}

static void set_clipboard_data(const char *data)
{
    SDL_SetClipboardText(data);
}

static bool my_event_filter(void *userdata, SDL_Event *event)
{
    // SDL uses relative time, we need absolute
    event->common.timestamp = Sys_Milliseconds();
    return true;
}

static int get_dpi_scale(void)
{
    return SDL_GetWindowDisplayScale(sdl.window) + 0.5f;
}

static void shutdown(void)
{
    if (sdl.context)
        SDL_GL_DestroyContext(sdl.context);

    if (sdl.window)
        SDL_DestroyWindow(sdl.window);

    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    memset(&sdl, 0, sizeof(sdl));
}

static bool create_window_and_context(const vrect_t *rc)
{
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_OPENGL | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (vid_fullscreen->integer)
        flags |= SDL_WINDOW_FULLSCREEN;

    sdl.window = SDL_CreateWindow(PRODUCT, rc->width, rc->height, flags);
    if (!sdl.window) {
        Com_EPrintf("Couldn't create SDL window: %s\n", SDL_GetError());
        return false;
    }

    sdl.context = SDL_GL_CreateContext(sdl.window);
    if (!sdl.context) {
        Com_EPrintf("Couldn't create OpenGL context: %s\n", SDL_GetError());
        SDL_DestroyWindow(sdl.window);
        sdl.window = NULL;
        return false;
    }

    return true;
}

static bool init(void)
{
    vrect_t rc;

    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        Com_EPrintf("Couldn't initialize SDL video: %s\n", SDL_GetError());
        return false;
    }

    set_gl_attributes();

    SDL_SetEventFilter(my_event_filter, NULL);

    if (!VID_GetGeometry(&rc)) {
        rc.x = SDL_WINDOWPOS_UNDEFINED;
        rc.y = SDL_WINDOWPOS_UNDEFINED;
    }

    if (!create_window_and_context(&rc)) {
        Com_Printf("Falling back to failsafe config\n");
        SDL_GL_ResetAttributes();
        if (!create_window_and_context(&rc)) {
            shutdown();
            return false;
        }
    }

    SDL_SetWindowMinimumSize(sdl.window, 320, 240);

    SDL_Surface *icon = SDL_CreateSurfaceFrom(q2icon_width, q2icon_height, SDL_PIXELFORMAT_INDEX1LSB,
                                              q2icon_bits, q2icon_width / 8);
    if (icon) {
        SDL_Color colors[2] = {
            { 255, 255, 255 },
            {   0, 128, 128 }
        };
        SDL_SetPaletteColors(SDL_CreateSurfacePalette(icon), colors, 0, 2);
        SDL_SetSurfaceColorKey(icon, true, 0);
        SDL_SetWindowIcon(sdl.window, icon);
        SDL_DestroySurface(icon);
    }

    Com_Printf("Using SDL video driver: %s\n", SDL_GetCurrentVideoDriver());

    return true;
}

/*
==========================================================================

EVENTS

==========================================================================
*/

static const byte scantokey[] = {
    #include "keytables/sdl.h"
};

static const byte scantokey2[] = {
    K_LCTRL, K_LSHIFT, K_LALT, K_LWINKEY, K_RCTRL, K_RSHIFT, K_RALT, K_RWINKEY
};

static void key_event(SDL_KeyboardEvent *event)
{
    unsigned key = event->scancode;

    if (key < q_countof(scantokey))
        key = scantokey[key];
    else if (key >= SDL_SCANCODE_LCTRL && key < SDL_SCANCODE_LCTRL + q_countof(scantokey2))
        key = scantokey2[key - SDL_SCANCODE_LCTRL];
    else
        key = 0;

    if (!key) {
        Com_DPrintf("%s: unknown scancode %d\n", __func__, event->scancode);
        return;
    }

    Key_Event(key, event->down, event->timestamp);
}

static void mouse_button_event(SDL_MouseButtonEvent *event)
{
    unsigned key;

    switch (event->button) {
    case SDL_BUTTON_LEFT:
        key = K_MOUSE1;
        break;
    case SDL_BUTTON_RIGHT:
        key = K_MOUSE2;
        break;
    case SDL_BUTTON_MIDDLE:
        key = K_MOUSE3;
        break;
    case SDL_BUTTON_X1:
        key = K_MOUSE4;
        break;
    case SDL_BUTTON_X2:
        key = K_MOUSE5;
        break;
    default:
        Com_DPrintf("%s: unknown button %d\n", __func__, event->button);
        return;
    }

    Key_Event(key, event->down, event->timestamp);
}

static void mouse_wheel_event(SDL_MouseWheelEvent *event)
{
    if (event->x > 0) {
        Key_Event(K_MWHEELRIGHT, true, event->timestamp);
        Key_Event(K_MWHEELRIGHT, false, event->timestamp);
    } else if (event->x < 0) {
        Key_Event(K_MWHEELLEFT, true, event->timestamp);
        Key_Event(K_MWHEELLEFT, false, event->timestamp);
    }

    if (event->y > 0) {
        Key_Event(K_MWHEELUP, true, event->timestamp);
        Key_Event(K_MWHEELUP, false, event->timestamp);
    } else if (event->y < 0) {
        Key_Event(K_MWHEELDOWN, true, event->timestamp);
        Key_Event(K_MWHEELDOWN, false, event->timestamp);
    }
}

static void pump_events(void)
{
    SDL_Event   event;
    Uint32      flags;
    active_t    active;
    vrect_t     rc;
    float       f;

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
            Com_Quit(NULL, ERR_DISCONNECT);
            break;

        case SDL_EVENT_WINDOW_MINIMIZED:
        case SDL_EVENT_WINDOW_RESTORED:
        case SDL_EVENT_WINDOW_MOUSE_ENTER:
        case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        case SDL_EVENT_WINDOW_FOCUS_GAINED:
        case SDL_EVENT_WINDOW_FOCUS_LOST:
        case SDL_EVENT_WINDOW_SHOWN:
        case SDL_EVENT_WINDOW_HIDDEN:
            flags = SDL_GetWindowFlags(sdl.window);
            if (flags & (SDL_WINDOW_INPUT_FOCUS | SDL_WINDOW_MOUSE_FOCUS)) {
                active = ACT_ACTIVATED;
            } else if (flags & (SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) {
                active = ACT_MINIMIZED;
            } else {
                active = ACT_RESTORED;
            }
            CL_Activate(active);
            break;

        case SDL_EVENT_WINDOW_MOVED:
            flags = SDL_GetWindowFlags(sdl.window);
            if (!(flags & SDL_WINDOW_FULLSCREEN)) {
                SDL_GetWindowSize(sdl.window, &rc.width, &rc.height);
                rc.x = event.window.data1;
                rc.y = event.window.data2;
                VID_SetGeometry(&rc);
            }
            break;

        case SDL_EVENT_WINDOW_RESIZED:
            flags = SDL_GetWindowFlags(sdl.window);
            if (!(flags & SDL_WINDOW_FULLSCREEN)) {
                SDL_GetWindowPosition(sdl.window, &rc.x, &rc.y);
                rc.width = event.window.data1;
                rc.height = event.window.data2;
                VID_SetGeometry(&rc);
            }
            break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
        case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
            mode_changed();
            break;

        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            key_event(&event.key);
            break;

        case SDL_EVENT_MOUSE_MOTION:
            f = SDL_GetWindowPixelDensity(sdl.window);
            Key_MouseEvent(event.motion.x * f, event.motion.y * f);
            break;

        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            mouse_button_event(&event.button);
            break;

        case SDL_EVENT_MOUSE_WHEEL:
            mouse_wheel_event(&event.wheel);
            break;
        }
    }
}

/*
===============================================================================

MOUSE

===============================================================================
*/

static bool get_mouse_motion(float *dx, float *dy)
{
    if (!SDL_GetWindowRelativeMouseMode(sdl.window)) {
        return false;
    }
    SDL_GetRelativeMouseState(dx, dy);
    return true;
}

static void warp_mouse(int x, int y)
{
    SDL_WarpMouseInWindow(sdl.window, x, y);
    SDL_GetRelativeMouseState(NULL, NULL);
}

static void shutdown_mouse(void)
{
    SDL_SetWindowMouseGrab(sdl.window, false);
    SDL_SetWindowRelativeMouseMode(sdl.window, false);
    SDL_ShowCursor();
}

static bool init_mouse(void)
{
    if (!SDL_WasInit(SDL_INIT_VIDEO)) {
        return false;
    }

    Com_Printf("SDL mouse initialized.\n");
    return true;
}

static void grab_mouse(bool grab)
{
    SDL_SetWindowMouseGrab(sdl.window, grab);
    if (Key_GetDest() & (KEY_MENU | KEY_GAME))
        grab = false;
    SDL_SetWindowRelativeMouseMode(sdl.window, grab);
    SDL_GetRelativeMouseState(NULL, NULL);
    if (sdl.flags & QVF_FULLSCREEN)
        SDL_HideCursor();
    else
        SDL_ShowCursor();
}

static bool probe(void)
{
    return true;
}

const vid_driver_t vid_sdl = {
    .name = "sdl",

    .probe = probe,
    .init = init,
    .shutdown = shutdown,
    .fatal_shutdown = fatal_shutdown,
    .pump_events = pump_events,

    .get_dpi_scale = get_dpi_scale,
    .set_mode = set_mode,

    .get_proc_addr = get_proc_addr,
    .swap_buffers = swap_buffers,
    .swap_interval = swap_interval,

    .get_selection_data = get_selection_data,
    .get_clipboard_data = get_clipboard_data,
    .set_clipboard_data = set_clipboard_data,

    .init_mouse = init_mouse,
    .shutdown_mouse = shutdown_mouse,
    .grab_mouse = grab_mouse,
    .warp_mouse = warp_mouse,
    .get_mouse_motion = get_mouse_motion,
};
