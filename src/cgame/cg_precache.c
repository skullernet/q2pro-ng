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

/*
================
CG_LoadClientinfo

Parse short infostring containing player name, skin, etc.
================
*/
static void CG_LoadClientinfo(cg_clientinfo_t *ci, const char *s)
{
    char    model_name[MAX_QPATH];
    char    skin_name[MAX_QPATH];
    int     i;
    char    *val;

    // name and gender
    Q_strlcpy(ci->name, Info_ValueForKey(s, "name"), sizeof(ci->name));
    val = Info_ValueForKey(s, "gender");
    if (*val)
        ci->gender = Q_clip(Q_atoi(val), GENDER_MALE, GENDER_NEUTRAL);
    else
        ci->gender = GENDER_NEUTRAL;

    val = Info_ValueForKey(s, "skin");
    BG_ParsePlayerSkin(model_name, skin_name, val, cg_noskins.integer);

    // model file
    ci->model = trap_R_RegisterModel(va("players/%s/tris.md2", model_name));
    if (!ci->model && Q_stricmp(model_name, "male")) {
        strcpy(model_name, "male");
        ci->model = trap_R_RegisterModel("players/male/tris.md2");
    }

    // skin file
    ci->skin = trap_R_RegisterSkin(va("players/%s/%s.pcx", model_name, skin_name));

    // if we don't have the skin and the model wasn't male,
    // see if the male has it (this is for CTF's skins)
    if (!ci->skin && Q_stricmp(model_name, "male")) {
        // change model to male
        strcpy(model_name, "male");
        ci->model = trap_R_RegisterModel("players/male/tris.md2");

        // see if the skin exists for the male model
        ci->skin = trap_R_RegisterSkin(va("players/male/%s.pcx", skin_name));
    }

    // if we still don't have a skin, it means that the male model
    // didn't have it, so default to grunt
    if (!ci->skin) {
        // see if the skin exists for the male model
        strcpy(skin_name, "grunt");
        ci->skin = trap_R_RegisterSkin("players/male/grunt.pcx");
    }

    // disguise skin
    ci->disguise_skin = trap_R_RegisterSkin(va("players/%s/disguise.pcx", model_name));
    if (!ci->disguise_skin)
        ci->disguise_skin = ci->skin;

    // weapon file
    for (i = 0; i < MAX_CLIENTWEAPONS; i++) {
        char name[MAX_QPATH];
        if (!trap_GetConfigstring(CS_CLIENTWEAPONS + i, name, sizeof(name)))
            break;
        ci->weaponmodel[i] = trap_R_RegisterModel(va("players/%s/%s", model_name, name));
        if (!ci->weaponmodel[i] && !Q_stricmp(model_name, "cyborg")) {
            // try male
            ci->weaponmodel[i] = trap_R_RegisterModel(va("players/male/%s", name));
        }
        // only default model when vwep is off
        if (!cg_vwep.integer)
            break;
    }

    // icon file
    ci->icon = trap_R_RegisterPic(va("/players/%s/%s_i.pcx", model_name, skin_name));

    // dogtag file
    val = Info_ValueForKey(s, "dogtag");
    ci->dogtag = 0;
    if (COM_IsPath(val))
        ci->dogtag = trap_R_RegisterPic(va("/tags/%s.pcx", val));
    if (!ci->dogtag)
        ci->dogtag = trap_R_RegisterPic("/tags/default.pcx");

    // sounds
    for (i = 0; i < SS_MAX; i++) {
        // see if we already know of the model specific sound
        ci->sounds[i] = trap_S_RegisterSound(va("#players/%s/%s.wav", model_name, sexed_sounds[i]));
        if (!ci->sounds[i]) {
            // no, revert to the male sound in the pak0.pak
            ci->sounds[i] = trap_S_RegisterSound(va("player/male/%s.wav", sexed_sounds[i]));
        }
    }

    // base info should be at least partially valid
    if (ci == &cgs.baseclientinfo)
        return;

    // must have loaded all data types to be valid
    if (!ci->skin || !ci->icon || !ci->model || !ci->weaponmodel[0]) {
        ci->skin = 0;
        ci->disguise_skin = 0;
        ci->icon = 0;
        ci->dogtag = 0;
        ci->model = 0;
        ci->weaponmodel[0] = 0;
    }
}

/*
=================
CG_SetSky

=================
*/
void CG_SetSky(void)
{
    char buffer[MAX_STRING_CHARS];
    sky_params_t sky;

    trap_GetConfigstring(CS_SKY, buffer, sizeof(buffer));
    BG_ParseSkyParams(&sky, buffer);
    trap_R_SetSky(sky.name, sky.rotate, sky.autorotate, sky.axis);
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

static void CG_RegisterAmmo(const char *s, cg_wheel_ammo_t *item)
{
    char name[MAX_QPATH];
    unsigned icon;

    item->id = Q_atoi(COM_Parse(&s));
    Q_assert_soft(item->id < MAX_ITEMS);

    icon = Q_atoi(COM_Parse(&s));
    Q_assert_soft(icon < MAX_IMAGES);

    trap_GetConfigstring(CS_IMAGES + icon, name, sizeof(name));
    item->icon = trap_R_RegisterPic(va("wheel/%s", name));
    if (!item->icon)
        item->icon = trap_R_RegisterPic(name);
}

static void CG_RegisterItem(const char *s, cg_wheel_item_t *item)
{
    char name[MAX_QPATH];
    unsigned icon;

    item->id = Q_atoi(COM_Parse(&s));
    Q_assert_soft(item->id < MAX_ITEMS);

    icon = Q_atoi(COM_Parse(&s));
    Q_assert_soft(icon < MAX_IMAGES);

    item->ammo = Q_atoi(COM_Parse(&s));
    item->quantity = Q_atoi(COM_Parse(&s));
    item->quantity_warn = Q_atoi(COM_Parse(&s));
    item->droppable = Q_atoi(COM_Parse(&s));

    trap_GetConfigstring(CS_IMAGES + icon, name, sizeof(name));
    item->icon[0] = trap_R_RegisterPic(va("wheel/%s", name));
    item->icon[1] = trap_R_RegisterPic(va("wheel/%s_selected", name));
    if (!item->icon[0])
        item->icon[0] = trap_R_RegisterPic(name);
    if (!item->icon[1])
        item->icon[1] = item->icon[0];
}

static void CG_RegisterWheelItems(void)
{
    int     i;
    char    name[MAX_QPATH];

    for (i = 0; i < MAX_WHEEL_ITEMS; i++) {
        if (!trap_GetConfigstring(CS_WHEEL_AMMO + i, name, sizeof(name)))
            break;
        CG_RegisterAmmo(name, &cgs.wheel.ammo[i]);
    }
    cgs.wheel.num_ammo = i;

    for (i = 0; i < MAX_WHEEL_ITEMS; i++) {
        if (!trap_GetConfigstring(CS_WHEEL_WEAPONS + i, name, sizeof(name)))
            break;
        CG_RegisterItem(name, &cgs.wheel.weapons[i]);
    }
    cgs.wheel.num_weapons = i;

    for (i = 0; i < MAX_WHEEL_ITEMS; i++) {
        if (!trap_GetConfigstring(CS_WHEEL_POWERUPS + i, name, sizeof(name)))
            break;
        CG_RegisterItem(name, &cgs.wheel.powerups[i]);
    }
    cgs.wheel.num_powerups = i;
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
    char    skin[MAX_INFO_STRING];

    trap_SetLoadText("models");
    CG_RegisterTEntModels();
    for (i = 1; i < MAX_MODELS; i++) {
        if (!trap_GetConfigstring(CS_MODELS + i, name, sizeof(name)))
            break;
        cgs.models.precache[i] = trap_R_RegisterModel(name);
    }

    trap_SetLoadText("images");
    cgs.images.flare = trap_R_RegisterSprite("misc/flare.tga");
    cgs.images.friend = trap_R_RegisterPic("friend");
    for (i = 1; i < MAX_IMAGES; i++) {
        if (!trap_GetConfigstring(CS_IMAGES + i, name, sizeof(name)))
            break;
        cgs.images.precache[i] = CG_RegisterImage(name);
    }

    CG_RegisterWheelItems();

    trap_SetLoadText("sounds");
    CG_RegisterTEntSounds();
    for (i = 1; i < MAX_SOUNDS; i++) {
        if (!trap_GetConfigstring(CS_SOUNDS + i, name, sizeof(name)))
            break;
        cgs.sounds.precache[i] = trap_S_RegisterSound(name);
    }

    trap_SetLoadText("clients");
    CG_LoadClientinfo(&cgs.baseclientinfo, "\\name\\unnamed\\skin\\male/grunt");
    for (i = 0; i < MAX_CLIENTS; i++) {
        if (!trap_GetConfigstring(CS_PLAYERSKINS + i, skin, sizeof(skin)))
            continue;
        CG_LoadClientinfo(&cgs.clientinfo[i], skin);
    }

    for (i = 0; i < MAX_LIGHTSTYLES; i++) {
        if (!trap_GetConfigstring(CS_LIGHTS + i, name, sizeof(name)))
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
    if (index == CS_SKY) {
        CG_SetSky();
        return;
    }

    if (index == CS_STATUSBAR) {
        trap_GetConfigstring(index, cgs.statusbar, sizeof(cgs.statusbar));
        return;
    }

    if (index == CS_LAYOUT) {
        trap_GetConfigstring(index, cg.layout, sizeof(cg.layout));
        return;
    }

    if (index >= CS_PLAYERSKINS && index < CS_PLAYERSKINS + MAX_CLIENTS) {
        char s[MAX_INFO_STRING];
        trap_GetConfigstring(index, s, sizeof(s));
        CG_LoadClientinfo(&cgs.clientinfo[index - CS_PLAYERSKINS], s);
        return;
    }

    if (index >= CS_LEVEL_ENTRIES && index <= CS_LEVEL_ENTRIES_LAST) {
        char s[MAX_STRING_CHARS];
        trap_GetConfigstring(index, s, sizeof(s));
        BG_ParseLevelEntry(&cgs.level_entries[index - CS_LEVEL_ENTRIES], s);
        return;
    }

    char s[MAX_QPATH];
    trap_GetConfigstring(index, s, sizeof(s));

    if (index == CS_CDTRACK) {
        trap_S_StartBackgroundTrack(s);
        return;
    }

    if (index == CS_AIRACCEL) {
        pm_config.airaccel = Q_atoi(s);
        return;
    }

    if (index == CS_PHYSICS_FLAGS) {
        pm_config.physics_flags = Q_atoi(s);
        return;
    }

    if (index == CS_MAXCLIENTS) {
        cgs.maxclients = Q_atoi(s);
        return;
    }

    if (index >= CS_MODELS + 1 && index < CS_MODELS + MAX_MODELS) {
        cgs.models.precache[index - CS_MODELS] = trap_R_RegisterModel(s);
        return;
    }

    if (index >= CS_SOUNDS + 1 && index < CS_SOUNDS + MAX_SOUNDS) {
        cgs.sounds.precache[index - CS_SOUNDS] = trap_S_RegisterSound(s);
        return;
    }

    if (index >= CS_IMAGES + 1 && index < CS_IMAGES + MAX_IMAGES) {
        cgs.images.precache[index - CS_IMAGES] = CG_RegisterImage(s);
        return;
    }

    if (index >= CS_LIGHTS && index < CS_LIGHTS + MAX_LIGHTSTYLES) {
        CG_SetLightStyle(index - CS_LIGHTS, s);
        return;
    }
}
