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
// cl_scrn.c -- master for refresh, status bar, console, chat, notify, etc

#include "client.h"

static cvar_t   *scr_netgraph;
static cvar_t   *scr_timegraph;
static cvar_t   *scr_debuggraph;
static cvar_t   *scr_graphheight;
static cvar_t   *scr_graphscale;
static cvar_t   *scr_graphshift;
static cvar_t   *scr_showstats;

static qhandle_t    scr_font;
static int          scr_width;
static int          scr_height;

#define GRAPH_SAMPLES   4096
#define GRAPH_MASK      (GRAPH_SAMPLES - 1)

static struct {
    float       values[GRAPH_SAMPLES];
    byte        colors[GRAPH_SAMPLES];
    unsigned    current;
} graph;

/*
==============
SCR_DebugGraph
==============
*/
void SCR_DebugGraph(float value, int color)
{
    graph.values[graph.current & GRAPH_MASK] = value;
    graph.colors[graph.current & GRAPH_MASK] = color;
    graph.current++;
}

/*
==============
SCR_AddNetgraph

A new packet was just parsed
==============
*/
void SCR_AddNetgraph(void)
{
    int         i, color;
    unsigned    value;

    // if using the debuggraph for something else, don't
    // add the net lines
    if (scr_debuggraph->integer || scr_timegraph->integer)
        return;

    for (i = 0; i < cls.netchan.dropped; i++)
        SCR_DebugGraph(30, 0x40);

    if (cl.frame.flags & FF_SUPPRESSED)
        SCR_DebugGraph(30, 0xdf);

    if (scr_netgraph->integer > 1) {
        value = msg_read.cursize;
        if (value < 200)
            color = 61;
        else if (value < 500)
            color = 59;
        else if (value < 800)
            color = 57;
        else if (value < 1200)
            color = 224;
        else
            color = 242;
        value /= 40;
    } else {
        // see what the latency was on this packet
        i = cls.netchan.incoming_acknowledged & CMD_MASK;
        value = (cls.realtime - cl.history[i].sent) / 30;
        color = 0xd0;
    }

    SCR_DebugGraph(min(value, 30), color);
}

/*
==============
SCR_DrawDebugGraph
==============
*/
static void SCR_DrawDebugGraph(void)
{
    int     a, y, w, i, h, height;
    float   v, scale, shift;

    scale = scr_graphscale->value;
    shift = scr_graphshift->value;
    height = scr_graphheight->integer;
    if (height < 1)
        return;

    w = scr_width;
    y = scr_height;

    for (a = 0; a < w; a++) {
        i = (graph.current - 1 - a) & GRAPH_MASK;
        v = graph.values[i] * scale + shift;

        if (v < 0)
            v += height * (1 + (int)(-v / height));

        h = (int)v % height;
        R_DrawFill8(w - 1 - a, y - h, 1, h, graph.colors[i]);
    }
}

static void SCR_DrawDebugStats(void)
{
    if (!scr_showstats->integer)
        return;

    static const char *const names[4] = {
        "c_fps", "r_fps", "c_mps", "c_pps"
    };

    int x = scr_width - 11 * CONCHAR_WIDTH;
    int y = scr_height - 5 * CONCHAR_HEIGHT;

    for (int i = 0; i < 4; i++) {
        R_DrawString(x, y, 0, -1, va("%4d %s", cls.measure.fps[i], names[i]), scr_font);
        y += CONCHAR_HEIGHT;
    }
}

void SCR_ModeChanged(void)
{
    IN_Activate();
    Con_CheckResize();
    UI_ModeChanged();
    if (cge)
        cge->ModeChanged();
    scr_width = r_config.width * r_config.scale;
    scr_height = r_config.height * r_config.scale;
    cls.disable_screen = 0;
}

/*
==================
SCR_Init
==================
*/
void SCR_Init(void)
{
    scr_netgraph = Cvar_Get("netgraph", "0", 0);
    scr_timegraph = Cvar_Get("timegraph", "0", 0);
    scr_debuggraph = Cvar_Get("debuggraph", "0", 0);
    scr_graphheight = Cvar_Get("graphheight", "32", 0);
    scr_graphscale = Cvar_Get("graphscale", "1", 0);
    scr_graphshift = Cvar_Get("graphshift", "0", 0);
    scr_showstats = Cvar_Get("scr_showstats", "0", 0);
}

void SCR_Shutdown(void)
{
}

void SCR_RegisterMedia(void)
{
    scr_font = R_RegisterFont("conchars");
}

/*
================
SCR_BeginLoadingPlaque
================
*/
void SCR_BeginLoadingPlaque(void)
{
    if (!cls.state) {
        return;
    }

    S_StopAllSounds();
    OGG_Update();

    if (cls.disable_screen) {
        return;
    }

#if USE_DEBUG
    if (developer->integer) {
        return;
    }
#endif

    // if at console or menu, don't bring up the plaque
    if (cls.key_dest & (KEY_CONSOLE | KEY_MENU)) {
        return;
    }

    cls.draw_loading = true;
    SCR_UpdateScreen();
    cls.draw_loading = false;

    cls.disable_screen = Sys_Milliseconds();
}

/*
================
SCR_EndLoadingPlaque
================
*/
void SCR_EndLoadingPlaque(void)
{
    if (!cls.state) {
        return;
    }
    cls.disable_screen = 0;
    Con_ClearNotify_f();
}

static void SCR_DrawActive(void)
{
    // if full screen menu is up, do nothing at all
    if (!UI_IsTransparent())
        return;

    // draw black background if not active
    if (cls.state < ca_active) {
        R_DrawFill8(0, 0, r_config.width, r_config.height, 0);
        return;
    }

    if (cls.state == ca_cinematic)
        SCR_DrawCinematic();

    if (cls.state == ca_active || cls.draw_loading)
        cge->DrawFrame(cls.realtime, cls.state == ca_active, cls.draw_loading);

    R_SetScale(r_config.scale);

    SCR_DrawDebugStats();

    if (scr_timegraph->integer)
        SCR_DebugGraph(cls.frametime * 300, 0xdc);

    if (scr_debuggraph->integer || scr_timegraph->integer || scr_netgraph->integer)
        SCR_DrawDebugGraph();

    R_SetScale(1.0f);
}

/*
==================
SCR_UpdateScreen

This is called every frame, and can also be called explicitly to flush
text to the screen.
==================
*/
void SCR_UpdateScreen(void)
{
    static int recursive;

    if (!cls.ref_initialized) {
        return;             // not initialized yet
    }

    // if the screen is disabled (loading plaque is up), do nothing at all
    if (cls.disable_screen) {
        unsigned delta = Sys_Milliseconds() - cls.disable_screen;

        if (delta < 120 * 1000) {
            return;
        }

        cls.disable_screen = 0;
        Com_Printf("Loading plaque timed out.\n");
    }

    if (recursive > 1) {
        Com_Error(ERR_FATAL, "%s: recursively called", __func__);
    }

    recursive++;

    R_BeginFrame();

    // do 3D refresh drawing
    SCR_DrawActive();

    // draw main menu
    UI_Draw(cls.realtime);

    // draw console
    Con_DrawConsole();

    R_EndFrame();

    recursive--;
}
