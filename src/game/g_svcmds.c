// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"

static void Svcmd_Test_f(void)
{
    G_Printf("Svcmd_Test_f()\n");
}

// [Paril-KEX]
static void SVCmd_NextMap_f(void)
{
    G_ClientPrintf(NULL, PRINT_HIGH, "Map ended by server.\n");
    EndDMLevel();
}

/*
=================
ServerCommand

ServerCommand will be called when an "sv" command is issued.
The game can issue trap_Argc() / trap_Argv() commands to get the rest
of the parameters
=================
*/
void ServerCommand(void)
{
    char cmd[MAX_QPATH];

    trap_Argv(1, cmd, sizeof(cmd));
    if (Q_strcasecmp(cmd, "test") == 0)
        Svcmd_Test_f();
    else if (Q_strcasecmp(cmd, "nextmap") == 0)
        SVCmd_NextMap_f();
    else if (Q_strcasecmp(cmd, "meminfo") == 0)
        G_MemoryInfo_f();
    else
        G_Printf("Unknown server command \"%s\"\n", cmd);
}
