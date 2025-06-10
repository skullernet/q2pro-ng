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

//
// cl_precache.c
//

#include "client.h"

static const char *const sexed_sounds[SS_MAX] = {
    [SS_DEATH1]    = "death1",
    [SS_DEATH2]    = "death2",
    [SS_DEATH3]    = "death3",
    [SS_DEATH4]    = "death4",
    [SS_FALL1]     = "fall1",
    [SS_FALL2]     = "fall2",
    [SS_GURP1]     = "gurp1",
    [SS_GURP2]     = "gurp2",
    [SS_DROWN]     = "drown1",
    [SS_JUMP]      = "jump1",
    [SS_PAIN25_1]  = "pain25_1",
    [SS_PAIN25_2]  = "pain25_2",
    [SS_PAIN50_1]  = "pain50_1",
    [SS_PAIN50_2]  = "pain50_2",
    [SS_PAIN75_1]  = "pain75_1",
    [SS_PAIN75_2]  = "pain75_2",
    [SS_PAIN100_1] = "pain100_1",
    [SS_PAIN100_2] = "pain100_2",
};

static bool CG_FileExists(const char *name)
{
    qhandle_t f;
    trap_FS_OpenFile(name, &f, FS_MODE_READ | FS_FLAG_LOADFILE);
    if (f) {
        trap_FS_CloseFile(f);
        return true;
    }
    return false;
}

static qhandle_t CG_RegisterSexedSound(const char *model, const char *base)
{
    char buffer[MAX_QPATH];

    // see if we already know of the model specific sound
    if (Q_concat(buffer, MAX_QPATH, "#players/", model, "/", base, ".wav") >= MAX_QPATH)
        Q_concat(buffer, MAX_QPATH, "#players/male/", base, ".wav");

    // see if it exists
    if (CG_FileExists(buffer + 1))
        return trap_S_RegisterSound(buffer);

    // no, revert to the male sound in the pak0.pak
    Q_concat(buffer, MAX_QPATH, "player/male/", base, ".wav");
    return trap_S_RegisterSound(buffer);
}

/*
================
CG_ParsePlayerSkin

Breaks up playerskin into name (optional), model and skin components.
If model or skin are found to be invalid, replaces them with sane defaults.
================
*/
void CG_ParsePlayerSkin(char *name, char *model, char *skin, const char *s)
{
    size_t len;
    char *t;

    len = strlen(s);
    Q_assert(len < MAX_QPATH);

    // isolate the player's name
    t = strchr(s, '\\');
    if (t) {
        len = t - s;
        strcpy(model, t + 1);
    } else {
        len = 0;
        strcpy(model, s);
    }

    // copy the player's name
    if (name) {
        memcpy(name, s, len);
        name[len] = 0;
    }

    // isolate the model name
    t = strchr(model, '/');
    if (!t)
        t = strchr(model, '\\');
    if (!t)
        goto default_model;
    *t = 0;

    // isolate the skin name
    strcpy(skin, t + 1);

    // fix empty model to male
    if (t == model)
        strcpy(model, "male");

    // apply restrictions on skins
    if (cl_noskins->integer == 2 || !COM_IsPath(skin))
        goto default_skin;

    if (cl_noskins->integer || !COM_IsPath(model))
        goto default_model;

    return;

default_skin:
    if (!Q_stricmp(model, "female")) {
        strcpy(model, "female");
        strcpy(skin, "athena");
    } else {
default_model:
        strcpy(model, "male");
        strcpy(skin, "grunt");
    }
}

/*
================
CG_LoadClientinfo

================
*/
void CG_LoadClientinfo(clientinfo_t *ci, const char *s)
{
    int         i;
    char        model_name[MAX_QPATH];
    char        skin_name[MAX_QPATH];
    char        model_filename[MAX_QPATH];
    char        skin_filename[MAX_QPATH];
    char        weapon_filename[MAX_QPATH];
    char        icon_filename[MAX_QPATH];

    CG_ParsePlayerSkin(ci->name, model_name, skin_name, s);

    // model file
    Q_concat(model_filename, sizeof(model_filename),
             "players/", model_name, "/tris.md2");
    ci->model = R_RegisterModel(model_filename);
    if (!ci->model && Q_stricmp(model_name, "male")) {
        strcpy(model_name, "male");
        strcpy(model_filename, "players/male/tris.md2");
        ci->model = R_RegisterModel(model_filename);
    }

    // skin file
    Q_concat(skin_filename, sizeof(skin_filename),
             "players/", model_name, "/", skin_name, ".pcx");
    ci->skin = R_RegisterSkin(skin_filename);

    // if we don't have the skin and the model was female,
    // see if athena skin exists
    if (!ci->skin && !Q_stricmp(model_name, "female")) {
        strcpy(skin_name, "athena");
        strcpy(skin_filename, "players/female/athena.pcx");
        ci->skin = R_RegisterSkin(skin_filename);
    }

    // if we don't have the skin and the model wasn't male,
    // see if the male has it (this is for CTF's skins)
    if (!ci->skin && Q_stricmp(model_name, "male")) {
        // change model to male
        strcpy(model_name, "male");
        strcpy(model_filename, "players/male/tris.md2");
        ci->model = R_RegisterModel(model_filename);

        // see if the skin exists for the male model
        Q_concat(skin_filename, sizeof(skin_filename),
                 "players/male/", skin_name, ".pcx");
        ci->skin = R_RegisterSkin(skin_filename);
    }

    // if we still don't have a skin, it means that the male model
    // didn't have it, so default to grunt
    if (!ci->skin) {
        // see if the skin exists for the male model
        strcpy(skin_name, "grunt");
        strcpy(skin_filename, "players/male/grunt.pcx");
        ci->skin = R_RegisterSkin(skin_filename);
    }

    // weapon file
    for (i = 0; i < cg.numWeaponModels; i++) {
        Q_concat(weapon_filename, sizeof(weapon_filename),
                 "players/", model_name, "/", cg.weaponModels[i]);
        ci->weaponmodel[i] = R_RegisterModel(weapon_filename);
        if (!ci->weaponmodel[i] && !Q_stricmp(model_name, "cyborg")) {
            // try male
            Q_concat(weapon_filename, sizeof(weapon_filename),
                     "players/male/", cg.weaponModels[i]);
            ci->weaponmodel[i] = R_RegisterModel(weapon_filename);
        }
    }

    // icon file
    Q_concat(icon_filename, sizeof(icon_filename),
             "/players/", model_name, "/", skin_name, "_i.pcx");
    ci->icon = R_RegisterTempPic(icon_filename);

    strcpy(ci->model_name, model_name);
    strcpy(ci->skin_name, skin_name);

    // sounds
    for (i = 0; i < SS_MAX; i++)
        ci->sounds[i] = CG_RegisterSexedSound(model_name, sexed_sounds[i]);

    // base info should be at least partially valid
    if (ci == &cg.baseclientinfo)
        return;

    // must have loaded all data types to be valid
    if (!ci->skin || !ci->icon || !ci->model || !ci->weaponmodel[0]) {
        ci->skin = 0;
        ci->icon = 0;
        ci->model = 0;
        ci->weaponmodel[0] = 0;
        ci->model_name[0] = 0;
        ci->skin_name[0] = 0;
    }
}

/*
=================
CG_RegisterSounds
=================
*/
void CG_RegisterSounds(void)
{
    int i;
    char    *s;

    S_BeginRegistration();
    CG_RegisterTEntSounds();
    for (i = 1; i < MAX_SOUNDS; i++) {
        s = cg.configstrings[CS_SOUNDS + i];
        if (!s[0])
            break;
        cg.sound_precache[i] = trap_S_RegisterSound(s);
    }
    S_EndRegistration();
}

/*
=================
CG_RegisterBspModels

Registers main BSP file and inline models
=================
*/
void CG_RegisterBspModels(void)
{
    char *name = va("maps/%s.bsp", cg.mapname);
    int ret;

    ret = BSP_Load(name, &cg.bsp);
    if (cg.bsp == NULL) {
        Com_Error(ERR_DROP, "Couldn't load %s: %s", name, BSP_ErrorString(ret));
    }

    if (cg.bsp->checksum != Q_atoi(cg.configstrings[CS_MAPCHECKSUM])) {
        if (cgs.demo.playback) {
            Com_WPrintf("Local map version differs from demo: %i != %s\n",
                        cg.bsp->checksum, cg.configstrings[CS_MAPCHECKSUM]);
        } else {
            Com_Error(ERR_DROP, "Local map version differs from server: %i != %s",
                      cg.bsp->checksum, cg.configstrings[CS_MAPCHECKSUM]);
        }
    }
}

/*
=================
CG_RegisterVWepModels

Builds a list of visual weapon models
=================
*/
void CG_RegisterVWepModels(void)
{
    int         i;
    char        *name;

    cg.numWeaponModels = 1;
    strcpy(cg.weaponModels[0], "weapon.md2");

    // only default model when vwep is off
    if (!cl_vwep->integer) {
        return;
    }

    for (i = 1; i < MAX_MODELS; i++) {
        name = cg.configstrings[CS_MODELS + i];
        if (!name[0] && i != MODELINDEX_PLAYER) {
            break;
        }
        if (name[0] != '#') {
            continue;
        }

        // special player weapon model
        Q_strlcpy(cg.weaponModels[cg.numWeaponModels++], name + 1, sizeof(cg.weaponModels[0]));

        if (cg.numWeaponModels == MAX_CLIENTWEAPONMODELS) {
            break;
        }
    }
}

/*
=================
CG_SetSky

=================
*/
void CG_SetSky(void)
{
    float       rotate = 0;
    int         autorotate = 1;
    vec3_t      axis;

    sscanf(cg.configstrings[CS_SKYROTATE], "%f %d", &rotate, &autorotate);

    if (sscanf(cg.configstrings[CS_SKYAXIS], "%f %f %f",
               &axis[0], &axis[1], &axis[2]) != 3) {
        Com_DPrintf("Couldn't parse CS_SKYAXIS\n");
        VectorClear(axis);
    }

    R_SetSky(cg.configstrings[CS_SKY], rotate, autorotate, axis);
}

/*
=================
CG_RegisterImage

Hack to handle RF_CUSTOMSKIN for remaster
=================
*/
static qhandle_t CG_RegisterImage(const char *s)
{
    // if it's in a subdir and has an extension, it's either a sprite or a skin
    // allow /some/pic.pcx escape syntax
    if (*s != '/' && *s != '\\' && *COM_FileExtension(s)) {
        if (!FS_pathcmpn(s, CONST_STR_LEN("sprites/psx_flare")))
            return R_RegisterImage(s, IT_SPRITE, IF_DEFAULT_FLARE);

        if (!FS_pathcmpn(s, CONST_STR_LEN("sprites/")))
            return R_RegisterSprite(s);

        if (strchr(s, '/'))
            return R_RegisterSkin(s);
    }

    return R_RegisterTempPic(s);
}

/*
=================
CG_PrepRefresh

Call before entering a new level, or after changing dlls
=================
*/
void CG_PrepRefresh(void)
{
    int         i;
    char        *name;

    if (!cgs.ref_initialized)
        return;
    if (!cg.mapname[0])
        return;     // no map loaded

    // register models, pics, and skins
    R_BeginRegistration(cg.mapname);

    CG_LoadState(LOAD_MODELS);

    CG_RegisterTEntModels();

    for (i = 1; i < MAX_MODELS; i++) {
        name = cg.configstrings[CS_MODELS + i];
        if (!name[0] && i != MODELINDEX_PLAYER) {
            break;
        }
        if (name[0] == '#') {
            continue;
        }
        cg.model_draw[i] = R_RegisterModel(name);
    }

    CG_LoadState(LOAD_IMAGES);
    for (i = 1; i < MAX_IMAGES; i++) {
        name = cg.configstrings[CS_IMAGES + i];
        if (!name[0]) {
            break;
        }
        cg.image_precache[i] = CG_RegisterImage(name);
    }

    CG_LoadState(LOAD_CLIENTS);
    for (i = 0; i < MAX_CLIENTS; i++) {
        name = cg.configstrings[CS_PLAYERSKINS + i];
        if (!name[0]) {
            continue;
        }
        CG_LoadClientinfo(&cg.clientinfo[i], name);
    }

    CG_LoadClientinfo(&cg.baseclientinfo, "unnamed\\male/grunt");

    // set sky textures and speed
    CG_SetSky();

    // the renderer can now free unneeded stuff
    R_EndRegistration();

    // clear any lines of console text
    Con_ClearNotify_f();

    SCR_UpdateScreen();

    // start the cd track
    OGG_Play();
}

/*
=================
CG_UpdateConfigstring

A configstring update has been parsed.
=================
*/
void CG_UpdateConfigstring(int index)
{
    const char *s = cg.configstrings[index];

    if (index == CS_MAXCLIENTS) {
        cg.maxclients = Q_atoi(s);
        return;
    }

    if (index == CS_AIRACCEL) {
        //cg.pmp.airaccelerate = cg.pmp.qwmode || Q_atoi(s);
        return;
    }

    if (index >= CS_LIGHTS && index < CS_LIGHTS + MAX_LIGHTSTYLES) {
        CG_SetLightStyle(index - CS_LIGHTS, s);
        return;
    }

    if (cgs.state < ca_precached) {
        return;
    }

    if (index >= CS_MODELS && index < CS_MODELS + MAX_MODELS) {
        cg.model_draw[index - CS_MODELS] = R_RegisterModel(s);
        return;
    }

    if (index >= CS_SOUNDS && index < CS_SOUNDS + MAX_SOUNDS) {
        cg.sound_precache[index - CS_SOUNDS] = trap_S_RegisterSound(s);
        return;
    }

    if (index >= CS_IMAGES && index < CS_IMAGES + MAX_IMAGES) {
        cg.image_precache[index - CS_IMAGES] = CG_RegisterImage(s);
        return;
    }

    if (index >= CS_PLAYERSKINS && index < CS_PLAYERSKINS + MAX_CLIENTS) {
        CG_LoadClientinfo(&cg.clientinfo[index - CS_PLAYERSKINS], s);
        return;
    }

    if (index == CS_CDTRACK) {
        OGG_Play();
        return;
    }

    if (index == CS_SKYROTATE || index == CS_SKYAXIS) {
        CG_SetSky();
        return;
    }
}
