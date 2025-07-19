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
// cg_precache.c
//

#include "cg_local.h"
#include "shared/files.h"

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

static qhandle_t CG_RegisterSexedSound(const char *model, const char *base)
{
    char buffer[MAX_QPATH];

    // see if we already know of the model specific sound
    if (Q_concat(buffer, MAX_QPATH, "#players/", model, "/", base, ".wav") >= MAX_QPATH)
        Q_concat(buffer, MAX_QPATH, "#players/male/", base, ".wav");

    // see if it exists
    if (trap_FS_OpenFile(buffer + 1, NULL, 0) >= 0)
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
static void CG_ParsePlayerSkin(char *name, char *model, char *skin, const char *s)
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
    if (cg_noskins.integer == 2 || !COM_IsPath(skin))
        goto default_skin;

    if (cg_noskins.integer || !COM_IsPath(model))
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
static void CG_LoadClientinfo(clientinfo_t *ci, const char *s)
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
    ci->model = trap_R_RegisterModel(model_filename);
    if (!ci->model && Q_stricmp(model_name, "male")) {
        strcpy(model_name, "male");
        strcpy(model_filename, "players/male/tris.md2");
        ci->model = trap_R_RegisterModel(model_filename);
    }

    // skin file
    Q_concat(skin_filename, sizeof(skin_filename),
             "players/", model_name, "/", skin_name, ".pcx");
    ci->skin = trap_R_RegisterSkin(skin_filename);

    // if we don't have the skin and the model was female,
    // see if athena skin exists
    if (!ci->skin && !Q_stricmp(model_name, "female")) {
        strcpy(skin_name, "athena");
        strcpy(skin_filename, "players/female/athena.pcx");
        ci->skin = trap_R_RegisterSkin(skin_filename);
    }

    // if we don't have the skin and the model wasn't male,
    // see if the male has it (this is for CTF's skins)
    if (!ci->skin && Q_stricmp(model_name, "male")) {
        // change model to male
        strcpy(model_name, "male");
        strcpy(model_filename, "players/male/tris.md2");
        ci->model = trap_R_RegisterModel(model_filename);

        // see if the skin exists for the male model
        Q_concat(skin_filename, sizeof(skin_filename),
                 "players/male/", skin_name, ".pcx");
        ci->skin = trap_R_RegisterSkin(skin_filename);
    }

    // if we still don't have a skin, it means that the male model
    // didn't have it, so default to grunt
    if (!ci->skin) {
        // see if the skin exists for the male model
        strcpy(skin_name, "grunt");
        strcpy(skin_filename, "players/male/grunt.pcx");
        ci->skin = trap_R_RegisterSkin(skin_filename);
    }

    // weapon file
    for (i = 0; i < cgs.numWeaponModels; i++) {
        Q_concat(weapon_filename, sizeof(weapon_filename),
                 "players/", model_name, "/", cgs.weaponModels[i]);
        ci->weaponmodel[i] = trap_R_RegisterModel(weapon_filename);
        if (!ci->weaponmodel[i] && !Q_stricmp(model_name, "cyborg")) {
            // try male
            Q_concat(weapon_filename, sizeof(weapon_filename),
                     "players/male/", cgs.weaponModels[i]);
            ci->weaponmodel[i] = trap_R_RegisterModel(weapon_filename);
        }
    }

    // icon file
    Q_concat(icon_filename, sizeof(icon_filename),
             "/players/", model_name, "/", skin_name, "_i.pcx");
    ci->icon = trap_R_RegisterPic(icon_filename);

    strcpy(ci->model_name, model_name);
    strcpy(ci->skin_name, skin_name);

    // sounds
    for (i = 0; i < SS_MAX; i++)
        ci->sounds[i] = CG_RegisterSexedSound(model_name, sexed_sounds[i]);

    // base info should be at least partially valid
    if (ci == &cgs.baseclientinfo)
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
CG_RegisterVWepModels

Builds a list of visual weapon models
=================
*/
static void CG_RegisterVWepModels(void)
{
    int     i;
    char    name[MAX_QPATH];

    cgs.numWeaponModels = 1;
    strcpy(cgs.weaponModels[0], "weapon.md2");

    // only default model when vwep is off
    if (!cg_vwep.integer)
        return;

    for (i = 1; i < MAX_MODELS; i++) {
        trap_GetConfigstring(CS_MODELS + i, name, sizeof(name));
        if (name[0] != '#')
            continue;

        // special player weapon model
        Q_strlcpy(cgs.weaponModels[cgs.numWeaponModels++], name + 1, sizeof(cgs.weaponModels[0]));

        if (cgs.numWeaponModels == MAX_CLIENTWEAPONMODELS)
            break;
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
    char        name[MAX_QPATH];

    trap_GetConfigstring(CS_SKYROTATE, name, sizeof(name));
    sscanf(name, "%f %d", &rotate, &autorotate);

    trap_GetConfigstring(CS_SKYAXIS, name, sizeof(name));
    if (sscanf(name, "%f %f %f", &axis[0], &axis[1], &axis[2]) != 3) {
        Com_Printf("Couldn't parse CS_SKYAXIS\n");
        VectorClear(axis);
    }

    trap_GetConfigstring(CS_SKY, name, sizeof(name));
    trap_R_SetSky(name, rotate, autorotate, axis);
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
        if (!Q_stricmpn(s, CONST_STR_LEN("sprites/")))
            return trap_R_RegisterSprite(s);

        if (strchr(s, '/'))
            return trap_R_RegisterSkin(s);
    }

    return trap_R_RegisterPic(s);
}

/*
=================
CG_RegisterMedia

Call before entering a new level, or after changing dlls
=================
*/
void CG_RegisterMedia(void)
{
    int     i;
    char    name[MAX_QPATH];

    trap_SetLoadState("models");
    CG_RegisterVWepModels();
    CG_RegisterTEntModels();

    for (i = 1; i < MAX_MODELS; i++) {
        trap_GetConfigstring(CS_MODELS + i, name, sizeof(name));
        if (!name[0])
            break;
        if (name[0] == '#')
            continue;
        cgs.models.precache[i] = trap_R_RegisterModel(name);
    }

    trap_SetLoadState("images");
    for (i = 1; i < MAX_IMAGES; i++) {
        trap_GetConfigstring(CS_IMAGES + i, name, sizeof(name));
        if (!name[0])
            break;
        cgs.images.precache[i] = CG_RegisterImage(name);
    }
    cgs.images.flare = trap_R_RegisterSprite("misc/flare.tga");

    trap_SetLoadState("clients");
    for (i = 0; i < MAX_CLIENTS; i++) {
        trap_GetConfigstring(CS_PLAYERSKINS + i, name, sizeof(name));
        if (!name[0])
            continue;
        CG_LoadClientinfo(&cgs.clientinfo[i], name);
    }

    CG_LoadClientinfo(&cgs.baseclientinfo, "unnamed\\male/grunt");

    trap_SetLoadState("sounds");
    CG_RegisterTEntSounds();
    for (i = 1; i < MAX_SOUNDS; i++) {
        trap_GetConfigstring(CS_SOUNDS + i, name, sizeof(name));
        if (!name[0])
            break;
        cgs.sounds.precache[i] = trap_S_RegisterSound(name);
    }

    for (int i = 0; i < MAX_LIGHTSTYLES; i++) {
        trap_GetConfigstring(CS_LIGHTS + i, name, sizeof(name));
        if (!name[0])
            continue;
        CG_SetLightStyle(i, name);
    }

    // set sky textures and speed
    CG_SetSky();

    // start the cd track
    trap_GetConfigstring(CS_CDTRACK, name, sizeof(name));
    trap_S_StartBackgroundTrack(name);
}

/*
=================
CG_UpdateConfigstring

A configstring update has been parsed.
=================
*/
qvm_exported void CG_UpdateConfigstring(unsigned index)
{
    char s[MAX_QPATH];
    trap_GetConfigstring(index, s, sizeof(s));

    if (index == CS_MAXCLIENTS) {
        cgs.maxclients = Q_atoi(s);
        return;
    }

    if (index == CS_AIRACCEL) {
        pm_config.airaccel = Q_atoi(s);
        return;
    }

    if (index == CONFIG_PHYSICS_FLAGS) {
        pm_config.physics_flags = Q_atoi(s);
        return;
    }

    if (index == CS_STATUSBAR) {
        trap_GetConfigstring(index, cgs.statusbar, sizeof(cgs.statusbar));
        return;
    }

    if (index >= CS_LIGHTS && index < CS_LIGHTS + MAX_LIGHTSTYLES) {
        CG_SetLightStyle(index - CS_LIGHTS, s);
        return;
    }

    if (index >= CS_MODELS && index < CS_MODELS + MAX_MODELS) {
        cgs.models.precache[index - CS_MODELS] = trap_R_RegisterModel(s);
        return;
    }

    if (index >= CS_SOUNDS && index < CS_SOUNDS + MAX_SOUNDS) {
        cgs.sounds.precache[index - CS_SOUNDS] = trap_S_RegisterSound(s);
        return;
    }

    if (index >= CS_IMAGES && index < CS_IMAGES + MAX_IMAGES) {
        cgs.images.precache[index - CS_IMAGES] = CG_RegisterImage(s);
        return;
    }

    if (index >= CS_PLAYERSKINS && index < CS_PLAYERSKINS + MAX_CLIENTS) {
        CG_LoadClientinfo(&cgs.clientinfo[index - CS_PLAYERSKINS], s);
        return;
    }

    if (index == CS_CDTRACK) {
        trap_S_StartBackgroundTrack(s);
        return;
    }

    if (index == CS_SKYROTATE || index == CS_SKYAXIS) {
        CG_SetSky();
        return;
    }
}
