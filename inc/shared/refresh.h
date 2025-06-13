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

#pragma once

#define MAX_DLIGHTS     64
#define MAX_ENTITIES    2048
#define MAX_PARTICLES   8192
#define MAX_LIGHTSTYLES 256

#define POWERSUIT_SCALE     4.0f
#define WEAPONSHELL_SCALE   0.5f

#define RF_TRACKER          BIT_ULL(32)

#define RF_SHELL_MASK       (RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE | \
                             RF_SHELL_DOUBLE | RF_SHELL_HALF_DAM | RF_SHELL_LITE_GREEN)

#define DLIGHT_CUTOFF       64

typedef struct entity_s {
    qhandle_t           model;          // opaque type outside refresh
    vec3_t              angles;

    /*
    ** most recent data
    */
    vec3_t              origin;     // also used as RF_BEAM's "from"
    unsigned            frame;      // also used as RF_BEAM's diameter

    /*
    ** previous data for lerping
    */
    vec3_t              oldorigin;  // also used as RF_BEAM's "to"
    unsigned            oldframe;

    /*
    ** misc
    */
    float   backlerp;               // 0.0 = current, 1.0 = old
    int     skinnum;                // also used as RF_BEAM's palette index,
                                    // -1 => use rgba

    float   alpha;                  // ignore if RF_TRANSLUCENT isn't set
    color_t rgba;

    uint64_t    flags;

    qhandle_t   skin;           // NULL for inline skin
    float       scale;
} entity_t;

typedef struct {
    vec3_t  origin;
    int     color;              // -1 => use rgba
    float   scale;
    float   alpha;
    color_t rgba;
} particle_t;

typedef struct {
    float   white;              // highest of RGB
} lightstyle_t;

typedef struct {
    int         x, y, width, height;// in virtual screen coordinates
    float       fov_x, fov_y;
    vec3_t      vieworg;
    vec3_t      viewangles;
    vec4_t      screen_blend;       // rgba 0-1 full screen blend
    vec4_t      damage_blend;       // rgba 0-1 damage blend
    player_fog_t        fog;
    player_heightfog_t  heightfog;
    float       frametime;          // seconds since last video frame
    float       time;               // time is used to auto animate
    int         rdflags;            // RDF_UNDERWATER, etc
    byte        areabits[MAX_MAP_AREA_BYTES];   // only areas with set bits will be drawn
} refdef_t;

typedef struct {
    int left, right, top, bottom;
} clipRect_t;

typedef enum {
    QVF_FULLSCREEN      = BIT(0),
    QVF_GAMMARAMP       = BIT(1),
    QVF_VIDEOSYNC       = BIT(2),
} vidFlags_t;

typedef struct {
    int         width;
    int         height;
    vidFlags_t  flags;
} refcfg_t;
