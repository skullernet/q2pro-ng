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
// sv_user.c -- server code for moving users

#include "server.h"

#define MSG_GAMESTATE   (MSG_RELIABLE | MSG_CLEAR | MSG_COMPRESS)

/*
============================================================

USER STRINGCMD EXECUTION

sv_client and sv_player will be valid.
============================================================
*/

static int      stringCmdCount;

/*
================
SV_CreateBaselines

Entity baselines are used to compress the update messages
to the clients -- only the fields that differ from the
baseline will be transmitted
================
*/
static void SV_CreateBaselines(void)
{
    int        i;
    edict_t    *ent;
    entity_state_t *base, **chunk;

    // clear baselines from previous level
    for (i = 0; i < SV_BASELINES_CHUNKS; i++) {
        base = sv_client->baselines[i];
        if (base) {
            memset(base, 0, sizeof(*base) * SV_BASELINES_PER_CHUNK);
        }
    }

    for (i = 0; i < svs.num_edicts; i++) {
        ent = SV_EdictForNum(i);

        if (!ent->r.inuse) {
            continue;
        }

        Q_assert_soft(ent->s.number == i);

        if (ent->r.svflags & SVF_NOCLIENT) {
            continue;
        }

        if (!HAS_EFFECTS(ent)) {
            continue;
        }

        chunk = &sv_client->baselines[i >> SV_BASELINES_SHIFT];
        if (*chunk == NULL) {
            *chunk = SV_Mallocz(sizeof(*base) * SV_BASELINES_PER_CHUNK);
        }

        base = *chunk + (i & SV_BASELINES_MASK);
        *base = ent->s;

        // no need to transmit data that will change anyway
        if (i < svs.maxclients) {
            VectorClear(base->origin);
            VectorClear(base->angles);
            base->frame = 0;
        }

        // don't ever transmit event
        base->event[0] = base->event[1] = 0;
    }
}

static void write_configstring_stream(void)
{
    int         i;
    const char *string;
    size_t      length;

    MSG_WriteByte(svc_configstringstream);

    // write a packet full of data
    for (i = 0; i < MAX_CONFIGSTRINGS; i++) {
        string = sv.configstrings[i];
        if (!string) {
            continue;
        }
        length = strlen(string);

        // check if this configstring will overflow
        if (msg_write.cursize + length + 5 > msg_write.maxsize) {
            MSG_WriteShort(MAX_CONFIGSTRINGS);
            SV_ClientAddMessage(sv_client, MSG_GAMESTATE);
            MSG_WriteByte(svc_configstringstream);
        }

        MSG_WriteShort(i);
        MSG_WriteData(string, length + 1);
    }

    MSG_WriteShort(MAX_CONFIGSTRINGS);
    SV_ClientAddMessage(sv_client, MSG_GAMESTATE);
}

static void write_baseline_stream(void)
{
    int i, j;
    const entity_state_t *base;

    MSG_BeginWriting();
    MSG_WriteByte(svc_baselinestream);

    // write a packet full of data
    for (i = 0; i < SV_BASELINES_CHUNKS; i++) {
        base = sv_client->baselines[i];
        if (!base) {
            continue;
        }
        for (j = 0; j < SV_BASELINES_PER_CHUNK; j++, base++) {
            if ((i || j) && !base->number) {
                continue;
            }
            // check if this baseline will overflow
            if (msg_write.cursize + msg_max_entity_bytes > msg_write.maxsize) {
                MSG_WriteBits(ENTITYNUM_NONE, ENTITYNUM_BITS);
                MSG_FlushBits();
                SV_ClientAddMessage(sv_client, MSG_GAMESTATE);
                MSG_BeginWriting();
                MSG_WriteByte(svc_baselinestream);
            }
            MSG_WriteDeltaEntity(NULL, base, false);
        }
    }

    MSG_WriteBits(ENTITYNUM_NONE, ENTITYNUM_BITS);
    MSG_FlushBits();
    SV_ClientAddMessage(sv_client, MSG_GAMESTATE);
}

/*
================
SV_New_f

Sends the first message from the server to a connected client.
This will be sent on the initial connection and upon each server load.
================
*/
void SV_New_f(void)
{
    Com_DPrintf("New() from %s\n", sv_client->name);

    if (sv_client->state < cs_connected) {
        Com_DPrintf("Going from cs_assigned to cs_connected for %s\n",
                    sv_client->name);
        sv_client->state = cs_connected;
        sv_client->lastmessage = svs.realtime; // don't timeout
        sv_client->connect_time = time(NULL);
    } else if (sv_client->state > cs_connected) {
        Com_DPrintf("New not valid -- already primed\n");
        return;
    }

    //
    // serverdata needs to go over for all types of servers
    // to make sure the protocol is right, and to set the gamedir
    //

    // create baselines for this client
    SV_CreateBaselines();

    // send the serverdata
    MSG_WriteByte(svc_serverdata);
    MSG_WriteLong(PROTOCOL_VERSION_MAJOR);
    MSG_WriteLong(sv_client->protocol);
    MSG_WriteLong(sv.spawncount);
    MSG_WriteByte(sv.state);
    MSG_WriteByte(sv_client->number);
    MSG_WriteString(fs_game->string);
    MSG_WriteString(sv.name);
    MSG_WriteLong(sv.cm.checksum);

    SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);

    Com_DPrintf("Going from cs_connected to cs_primed for %s\n",
                sv_client->name);
    sv_client->state = cs_primed;

    memset(&sv_client->lastcmd, 0, sizeof(sv_client->lastcmd));

    if (sv.state == ss_pic || sv.state == ss_cinematic)
        return;

    // send gamestate
    write_configstring_stream();
    write_baseline_stream();

    // send next command
    SV_ClientCommand(sv_client, "precache %i\n", sv.spawncount);
}

/*
==================
SV_Begin_f
==================
*/
void SV_Begin_f(void)
{
    Com_DPrintf("Begin() from %s\n", sv_client->name);

    // handle the case of a level changing while a client was connecting
    if (sv_client->state < cs_primed) {
        Com_DPrintf("Begin not valid -- not yet primed\n");
        SV_New_f();
        return;
    }
    if (sv_client->state > cs_primed) {
        Com_DPrintf("Begin not valid -- already spawned\n");
        return;
    }
    if (sv.state == ss_pic || sv.state == ss_cinematic) {
        Com_DPrintf("Begin not valid -- map not loaded\n");
        return;
    }

    Com_DPrintf("Going from cs_primed to cs_spawned for %s\n", sv_client->name);
    sv_client->state = cs_spawned;
    sv_client->begin_time = sv.time;
    sv_client->send_delta = 0;
    sv_client->command_msec = 1800;
    sv_client->cmd_msec_used = 0;
    sv_client->suppress_count = 0;
    sv_client->download = false;

    // allocate packet entities if not done yet
    if (!sv_client->entities)
        sv_client->entities = SV_Mallocz(sizeof(sv_client->entities[0]) * MAX_PARSE_ENTITIES);

    // call the game begin function
    ge->ClientBegin(sv_client->number);
}

//============================================================================

// a cinematic has completed or been aborted by a client, so move to the next server
static void SV_NextServer_f(void)
{
    if (sv.state != ss_pic && sv.state != ss_cinematic)
        return;     // can't nextserver while playing a normal game

    if (sv.state == ss_pic && !Cvar_VariableInteger("coop"))
        return;     // ss_pic can be nextserver'd in coop mode

    if (Q_atoi(Cmd_Argv(1)) != sv.spawncount)
        return;     // leftover from last server

    if (sv.nextserver_pending)
        return;

    sv.nextserver_pending = true;   // make sure another doesn't sneak in

    const char *v = Cvar_VariableString("nextserver");
    if (*v) {
        Cbuf_AddText(&cmd_buffer, v);
        Cbuf_AddText(&cmd_buffer, "\n");
    } else {
        Cbuf_AddText(&cmd_buffer, "killserver\n");
    }

    Cvar_Set("nextserver", "");
}

// the client is going to disconnect, so remove the connection immediately
static void SV_Disconnect_f(void)
{
    SV_DropClient(sv_client, "!?disconnected");
    SV_RemoveClient(sv_client);   // don't bother with zombie state
}

// dumps the serverinfo info string
static void SV_ShowServerInfo_f(void)
{
    char serverinfo[MAX_INFO_STRING];

    Cvar_BitInfo(serverinfo, CVAR_SERVERINFO);

    SV_ClientRedirect();
    Info_Print(serverinfo);
    Com_EndRedirect();
}

// dumps misc protocol info
static void SV_ShowMiscInfo_f(void)
{
    SV_ClientRedirect();
    SV_PrintMiscInfo();
    Com_EndRedirect();
}

static void SV_NoGameData_f(void)
{
    sv_client->nodata ^= 1;
}

static void SV_Lag_f(void)
{
    client_t *cl;

    if (Cmd_Argc() > 1) {
        SV_ClientRedirect();
        cl = SV_GetPlayer(Cmd_Argv(1), true);
        Com_EndRedirect();
        if (!cl) {
            return;
        }
    } else {
        cl = sv_client;
    }

    SV_ClientCommand(sv_client, "print \""
                    "Lag stats for:       %s\n"
                    "RTT (min/avg/max):   %d/%d/%d ms\n"
                    "Server to client PL: %.2f%% (approx)\n"
                    "Client to server PL: %.2f%%\n"
                    "Timescale          : %.3f\n\"",
                    cl->name, cl->min_ping, AVG_PING(cl), cl->max_ping,
                    PL_S2C(cl), PL_C2S(cl), cl->timescale);
}

static const ucmd_t ucmds[] = {
    // auto issued
    { "new", SV_New_f },
    { "begin", SV_Begin_f },
    { "nextserver", SV_NextServer_f },
    { "disconnect", SV_Disconnect_f },

    // issued by hand at client consoles
    { "info", SV_ShowServerInfo_f },
    { "sinfo", SV_ShowMiscInfo_f },

    { "nogamedata", SV_NoGameData_f },
    { "lag", SV_Lag_f },

    { NULL, NULL }
};

/*
==================
SV_ExecuteUserCommand
==================
*/
static void SV_ExecuteUserCommand(const char *s)
{
    const ucmd_t *u;
    char *c;

    Cmd_TokenizeString(s, false);

    c = Cmd_Argv(0);
    if (!c[0]) {
        return;
    }

    if ((u = Com_Find(ucmds, c)) != NULL) {
        if (u->func) {
            u->func();
        }
        return;
    }

    if (sv.state == ss_pic || sv.state == ss_cinematic) {
        return;
    }

    if (sv_client->state != cs_spawned && !sv_allow_unconnected_cmds->integer) {
        return;
    }

    if (!strcmp(c, "say") || !strcmp(c, "say_team")) {
        // don't timeout. only chat commands count as activity.
        sv_client->lastactivity = svs.realtime;
    }

    ge->ClientCommand(sv_client->number);
}

/*
===========================================================================

USER CMD EXECUTION

===========================================================================
*/

static bool     moveIssued;
static int      userinfoUpdateCount;

/*
==================
SV_ClientThink
==================
*/
static inline void SV_ClientThink(usercmd_t *cmd)
{
    usercmd_t *old = &sv_client->lastcmd;

    sv_client->command_msec -= cmd->msec;
    sv_client->cmd_msec_used += cmd->msec;
    sv_client->num_moves++;

    if (sv_client->command_msec < 0 && sv_enforcetime->integer) {
        Com_DPrintf("commandMsec underflow from %s: %d\n",
                    sv_client->name, sv_client->command_msec);
        return;
    }

    if (cmd->buttons != old->buttons
        || cmd->forwardmove != old->forwardmove
        || cmd->sidemove != old->sidemove
        || cmd->upmove != old->upmove) {
        // don't timeout
        sv_client->lastactivity = svs.realtime;
    }

    if (cmd != old)
        *old = *cmd;
    ge->ClientThink(sv_client->number);
}

static void SV_SetLastFrame(int lastframe)
{
    client_frame_t *frame;

    if (lastframe > 0) {
        int nextframe = sv_client->netchan.outgoing_sequence;

        if (lastframe >= nextframe)
            return; // ignore invalid acks

        if (lastframe <= sv_client->lastframe)
            return; // ignore duplicate acks

        if (nextframe - lastframe <= UPDATE_BACKUP) {
            frame = &sv_client->frames[lastframe & UPDATE_MASK];

            if (frame->number == lastframe) {
                // save time for ping calc
                if (frame->sentTime <= com_eventTime)
                    frame->latency = com_eventTime - frame->sentTime;
            }
        }

        // count valid ack
        sv_client->frames_acked++;
    }

    sv_client->lastframe = lastframe;
}

/*
==================
SV_NewClientExecuteMove
==================
*/
static void SV_NewClientExecuteMove(int c)
{
    usercmd_t   cmds[MAX_PACKET_FRAMES][MAX_PACKET_USERCMDS];
    usercmd_t   *lastcmd, *cmd;
    int         lastframe;
    int         numCmds[MAX_PACKET_FRAMES], numDups;
    int         i, j, lightlevel;
    int         net_drop;

    if (moveIssued) {
        SV_DropClient(sv_client, "multiple clc_move commands in packet");
        return;     // someone is trying to cheat...
    }

    moveIssued = true;

    if (c == clc_move_nodelta) {
        lastframe = -1;
    } else {
        lastframe = sv_client->netchan.incoming_acknowledged;
    }

    lightlevel = MSG_ReadByte();

    numDups = MSG_ReadBits(3);
    if (numDups >= MAX_PACKET_FRAMES) {
        SV_DropClient(sv_client, "too many frames in packet");
        return;
    }

    // read all cmds
    lastcmd = NULL;
    for (i = 0; i <= numDups; i++) {
        numCmds[i] = MSG_ReadBits(5);
        if (msg_read.readcount > msg_read.cursize) {
            SV_DropClient(sv_client, "read past end of message");
            return;
        }
        if (numCmds[i] >= MAX_PACKET_USERCMDS) {
            SV_DropClient(sv_client, "too many usercmds in frame");
            return;
        }
        for (j = 0; j < numCmds[i]; j++) {
            if (msg_read.readcount > msg_read.cursize) {
                SV_DropClient(sv_client, "read past end of message");
                return;
            }
            cmd = &cmds[i][j];
            MSG_ReadDeltaUsercmd(lastcmd, cmd);
            cmd->lightlevel = lightlevel;
            lastcmd = cmd;
        }
    }

    if (sv_client->state != cs_spawned) {
        SV_SetLastFrame(-1);
        return;
    }

    SV_SetLastFrame(lastframe);

    if (q_unlikely(!lastcmd)) {
        return; // should never happen
    }

    net_drop = sv_client->netchan.dropped;
    if (net_drop > numDups) {
        sv_client->frameflags |= FF_CLIENTPRED;
    }

    if (net_drop < 20) {
        // run lastcmd multiple times if no backups available
        while (net_drop > numDups) {
            SV_ClientThink(&sv_client->lastcmd);
            net_drop--;
        }

        // run backup cmds, if any
        while (net_drop > 0) {
            i = numDups - net_drop;
            for (j = 0; j < numCmds[i]; j++) {
                SV_ClientThink(&cmds[i][j]);
            }
            net_drop--;
        }

    }

    // run new cmds
    for (j = 0; j < numCmds[numDups]; j++) {
        SV_ClientThink(&cmds[numDups][j]);
    }

    sv_client->lastcmd = *lastcmd;
}

/*
=================
SV_UpdateUserinfo

Ensures that userinfo is valid and name is properly set.
=================
*/
static void SV_UpdateUserinfo(void)
{
    char *s;

    if (!sv_client->userinfo[0]) {
        SV_DropClient(sv_client, "empty userinfo");
        return;
    }

    if (!Info_Validate(sv_client->userinfo)) {
        SV_DropClient(sv_client, "malformed userinfo");
        return;
    }

    // validate name
    s = Info_ValueForKey(sv_client->userinfo, "name");
    s[MAX_CLIENT_NAME - 1] = 0;
    if (COM_IsWhite(s) || (sv_client->name[0] && strcmp(sv_client->name, s) &&
                           SV_RateLimited(&sv_client->ratelimit_namechange))) {
        if (!sv_client->name[0]) {
            SV_DropClient(sv_client, "malformed name");
            return;
        }
        if (!Info_SetValueForKey(sv_client->userinfo, "name", sv_client->name)) {
            SV_DropClient(sv_client, "oversize userinfo");
            return;
        }
        if (COM_IsWhite(s))
            SV_ClientCommand(sv_client, "print \"You can't have an empty name.\n\"");
        else
            SV_ClientCommand(sv_client, "print \"You can't change your name too often.\n\"");
        SV_ClientCommand(sv_client, "stuff set name \"%s\"\n", sv_client->name);
    }

    SV_UserinfoChanged(sv_client);
}

static void SV_ParseFullUserinfo(void)
{
    // malicious users may try sending too many userinfo updates
    if (userinfoUpdateCount >= MAX_PACKET_USERINFOS) {
        Com_DPrintf("Too many userinfos from %s\n", sv_client->name);
        MSG_ReadString(NULL, 0);
        return;
    }

    if (MSG_ReadString(sv_client->userinfo, sizeof(sv_client->userinfo)) >= sizeof(sv_client->userinfo)) {
        SV_DropClient(sv_client, "oversize userinfo");
        return;
    }

    Com_DDPrintf("%s(%s): %s [%d]\n", __func__,
                 sv_client->name, COM_MakePrintable(sv_client->userinfo), userinfoUpdateCount);

    SV_UpdateUserinfo();
    userinfoUpdateCount++;
}

static void SV_ParseDeltaUserinfo(void)
{
    char key[MAX_INFO_KEY], value[MAX_INFO_VALUE];

    // malicious users may try sending too many userinfo updates
    if (userinfoUpdateCount >= MAX_PACKET_USERINFOS) {
        Com_DPrintf("Too many userinfos from %s\n", sv_client->name);
        MSG_ReadString(NULL, 0);
        MSG_ReadString(NULL, 0);
        return;
    }

    // optimize by combining multiple delta updates into one (hack)
    while (1) {
        if (MSG_ReadString(key, sizeof(key)) >= sizeof(key)) {
            SV_DropClient(sv_client, "oversize userinfo key");
            return;
        }

        if (MSG_ReadString(value, sizeof(value)) >= sizeof(value)) {
            SV_DropClient(sv_client, "oversize userinfo value");
            return;
        }

        if (userinfoUpdateCount < MAX_PACKET_USERINFOS) {
            if (!Info_SetValueForKey(sv_client->userinfo, key, value)) {
                SV_DropClient(sv_client, "malformed userinfo");
                return;
            }

            Com_DDPrintf("%s(%s): %s %s [%d]\n", __func__,
                         sv_client->name, key, value, userinfoUpdateCount);

            userinfoUpdateCount++;
        } else {
            Com_DPrintf("Too many userinfos from %s\n", sv_client->name);
        }

        if (msg_read.readcount >= msg_read.cursize)
            break; // end of message

        if (msg_read.data[msg_read.readcount] != clc_userinfo_delta)
            break; // not delta userinfo

        msg_read.readcount++;
    }

    SV_UpdateUserinfo();
}

static void SV_ParseClientCommand(void)
{
    char buffer[MAX_STRING_CHARS];

    if (MSG_ReadString(buffer, sizeof(buffer)) >= sizeof(buffer)) {
        SV_DropClient(sv_client, "oversize stringcmd");
        return;
    }

    // malicious users may try using too many string commands
    if (stringCmdCount >= MAX_PACKET_STRINGCMDS) {
        Com_DPrintf("Too many stringcmds from %s\n", sv_client->name);
        return;
    }

    Com_DDPrintf("%s(%s): %s\n", __func__, sv_client->name, COM_MakePrintable(buffer));

    SV_ExecuteUserCommand(buffer);
    stringCmdCount++;
}

/*
===================
SV_ExecuteClientMessage

The current net_message is parsed for the given client
===================
*/
void SV_ExecuteClientMessage(client_t *client)
{
    int c;

    sv_client = client;

    // only allow one move command
    moveIssued = false;
    stringCmdCount = 0;
    userinfoUpdateCount = 0;

    while (1) {
        if (msg_read.readcount > msg_read.cursize) {
            SV_DropClient(client, "read past end of message");
            break;
        }

        c = MSG_ReadByte();
        if (c == -1)
            break;

        switch (c) {
        default:
            SV_DropClient(client, "unknown command byte");
            break;

        case clc_nop:
            break;

        case clc_userinfo:
            SV_ParseFullUserinfo();
            break;

        case clc_move_nodelta:
        case clc_move_batched:
            SV_NewClientExecuteMove(c);
            break;

        case clc_stringcmd:
            SV_ParseClientCommand();
            break;

        case clc_userinfo_delta:
            SV_ParseDeltaUserinfo();
            break;
        }

        if (client->state <= cs_zombie)
            break;    // disconnect command
    }

    sv_client = NULL;
}
