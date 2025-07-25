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

#include "server.h"

server_static_t svs;                // persistent server info
server_t        sv;                 // local server

void SV_ClientReset(client_t *client)
{
    if (client->state < cs_connected) {
        return;
    }

    // any partially connected client will be restarted
    client->state = cs_connected;
    client->framenum = 1; // frame 0 can't be used
    client->lastframe = -1;
    client->frames_nodelta = 0;
    client->send_delta = 0;
    client->suppress_count = 0;
    client->next_entity = 0;
    memset(&client->lastcmd, 0, sizeof(client->lastcmd));
}

static void resolve_masters(void)
{
#if USE_SERVER
    time_t now = time(NULL);

    for (int i = 0; i < MAX_MASTERS; i++) {
        master_t *m = &sv_masters[i];
        if (!m->name) {
            break;
        }
        if (now < m->last_resolved) {
            m->last_resolved = now;
            continue;
        }
        // re-resolve valid address after one day,
        // resolve invalid address after three hours
        int hours = m->adr.type ? 24 : 3;
        if (now - m->last_resolved < hours * 3600) {
            continue;
        }
        if (NET_StringToAdr(m->name, &m->adr, PORT_MASTER)) {
            Com_DPrintf("Master server at %s.\n", NET_AdrToString(&m->adr));
        } else {
            Com_WPrintf("Couldn't resolve master: %s\n", m->name);
            memset(&m->adr, 0, sizeof(m->adr));
        }
        m->last_resolved = now = time(NULL);
    }
#endif
}

/*
================
SV_SetState
================
*/
void SV_SetState(server_state_t state)
{
    sv.state = state;
    Cvar_SetInteger(sv_running, state, FROM_CODE);
}

/*
================
SV_SpawnServer

Change the server to a new map, taking all connected
clients along with it.
================
*/
void SV_SpawnServer(const mapcmd_t *cmd)
{
    int         i;
    client_t    *client;

    SCR_BeginLoadingPlaque();           // for local system
    R_ClearDebugLines();

    Com_Printf("------- Server Initialization -------\n");
    Com_Printf("SpawnServer: %s\n", cmd->server);

    Q_assert(cmd->state >= ss_game);

    // everyone needs to reconnect
    FOR_EACH_CLIENT(client) {
        SV_ClientReset(client);
    }

    SV_BroadcastCommand("changing map=%s\n", cmd->server);
    SV_SendClientMessages();
    SV_SendAsyncPackets();

    // free current level
    for (int i = 0; i < MAX_CONFIGSTRINGS; i++)
        Z_Free(sv.configstrings[i]);
    CM_FreeMap(&sv.cm);
    Nav_Unload();

    // wipe the entire per-level structure
    memset(&sv, 0, sizeof(sv));
    sv.spawncount = Q_rand() & INT_MAX;

    // set framerate parameters
    sv.frametime = 1000 / Cvar_ClampInteger(sv_fps, 10, 60);

    // save name for levels that don't set message
    sv.configstrings[CS_NAME] = SV_CopyString(cmd->server);
    Q_strlcpy(sv.name, cmd->server, sizeof(sv.name));
    Q_strlcpy(sv.mapcmd, cmd->buffer, sizeof(sv.mapcmd));
    Q_strlcpy(sv.spawnpoint, cmd->spawnpoint, sizeof(sv.spawnpoint));

    resolve_masters();

    if (cmd->state == ss_game) {
        sv.cm = cmd->cm;
    } else {
        // no real map
        sv.cm.entitystring = "";
    }

    //
    // clear physics interaction links
    //
    SV_ClearWorld();

    //
    // spawn the rest of the entities on the map
    //

    // precache and static commands can be issued during
    // map initialization
    SV_SetState(ss_loading);

    // load and spawn all other entities
    ge->SpawnEntities();

    // run for 0.2 seconds to allow everything to settle
    for (i = 0; i < 200 / sv.frametime; i++, sv.time += sv.frametime)
        ge->RunFrame(sv.time);

    // check for a savegame
    SV_CheckForSavegame(cmd);

    // all precaches are complete
    SV_SetState(cmd->state);

    // set serverinfo variable
    SV_InfoSet("mapname", sv.name);
    SV_InfoSet("port", net_port->string);

    Cvar_Set("sv_paused", "0");
    Cvar_Set("timedemo", "0");

    EXEC_TRIGGER(sv_changemapcmd);

#if USE_SYSCON
    SV_SetConsoleTitle();
#endif

    SV_BroadcastCommand("reconnect\n");

    Com_Printf("-------------------------------------\n");
}

static server_state_t get_server_state(const char *s)
{
    s = COM_FileExtension(s);

    if (!Q_stricmp(s, ".pcx"))
        return ss_pic;

    if (!Q_stricmp(s, ".cin"))
        return ss_cinematic;

    if (!Q_stricmp(s, ".dm2"))
        return ss_demo;

    return ss_game;
}

static bool parse_and_check_server(mapcmd_t *cmd, const char *server, bool nextserver)
{
    char    expanded[MAX_QPATH], *ch;
    int     ret = Q_ERR(ENAMETOOLONG);

    // copy it off
    Q_strlcpy(cmd->server, server, sizeof(cmd->server));

    // if there is a $, use the remainder as a spawnpoint
    ch = Q_strchrnul(cmd->server, '$');
    if (*ch)
        *ch++ = 0;
    cmd->spawnpoint = ch;

    // now expand and try to load the map
    server_state_t state = get_server_state(cmd->server);
    switch (state) {
    case ss_pic:
        if (Q_concat(expanded, sizeof(expanded), "pics/", cmd->server) < sizeof(expanded))
            ret = COM_DEDICATED ? Q_ERR_SUCCESS : FS_LoadFile(expanded, NULL);
        break;

    case ss_cinematic:
        if (nextserver && !sv_cinematics->integer)
            return false;   // skip it
        if (Q_concat(expanded, sizeof(expanded), "video/", cmd->server) < sizeof(expanded))
            ret = COM_DEDICATED ? Q_ERR_SUCCESS : SCR_CheckForCinematic(expanded);
        break;

    // demos are handled specially, because they are played locally on the client
    case ss_demo:
        if (nextserver && (!sv_cinematics->integer || svs.maxclients > 1))
            return false;       // skip it
        if (Q_concat(expanded, sizeof(expanded), "demos/", cmd->server) >= sizeof(expanded))
            break;
        ret = Q_ERR(ENOSYS);    // only works if running a client
#if USE_CLIENT
        if (dedicated->integer || cmd->loadgame)
            break;              // not supported
        ret = FS_LoadFile(expanded, NULL);
        if (ret == Q_ERR(EFBIG))
            ret = Q_ERR_SUCCESS;
#endif
        break;

    default:
        CM_LoadOverride(&cmd->cm, cmd->server, sizeof(cmd->server));    // may override server!
        if (Q_concat(expanded, sizeof(expanded), "maps/", cmd->server, ".bsp") < sizeof(expanded))
            ret = CM_LoadMap(&cmd->cm, expanded);
        if (ret < 0)
            CM_FreeMap(&cmd->cm);   // free entstring if overridden
        break;
    }

    if (ret < 0) {
        Com_Printf("Couldn't load %s: %s\n", expanded, BSP_ErrorString(ret));
        return false;
    }

    cmd->state = state;
    return true;
}

/*
==============
SV_ParseMapCmd

Parses mapcmd into more C friendly form.
Loads and fully validates the map to make sure server doesn't get killed.
==============
*/
bool SV_ParseMapCmd(mapcmd_t *cmd)
{
    char *s, *ch;
    char copy[MAX_QPATH];
    bool killserver = false;

    // copy it off to keep original mapcmd intact
    Q_strlcpy(copy, cmd->buffer, sizeof(copy));
    s = copy;

    while (1) {
        // hack for nextserver: kill server if map doesn't exist
        if (*s == '!') {
            s++;
            killserver = true;
        }

        // skip the end-of-unit flag if necessary
        if (*s == '*') {
            s++;
            cmd->endofunit = true;
        }

        // if there is a + in the map, set nextserver to the remainder.
        ch = strchr(s, '+');
        if (ch)
            *ch = 0;

        // see if map exists and can be loaded
        if (parse_and_check_server(cmd, s, ch)) {
            if (ch)
                Cvar_Set("nextserver", va("gamemap \"!%s\"", ch + 1));
            else
                Cvar_Set("nextserver", "");

            // special hack for end game screen in coop mode
            if (Cvar_VariableInteger("coop") && !Q_stricmp(s, "victory.pcx"))
                Cvar_Set("nextserver", "gamemap \"!*base1\"");
            return true;
        }

        // skip to nextserver if cinematic doesn't exist
        if (!ch)
            break;

        s = ch + 1;
    }

    if (killserver)
        Cbuf_AddText(&cmd_buffer, "killserver\n");

    return false;
}

/*
==============
SV_InitGame

A brand new game has been started.
==============
*/
void SV_InitGame(void)
{
    int     i;
    edict_t *ent;
    client_t *client;

    if (svs.initialized) {
        // cause any connected clients to reconnect
        SV_Shutdown("Server restarted\n", ERR_RECONNECT);
    } else {
        // make sure the client is down
        CL_Disconnect(ERR_RECONNECT);
        SCR_BeginLoadingPlaque();
        R_ClearDebugLines();

        CM_FreeMap(&sv.cm);
        memset(&sv, 0, sizeof(sv));

        // set up default frametime for main loop
        sv.frametime = BASE_FRAMETIME;
    }

    // get any latched variable changes (maxclients, etc)
    Cvar_GetLatchedVars();

#if USE_SERVER
    Cvar_Reset(sv_recycle);
#endif

    SV_InitGameProgs();

    Cvar_ClampInteger(sv_maxclients, 1, MAX_CLIENTS);

    // enable networking
    if (sv_maxclients->integer > 1) {
        NET_Config(NET_SERVER);
    }

    Cvar_ClampInteger(sv_reserved_slots, 0, sv_maxclients->integer - 1);
    svs.maxclients_soft = sv_maxclients->integer - sv_reserved_slots->integer;

    svs.maxclients = sv_maxclients->integer;
    svs.client_pool = SV_Mallocz(sizeof(svs.client_pool[0]) * svs.maxclients);

#if USE_ZLIB
    svs.z.zalloc = SV_zalloc;
    svs.z.zfree = SV_zfree;
    Q_assert(deflateInit2(&svs.z, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
             -MAX_WBITS, 9, Z_DEFAULT_STRATEGY) == Z_OK);
    svs.z_buffer_size = ZPACKET_HEADER + deflateBound(&svs.z, MAX_MSGLEN);
    svs.z_buffer = SV_Malloc(svs.z_buffer_size);
#endif

    // init game
    ge->Init();

    // send heartbeat very soon
    svs.last_heartbeat = -(HEARTBEAT_SECONDS - 5) * 1000;
    svs.heartbeat_index = 0;

    Q_assert_soft(svs.edicts);
    for (i = 0; i < svs.maxclients; i++) {
        client = svs.client_pool + i;
        ent = SV_EdictForNum(i);
        ent->s.number = i;
        client->edict = ent;
        client->number = i;
    }

    svs.initialized = true;
}
