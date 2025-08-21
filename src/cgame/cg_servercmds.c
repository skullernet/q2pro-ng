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

static void CG_Chat(const char *text)
{
    // disable notify
    print_type_t type = PRINT_TALK;
    if (!cg_chat_notify.integer)
        type |= PRINT_SKIPNOTIFY;

    trap_Print(type, text);

    SCR_AddToChatHUD(text);

    // play sound
    if (cg_chat_sound.integer > 0) {
        qhandle_t sfx = cgs.sounds.talk[cg_chat_sound.integer > 1];
        trap_S_StartSound(NULL, cg.listener.entnum, CHAN_TALK, sfx, 1, ATTN_NONE, 0);
    }
}

static int CG_ParseInt(int arg)
{
    char buf[MAX_QPATH];
    trap_Argv(arg, buf, sizeof(buf));
    return Q_atoi(buf);
}

static float CG_ParseFloat(int arg)
{
    char buf[MAX_QPATH];
    trap_Argv(arg, buf, sizeof(buf));
    return Q_atof(buf);
}

static void CG_ParseVector(int arg, vec3_t v)
{
    v[0] = CG_ParseFloat(arg + 0);
    v[1] = CG_ParseFloat(arg + 1);
    v[2] = CG_ParseFloat(arg + 2);
}

static void CG_Inventory(void)
{
    int count = trap_Argc() - 1;
    Q_assert_soft(count <= MAX_ITEMS);

    for (int i = 0; i < count; i++)
        cg.inventory[i] = CG_ParseInt(i + 1);

    for (int i = count; i < MAX_ITEMS; i++)
        cg.inventory[i] = 0;
}

// for reliable and local sounds
static void CG_Sound(void)
{
    unsigned entnum   = CG_ParseInt(1);
    unsigned channel  = CG_ParseInt(2);
    unsigned index    = CG_ParseInt(3);
    float volume      = CG_ParseFloat(4);
    float attenuation = CG_ParseFloat(5);

    Q_assert_soft(entnum < MAX_EDICTS);
    Q_assert_soft(index < MAX_SOUNDS);

    trap_S_StartSound(NULL, entnum, channel, cgs.sounds.precache[index], volume, attenuation, 0);
}

typedef enum {
    POI_OBJECTIVE = MAX_EDICTS,
    POI_PING,
    POI_PING_END = POI_PING + MAX_CLIENTS - 1
} pois_t;

static void CG_Poi(void)
{
    vec3_t point;
    CG_ParseVector(1, point);

    unsigned index = CG_ParseInt(4);
    Q_assert_soft(index < MAX_IMAGES);

    SCR_AddPOI(POI_OBJECTIVE, point, cgs.images.precache[index], U32_GREEN, cg_compass_time.value * 1000);

    trap_S_StartSound(NULL, cg.listener.entnum, CHAN_AUTO, cgs.sounds.help_marker, 1, ATTN_NONE, 0);
}

static void CG_Ping(void)
{
    unsigned entnum = CG_ParseInt(1);
    Q_assert_soft(entnum < MAX_CLIENTS);

    vec3_t point;
    CG_ParseVector(2, point);

    unsigned index = CG_ParseInt(5);
    Q_assert_soft(index < MAX_IMAGES);

    SCR_AddPOI(POI_PING + entnum, point, cgs.images.precache[index], U32_GREEN, 5000);

    trap_S_StartSound(NULL, cg.listener.entnum, CHAN_AUTO, cgs.sounds.help_marker, 1, ATTN_NONE, 0);
}

static void CG_Path(void)
{
    vec3_t point;
    CG_ParseVector(2, point);

    vec3_t dir;
    ByteToDir(CG_ParseInt(5), dir);

    CG_AddHelpPath(point, dir, CG_ParseInt(1));

    trap_S_StartSound(point, ENTITYNUM_WORLD, CHAN_AUTO, cgs.sounds.help_marker, 1, ATTN_NORM, 0);
}

// server commands allow transmitting arbitrary data from game to cgame
// without changing network protocol
qvm_exported void CG_ServerCommand(void)
{
    char cmd[MAX_QPATH];
    trap_Argv(0, cmd, sizeof(cmd));

    if (!strcmp(cmd, "layout")) {
        trap_Args(cg.layout, sizeof(cg.layout));
        return;
    }

    if (!strcmp(cmd, "inven")) {
        CG_Inventory();
        return;
    }

    if (!strcmp(cmd, "sound")) {
        CG_Sound();
        return;
    }

    if (!strcmp(cmd, "poi")) {
        CG_Poi();
        return;
    }

    if (!strcmp(cmd, "ping")) {
        CG_Ping();
        return;
    }

    if (!strcmp(cmd, "path")) {
        CG_Path();
        return;
    }

    char buf[MAX_STRING_CHARS];
    trap_Argv(1, buf, sizeof(buf));

    if (!strcmp(cmd, "print")) {
        trap_Print(PRINT_ALL, buf);
        return;
    }

    if (!strcmp(cmd, "cp")) {
        SCR_CenterPrint(buf, false);
        return;
    }

    if (!strcmp(cmd, "tw")) {
        SCR_CenterPrint(buf, true);
        return;
    }

    if (!strcmp(cmd, "chat")) {
        CG_Chat(buf);
        return;
    }
}
