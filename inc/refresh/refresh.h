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

#include "common/cvar.h"
#include "common/error.h"
#include "shared/refresh.h"

enum {
    QGL_PROFILE_NONE,
    QGL_PROFILE_CORE,
    QGL_PROFILE_ES,
};

typedef struct {
    uint8_t     colorbits;
    uint8_t     depthbits;
    uint8_t     stencilbits;
    uint8_t     multisamples;
    bool        debug;
    uint8_t     profile;
    uint8_t     major_ver;
    uint8_t     minor_ver;
} r_opengl_config_t;

extern refcfg_t r_config;

// called when the library is loaded
bool    R_Init(bool total);

// called before the library is unloaded
void    R_Shutdown(bool total);

// All data that will be used in a level should be
// registered before rendering any frames to prevent disk hits,
// but they can still be registered at a later time
// if necessary.
//
// EndRegistration will free any remaining data that wasn't registered.
// Any model_s or skin_s pointers from before the BeginRegistration
// are no longer valid after EndRegistration.
//
// Skins and images need to be differentiated, because skins
// are flood filled to eliminate mip map edge errors, and pics have
// an implicit "pics/" prepended to the name. (a pic name that starts with a
// slash will not use the "pics/" prefix or the ".pcx" postfix)
void    R_BeginRegistration(const char *map);
qhandle_t R_RegisterModel(const char *name);
qhandle_t R_RegisterPic(const char *name);
qhandle_t R_RegisterFont(const char *name);
qhandle_t R_RegisterTempPic(const char *name);
qhandle_t R_RegisterTempFont(const char *name);
qhandle_t R_RegisterSkin(const char *name);
qhandle_t R_RegisterSprite(const char *name);
void    R_SetSky(const char *name, float rotate, bool autorotate, const vec3_t axis);
void    R_EndRegistration(void);

void    R_ClearScene(void);
void    R_AddEntity(const entity_t *ent);
void    R_AddLight(const light_t *light);
void    R_SetLightStyle(unsigned style, float value);
void    R_LocateParticles(const particle_t *p, int count);
void    R_RenderFrame(const refdef_t *fd);
void    R_LightPoint(const vec3_t origin, vec3_t light);

void    R_GetPalette(uint32_t palette[256]);
void    R_ClearColor(void);
void    R_SetAlpha(float clpha);
void    R_SetColor24(uint32_t color);
void    R_SetColor(uint32_t color);
void    R_SetClipRect(const clipRect_t *clip);
float   R_ClampScale(cvar_t *var);
void    R_SetScale(float scale);
void    R_DrawChar(int x, int y, int flags, int ch, qhandle_t font);
int     R_DrawString(int x, int y, int flags, size_t maxChars,
                     const char *string, qhandle_t font);  // returns advanced x coord
bool    R_GetPicSize(int *w, int *h, qhandle_t pic);   // returns transparency bit
void    R_DrawPic(int x, int y, qhandle_t pic);
void    R_DrawStretchPic(int x, int y, int w, int h, qhandle_t pic);
void    R_DrawKeepAspectPic(int x, int y, int w, int h, qhandle_t pic);
void    R_DrawStretchRaw(int x, int y, int w, int h);
void    R_UpdateRawPic(int pic_w, int pic_h, const uint32_t *pic);
void    R_TileClear(int x, int y, int w, int h, qhandle_t pic);
void    R_DrawFill8(int x, int y, int w, int h, int c);
void    R_DrawFill32(int x, int y, int w, int h, uint32_t color);

// video mode and refresh state management entry points
void    R_BeginFrame(void);
void    R_EndFrame(void);
void    R_ModeChanged(int width, int height, int flags);
bool    R_VideoSync(void);

r_opengl_config_t R_GetGLConfig(void);
