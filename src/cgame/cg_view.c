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
// cl_view.c -- player rendering positioning

#include "client.h"

//=============
//
// development tools for weapons
//
int         gun_frame;
qhandle_t   gun_model;

//=============

static cvar_t   *cl_add_particles;
static cvar_t   *cl_add_lights;
static cvar_t   *cl_add_entities;
static cvar_t   *cl_add_blend;

#if USE_DEBUG
static cvar_t   *cl_testparticles;
static cvar_t   *cl_testentities;
static cvar_t   *cl_testlights;
static cvar_t   *cl_testblend;

static cvar_t   *cl_stats;
#endif

cvar_t   *cl_adjustfov;

int         r_numdlights;
dlight_t    r_dlights[MAX_DLIGHTS];

int         r_numentities;
entity_t    r_entities[MAX_ENTITIES];

int         r_numparticles;
particle_t  r_particles[MAX_PARTICLES];

lightstyle_t    r_lightstyles[MAX_LIGHTSTYLES];

/*
====================
V_ClearScene

Specifies the model that will be used as the world
====================
*/
static void V_ClearScene(void)
{
    r_numdlights = 0;
    r_numentities = 0;
    r_numparticles = 0;
}

/*
=====================
trap_R_AddEntity

=====================
*/
void trap_R_AddEntity(const entity_t *ent)
{
    if (r_numentities >= MAX_ENTITIES)
        return;
    r_entities[r_numentities++] = *ent;
}

/*
=====================
V_AddParticle

=====================
*/
void V_AddParticle(const particle_t *p)
{
    if (r_numparticles >= MAX_PARTICLES)
        return;
    r_particles[r_numparticles++] = *p;
}

/*
=====================
trap_R_AddLight

=====================
*/
void trap_R_AddLight(const vec3_t org, float intensity, float r, float g, float b)
{
    dlight_t    *dl;

    if (r_numdlights >= MAX_DLIGHTS)
        return;
    dl = &r_dlights[r_numdlights++];
    VectorCopy(org, dl->origin);
    dl->intensity = intensity;
    dl->color[0] = r;
    dl->color[1] = g;
    dl->color[2] = b;
}

/*
=====================
V_AddLightStyle

=====================
*/
void V_AddLightStyle(int style, float value)
{
    lightstyle_t    *ls;

    Q_assert(style >= 0 && style < MAX_LIGHTSTYLES);
    ls = &r_lightstyles[style];
    ls->white = value;
}

#if USE_DEBUG

/*
================
V_TestParticles

If cl_testparticles is set, create 4096 particles in the view
================
*/
static void V_TestParticles(void)
{
    particle_t  *p;
    int         i, j;
    float       d, r, u;

    r_numparticles = MAX_PARTICLES;
    for (i = 0; i < r_numparticles; i++) {
        d = i * 0.25f;
        r = 4 * ((i & 7) - 3.5f);
        u = 4 * (((i >> 3) & 7) - 3.5f);
        p = &r_particles[i];

        for (j = 0; j < 3; j++)
            p->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j] * d +
                           cl.v_right[j] * r + cl.v_up[j] * u;

        p->color = 8;
        p->alpha = 1;
    }
}

/*
================
V_TestEntities

If cl_testentities is set, create 32 player models
================
*/
static void V_TestEntities(void)
{
    int         i, j;
    float       f, r;
    entity_t    *ent;

    r_numentities = 32;
    memset(r_entities, 0, sizeof(r_entities));

    for (i = 0; i < r_numentities; i++) {
        ent = &r_entities[i];

        r = 64 * ((i % 4) - 1.5f);
        f = 64 * (i / 4) + 128;

        for (j = 0; j < 3; j++)
            ent->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j] * f +
                             cl.v_right[j] * r;

        ent->model = cl.baseclientinfo.model;
        ent->skin = cl.baseclientinfo.skin;
    }
}

/*
================
V_TestLights

If cl_testlights is set, create 32 lights models
================
*/
static void V_TestLights(void)
{
    int         i, j;
    float       f, r;
    dlight_t    *dl;

    if (cl_testlights->integer != 1) {
        dl = &r_dlights[0];
        r_numdlights = 1;

        VectorMA(cl.refdef.vieworg, 256, cl.v_forward, dl->origin);
        if (cl_testlights->integer == -1)
            VectorSet(dl->color, -1, -1, -1);
        else
            VectorSet(dl->color, 1, 1, 1);
        dl->intensity = 256;
        return;
    }

    r_numdlights = MAX_DLIGHTS;
    memset(r_dlights, 0, sizeof(r_dlights));

    for (i = 0; i < r_numdlights; i++) {
        dl = &r_dlights[i];

        r = 64 * ((i % 4) - 1.5f);
        f = 64 * (i / 4) + 128;

        for (j = 0; j < 3; j++)
            dl->origin[j] = cl.refdef.vieworg[j] + cl.v_forward[j] * f +
                            cl.v_right[j] * r;
        dl->color[0] = ((i % 6) + 1) & 1;
        dl->color[1] = (((i % 6) + 1) & 2) >> 1;
        dl->color[2] = (((i % 6) + 1) & 4) >> 2;
        dl->intensity = 200;
    }
}

#endif

//===================================================================

void CG_UpdateBlendSetting(void)
{
    MSG_WriteByte(clc_setting);
    MSG_WriteShort(CLS_NOBLEND);
    MSG_WriteShort(!cl_add_blend->integer);
    MSG_FlushTo(&cls.netchan.message);
}

//============================================================================

// gun frame debugging functions
static void V_Gun_Next_f(void)
{
    gun_frame++;
    Com_Printf("frame %i\n", gun_frame);
}

static void V_Gun_Prev_f(void)
{
    gun_frame--;
    if (gun_frame < 0)
        gun_frame = 0;
    Com_Printf("frame %i\n", gun_frame);
}

static void V_Gun_Model_f(void)
{
    char    name[MAX_QPATH];

    if (Cmd_Argc() != 2) {
        gun_model = 0;
        return;
    }
    Q_concat(name, sizeof(name), "models/", Cmd_Argv(1), "/tris.md2");
    gun_model = R_RegisterModel(name);
}

//============================================================================

// renderer will iterate the list backwards, so sorting order must be reversed
static int entitycmpfnc(const void *_a, const void *_b)
{
    const entity_t *a = (const entity_t *)_a;
    const entity_t *b = (const entity_t *)_b;

    bool a_trans = a->flags & RF_TRANSLUCENT;
    bool b_trans = b->flags & RF_TRANSLUCENT;
    if (a_trans != b_trans)
        return b_trans - a_trans;
    if (a_trans) {
        float dist_a = DistanceSquared(a->origin, cl.refdef.vieworg);
        float dist_b = DistanceSquared(b->origin, cl.refdef.vieworg);
        if (dist_a > dist_b)
            return 1;
        if (dist_a < dist_b)
            return -1;
    }

    bool a_shell = a->flags & RF_SHELL_MASK;
    bool b_shell = b->flags & RF_SHELL_MASK;
    if (a_shell != b_shell)
        return b_shell - a_shell;

    // all other models are sorted by model then skin
    if (a->model > b->model)
        return -1;
    if (a->model < b->model)
        return 1;

    if (a->skin > b->skin)
        return -1;
    if (a->skin < b->skin)
        return 1;

    return 0;
}

static void V_SetLightLevel(void)
{
    vec3_t shadelight;

    // save off light value for server to look at (BIG HACK!)
    R_LightPoint(cl.refdef.vieworg, shadelight);

    // pick the greatest component, which should be the same
    // as the mono value returned by software
    if (shadelight[0] > shadelight[1]) {
        if (shadelight[0] > shadelight[2]) {
            cl.lightlevel = 150.0f * shadelight[0];
        } else {
            cl.lightlevel = 150.0f * shadelight[2];
        }
    } else {
        if (shadelight[1] > shadelight[2]) {
            cl.lightlevel = 150.0f * shadelight[1];
        } else {
            cl.lightlevel = 150.0f * shadelight[2];
        }
    }
}

/*
====================
V_CalcFov
====================
*/
float V_CalcFov(float fov_x, float width, float height)
{
    float    a;
    float    x;

    if (fov_x < 0.75f || fov_x > 179)
        Com_Error(ERR_DROP, "%s: bad fov: %f", __func__, fov_x);

    x = width / tanf(fov_x * (M_PIf / 360));

    a = atanf(height / x);
    a = a * (360 / M_PIf);

    return a;
}


/*
==================
V_RenderView

==================
*/
void V_RenderView(void)
{
    // an invalid frame will just use the exact previous refdef
    // we can't use the old frame if the video mode has changed, though...
    if (cl.frame.valid) {
        V_ClearScene();

        // build a refresh entity list and calc cl.sim*
        // this also calls CG_CalcViewValues which loads
        // v_forward, etc.
        CG_AddEntities();

#if USE_DEBUG
        if (cl_testparticles->integer)
            V_TestParticles();
        if (cl_testentities->integer)
            V_TestEntities();
        if (cl_testlights->integer)
            V_TestLights();
        if (cl_testblend->integer & 1)
            Vector4Set(cl.refdef.screen_blend, 1, 0.5f, 0.25f, 0.5f);
        if (cl_testblend->integer & 2)
            Vector4Set(cl.refdef.damage_blend, 0.25f, 0.5f, 0.7f, 0.5f);
#endif

        // never let it sit exactly on a node line, because a water plane can
        // disappear when viewed with the eye exactly on it.
        // the server protocol only specifies to 1/8 pixel, so add 1/16 in each axis
        cl.refdef.vieworg[0] += 1.0f / 16;
        cl.refdef.vieworg[1] += 1.0f / 16;
        cl.refdef.vieworg[2] += 1.0f / 16;

        cl.refdef.x = scr_vrect.x;
        cl.refdef.y = scr_vrect.y;
        cl.refdef.width = scr_vrect.width;
        cl.refdef.height = scr_vrect.height;

        // adjust for non-4/3 screens
        if (cl_adjustfov->integer) {
            cl.refdef.fov_y = cl.fov_y;
            cl.refdef.fov_x = V_CalcFov(cl.refdef.fov_y, cl.refdef.height, cl.refdef.width);
        } else {
            cl.refdef.fov_x = cl.fov_x;
            cl.refdef.fov_y = V_CalcFov(cl.refdef.fov_x, cl.refdef.width, cl.refdef.height);
        }

        cl.refdef.frametime = cls.frametime;
        cl.refdef.time = cl.time * 0.001f;

        if (cl.frame.areabytes) {
            cl.refdef.areabits = cl.frame.areabits;
        } else {
            cl.refdef.areabits = NULL;
        }

        if (!cl_add_entities->integer)
            r_numentities = 0;
        if (!cl_add_particles->integer)
            r_numparticles = 0;
        if (!cl_add_lights->integer)
            r_numdlights = 0;
        if (!cl_add_blend->integer) {
            Vector4Clear(cl.refdef.screen_blend);
            Vector4Clear(cl.refdef.damage_blend);
        }
        if (cl.custom_fog.density) {
            cl.refdef.fog = cl.custom_fog;
            cl.refdef.heightfog = (player_heightfog_t){ 0 };
        }

        cl.refdef.num_entities = r_numentities;
        cl.refdef.entities = r_entities;
        cl.refdef.num_particles = r_numparticles;
        cl.refdef.particles = r_particles;
        cl.refdef.num_dlights = r_numdlights;
        cl.refdef.dlights = r_dlights;
        cl.refdef.lightstyles = r_lightstyles;
        cl.refdef.rdflags = cl.frame.ps.rdflags;

        // sort entities for better cache locality
        qsort(cl.refdef.entities, cl.refdef.num_entities, sizeof(cl.refdef.entities[0]), entitycmpfnc);
    }

    R_RenderFrame(&cl.refdef);
#if USE_DEBUG
    if (cl_stats->integer)
        Com_Printf("ent:%i  lt:%i  part:%i\n", r_numentities, r_numdlights, r_numparticles);
#endif

    V_SetLightLevel();
}


/*
=============
V_Viewpos_f
=============
*/
static void V_Viewpos_f(void)
{
    Com_Printf("%s : %.f\n", vtos(cl.refdef.vieworg), cl.refdef.viewangles[YAW]);
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
    int argc = Cmd_Argc();
    float args[5];

    if (argc == 1) {
        if (cl.custom_fog.density || cl.custom_fog.sky_factor) {
            Com_Printf("User set global fog:\n");
            dump_fog(&cl.custom_fog);
            return;
        }
        if (cl.frame.ps.fog.density || cl.frame.ps.fog.sky_factor) {
            Com_Printf("Global fog:\n");
            dump_fog(&cl.frame.ps.fog);
        }
        if (cl.frame.ps.heightfog.density) {
            Com_Printf("Height fog:\n");
            dump_heightfog(&cl.frame.ps.heightfog);
        }
        if (!(cl.frame.ps.fog.density || cl.frame.ps.fog.sky_factor || cl.frame.ps.heightfog.density))
            Com_Printf("No fog.\n");
        return;
    }

    if (argc < 5) {
        Com_Printf("Usage: %s <r g b density> [sky_factor]\n", Cmd_Argv(0));
        return;
    }

    for (int i = 0; i < 5; i++)
        args[i] = Q_clipf(Q_atof(Cmd_Argv(i + 1)), 0, 1);

    cl.custom_fog.color[0]   = args[0];
    cl.custom_fog.color[1]   = args[1];
    cl.custom_fog.color[2]   = args[2];
    cl.custom_fog.density    = args[3];
    cl.custom_fog.sky_factor = args[4];

    cl.refdef.fog = cl.custom_fog;
    cl.refdef.heightfog = (player_heightfog_t){ 0 };
}

static const cmdreg_t v_cmds[] = {
    { "gun_next", V_Gun_Next_f },
    { "gun_prev", V_Gun_Prev_f },
    { "gun_model", V_Gun_Model_f },
    { "viewpos", V_Viewpos_f },
    { "fog", V_Fog_f },
    { NULL }
};

static void cl_add_blend_changed(cvar_t *self)
{
    CG_UpdateBlendSetting();
}

/*
=============
V_Init
=============
*/
void V_Init(void)
{
    Cmd_Register(v_cmds);

#if USE_DEBUG
    cl_testblend = Cvar_Get("cl_testblend", "0", 0);
    cl_testparticles = Cvar_Get("cl_testparticles", "0", 0);
    cl_testentities = Cvar_Get("cl_testentities", "0", 0);
    cl_testlights = Cvar_Get("cl_testlights", "0", CVAR_CHEAT);

    cl_stats = Cvar_Get("cl_stats", "0", 0);
#endif

    cl_add_lights = Cvar_Get("cl_lights", "1", 0);
    cl_add_particles = Cvar_Get("cl_particles", "1", 0);
    cl_add_entities = Cvar_Get("cl_entities", "1", 0);
    cl_add_blend = Cvar_Get("cl_blend", "1", 0);
    cl_add_blend->changed = cl_add_blend_changed;

    cl_adjustfov = Cvar_Get("cl_adjustfov", "1", 0);
}

void V_Shutdown(void)
{
    Cmd_Deregister(v_cmds);
}
