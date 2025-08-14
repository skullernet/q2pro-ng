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

static void CG_StartLocalSoundOnce(const char *sound)
{
    if (cg.frame) {
        qhandle_t sfx = trap_S_RegisterSound(sound);
        trap_S_StartSound(NULL, cg.frame->ps.clientnum, CHAN_TALK, sfx, 1, ATTN_NONE, 0);
    }
}

static void CG_Chat(char *text)
{
    // disable notify
    print_type_t type = PRINT_TALK;
    if (!cg_chat_notify.integer)
        type |= PRINT_SKIPNOTIFY;

    trap_Print(type, text);

    SCR_AddToChatHUD(text);

    // play sound
    if (cg_chat_sound.integer > 1)
        CG_StartLocalSoundOnce("misc/talk1.wav");
    else if (cg_chat_sound.integer > 0)
        CG_StartLocalSoundOnce("misc/talk.wav");
}

static void CG_Inventory(void)
{
    int i, count = trap_Argc() - 1;
    Q_assert_soft(count <= MAX_ITEMS);

    for (i = 0; i < count; i++) {
        char buf[MAX_QPATH];
        trap_Argv(i + 1, buf, sizeof(buf));
        cg.inventory[i] = Q_atoi(buf);
    }

    for (; i < MAX_ITEMS; i++)
        cg.inventory[i] = 0;
}

// for reliable and local sounds
static void CG_Sound(void)
{
    char buf[MAX_QPATH];

    trap_Argv(1, buf, sizeof(buf));
    unsigned entnum = Q_atoi(buf);
    Q_assert_soft(entnum < MAX_EDICTS);

    trap_Argv(2, buf, sizeof(buf));
    unsigned channel = Q_atoi(buf);

    trap_Argv(3, buf, sizeof(buf));
    unsigned index = Q_atoi(buf);
    Q_assert_soft(index < MAX_SOUNDS);

    trap_Argv(4, buf, sizeof(buf));
    float volume = Q_atof(buf);

    trap_Argv(5, buf, sizeof(buf));
    float attenuation = Q_atof(buf);

    trap_S_StartSound(NULL, entnum, channel, cgs.sounds.precache[index], volume, attenuation, 0);
}

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
