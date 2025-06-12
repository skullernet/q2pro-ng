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
// cl_main.c  -- client main loop

#include "client.h"

cvar_t  *rcon_address;

cvar_t  *cl_timeout;
cvar_t  *cl_warn_on_fps_rounding;
cvar_t  *cl_maxfps;
cvar_t  *cl_async;
cvar_t  *r_maxfps;
cvar_t  *cl_autopause;

#if USE_DEBUG
cvar_t  *cl_shownet;
cvar_t  *cl_showmiss;
cvar_t  *cl_showclamp;
#endif

cvar_t  *cl_disconnectcmd;
cvar_t  *cl_changemapcmd;
cvar_t  *cl_beginmapcmd;

cvar_t  *cl_allow_vid_restart;

#if USE_FPS
cvar_t  *cl_updaterate;
#endif

cvar_t  *cl_protocol;

cvar_t  *gender_auto;

//
// userinfo
//
cvar_t  *info_password;
cvar_t  *info_spectator;
cvar_t  *info_name;
cvar_t  *info_skin;
cvar_t  *info_rate;
cvar_t  *info_fov;
cvar_t  *info_msg;
cvar_t  *info_hand;
cvar_t  *info_gender;
cvar_t  *info_uf;

client_static_t cls;
client_state_t  cl;

//======================================================================

typedef enum {
    REQ_FREE,
    REQ_STATUS_CL,
    REQ_STATUS_UI,
    REQ_INFO,
    REQ_RCON
} requestType_t;

typedef struct {
    requestType_t type;
    netadr_t adr;
    unsigned time;
} request_t;

#define MAX_REQUESTS    64
#define REQUEST_MASK    (MAX_REQUESTS - 1)

static request_t    clientRequests[MAX_REQUESTS];
static unsigned     nextRequest;

static request_t *CL_AddRequest(const netadr_t *adr, requestType_t type)
{
    request_t *r;

    r = &clientRequests[nextRequest++ & REQUEST_MASK];
    r->adr = *adr;
    r->type = type;
    r->time = cls.realtime;

    return r;
}

static request_t *CL_FindRequest(void)
{
    request_t *r;
    int i;

    // find the most recent request sent to this address
    for (i = 0; i < MAX_REQUESTS; i++) {
        r = &clientRequests[(nextRequest - i - 1) & REQUEST_MASK];
        if (!r->type) {
            continue;
        }
        if (r->adr.type == NA_BROADCAST) {
            if (cls.realtime - r->time > 3000) {
                continue;
            }
            if (!NET_IsLanAddress(&net_from)) {
                continue;
            }
        } else {
            if (cls.realtime - r->time > 6000) {
                break;
            }
            if (!NET_IsEqualBaseAdr(&net_from, &r->adr)) {
                continue;
            }
        }

        return r;
    }

    return NULL;
}

//======================================================================

/*
===================
CL_ClientCommand
===================
*/
void CL_ClientCommand(const char *string)
{
    if (!cls.netchan.init) {
        return;
    }

    Com_DDPrintf("%s: %s\n", __func__, COM_MakePrintable(string));

    MSG_WriteByte(clc_stringcmd);
    MSG_WriteString(string);
    MSG_FlushTo(&cls.netchan.message);
}

/*
===================
CL_ForwardToServer

adds the current command line as a clc_stringcmd to the client message.
things like godmode, noclip, etc, are commands directed to the server,
so when they are typed in at the console, they will need to be forwarded.
===================
*/
bool CL_ForwardToServer(void)
{
    char    *cmd;

    cmd = Cmd_Argv(0);
    if (cls.state != ca_active || *cmd == '-' || *cmd == '+') {
        return false;
    }

    if (cge->ConsoleCommand())
        return true;

    CL_ClientCommand(Cmd_RawArgsFrom(0));
    return true;
}

/*
==================
CL_ForwardToServer_f
==================
*/
static void CL_ForwardToServer_f(void)
{
    if (cls.state < ca_connected) {
        Com_Printf("Can't \"%s\", not connected\n", Cmd_Argv(0));
        return;
    }

    if (cls.demo.playback) {
        return;
    }

    // don't forward the first argument
    if (Cmd_Argc() > 1) {
        CL_ClientCommand(Cmd_RawArgs());
    }
}

/*
==================
CL_Pause_f
==================
*/
static void CL_Pause_f(void)
{
    // activate manual pause
    if (cl_paused->integer == 2) {
        Cvar_Set("cl_paused", "0");
    } else {
        Cvar_Set("cl_paused", "2");
    }

    CL_CheckForPause();
}

/*
=================
CL_CheckForResend

Resend a connect message if the last one has timed out
=================
*/
void CL_CheckForResend(void)
{
    char userinfo[MAX_INFO_STRING];
    int maxmsglen;

    if (cls.demo.playback) {
        return;
    }

    // if the local server is running and we aren't
    // then connect
    if (cls.state < ca_connecting && sv_running->integer > ss_loading) {
        strcpy(cls.servername, "localhost");
        cls.serverAddress.type = NA_LOOPBACK;
        cls.serverProtocol = PROTOCOL_VERSION_MAJOR;

        // we don't need a challenge on the localhost
        cls.state = ca_connecting;
        cls.connect_time -= CONNECT_FAST;
        cls.connect_count = 0;

        cls.passive = false;

        Con_Popup(true);
        UI_OpenMenu(UIMENU_NONE);
    }

    // resend if we haven't gotten a reply yet
    if (cls.state != ca_connecting && cls.state != ca_challenging) {
        return;
    }

    if (cls.realtime - cls.connect_time < CONNECT_DELAY) {
        return;
    }

    cls.connect_time = cls.realtime;    // for retransmit requests
    cls.connect_count++;

    if (cls.state == ca_challenging) {
        Com_Printf("Requesting challenge... %i\n", cls.connect_count);
        OOB_PRINT(NS_CLIENT, &cls.serverAddress, "getchallenge\n");
        return;
    }

    //
    // We have gotten a challenge from the server, so try and connect.
    //
    Com_Printf("Requesting connection... %i\n", cls.connect_count);

    cls.userinfo_modified = 0;
    cls.quakePort = net_qport->integer & 0xff;

    // use maximum allowed msglen for loopback
    maxmsglen = net_maxmsglen->integer;
    if (NET_IsLocalAddress(&cls.serverAddress)) {
        maxmsglen = MAX_PACKETLEN_WRITABLE;
    }

    Cvar_BitInfo(userinfo, CVAR_USERINFO);
    Netchan_OutOfBand(NS_CLIENT, &cls.serverAddress,
                      "connect %i %i %i \"%s\" %i %i %i\n", PROTOCOL_VERSION_MAJOR, cls.quakePort,
                      cls.challenge, userinfo, PROTOCOL_VERSION_MINOR, maxmsglen, USE_ZLIB);
}

static void CL_RecentIP_g(genctx_t *ctx)
{
    for (int i = 0; i < RECENT_ADDR; i++) {
        const netadr_t *a = &cls.recent_addr[(cls.recent_head - i - 1) & RECENT_MASK];
        if (a->type) {
            Prompt_AddMatch(ctx, NET_AdrToString(a));
        }
    }
}

static void CL_Connect_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        CL_RecentIP_g(ctx);
        Com_Address_g(ctx);
    }
}

/*
================
CL_Connect_f

================
*/
static void CL_Connect_f(void)
{
    char    *server, *p;
    netadr_t    address;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <server>\n", Cmd_Argv(0));
        return;
    }

    if (Cmd_Argc() > 2) {
        Com_Printf("Second argument to `%s' is now ignored. "
                   "Set protocol via `cl_protocol' variable.\n", Cmd_Argv(0));
    }

    server = Cmd_Argv(1);

    // support quake2://<address>[/] scheme
    if (!Q_strncasecmp(server, "quake2://", 9)) {
        server += 9;
        if ((p = strchr(server, '/')) != NULL) {
            *p = 0;
        }
    }

    if (!NET_StringToAdr(server, &address, PORT_SERVER)) {
        Com_Printf("Bad server address\n");
        return;
    }

    // copy early to avoid potential cmd_argv[1] clobbering
    Q_strlcpy(cls.servername, server, sizeof(cls.servername));

    // if running a local server, kill it and reissue
    SV_Shutdown("Server was killed.\n", ERR_DISCONNECT);

    NET_Config(NET_CLIENT);

    CL_Disconnect(ERR_RECONNECT);

    cls.serverAddress = address;
    cls.serverProtocol = cl_protocol->integer;
    cls.passive = false;
    cls.state = ca_challenging;
    cls.connect_time -= CONNECT_FAST;
    cls.connect_count = 0;

    Con_Popup(true);

    CL_CheckForResend();

    Cvar_Set("timedemo", "0");
}

static void CL_FollowIP_f(void)
{
    const netadr_t *a;
    int i = 0;

    if (Cmd_Argc() > 1) {
        // optional second argument references less recent address
        i = Q_clip(Q_atoi(Cmd_Argv(1)), 0, RECENT_ADDR - 1);
    }

    a = &cls.recent_addr[(cls.recent_head - i - 1) & RECENT_MASK];
    if (a->type) {
        const char *s = NET_AdrToString(a);
        Com_Printf("Following %s...\n", s);
        Cbuf_InsertText(cmd_current, va("connect %s\n", s));
    } else {
        Com_Printf("No IP address to follow.\n");
    }
}

static void CL_PassiveConnect_f(void)
{
    netadr_t address;

    if (cls.passive) {
        cls.passive = false;
        Com_Printf("No longer listening for passive connections.\n");
        return;
    }

    // if running a local server, kill it and reissue
    SV_Shutdown("Server was killed.\n", ERR_DISCONNECT);

    NET_Config(NET_CLIENT);

    CL_Disconnect(ERR_RECONNECT);

    if (!NET_GetAddress(NS_CLIENT, &address)) {
        return;
    }

    cls.passive = true;
    Com_Printf("Listening for passive connections at %s.\n",
               NET_AdrToString(&address));
}

void CL_SendRcon(const netadr_t *adr, const char *pass, const char *cmd)
{
    NET_Config(NET_CLIENT);

    CL_AddRequest(adr, REQ_RCON);

    Netchan_OutOfBand(NS_CLIENT, adr, "rcon \"%s\" %s", pass, cmd);
}


/*
=====================
CL_Rcon_f

  Send the rest of the command line over as
  an unconnected command.
=====================
*/
static void CL_Rcon_f(void)
{
    netadr_t    address;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <command>\n", Cmd_Argv(0));
        return;
    }

    if (!rcon_password->string[0]) {
        Com_Printf("You must set 'rcon_password' before "
                   "issuing an rcon command.\n");
        return;
    }

    address = cls.netchan.remote_address;
    if (!address.type) {
        if (!rcon_address->string[0]) {
            Com_Printf("You must either be connected, "
                       "or set the 'rcon_address' cvar "
                       "to issue rcon commands.\n");
            return;
        }
        if (!NET_StringToAdr(rcon_address->string, &address, PORT_SERVER)) {
            Com_Printf("Bad address: %s\n", rcon_address->string);
            return;
        }
    }

    CL_SendRcon(&address, rcon_password->string, COM_StripQuotes(Cmd_RawArgs()));
}

static void CL_Rcon_c(genctx_t *ctx, int argnum)
{
    Com_Generic_c(ctx, argnum - 1);
}

/*
=====================
CL_ClearState

=====================
*/
void CL_ClearState(void)
{
    S_StopAllSounds();
    SCR_StopCinematic();
    CL_FreeDemoSnapshots();

    // wipe the entire cl structure
    BSP_Free(cl.bsp);
    memset(&cl, 0, sizeof(cl));

    if (cls.state > ca_connected) {
        cls.state = ca_connected;
        CL_CheckForPause();
        CL_UpdateFrameTimes();
    }

    // unprotect game cvar
    fs_game->flags &= ~CVAR_ROM;
}

/*
=====================
CL_Disconnect

Goes from a connected state to full screen console state
Sends a disconnect message to the server
This is also called on Com_Error, so it shouldn't cause any errors
=====================
*/
void CL_Disconnect(error_type_t type)
{
    if (!cls.state) {
        return;
    }

    SCR_EndLoadingPlaque(); // get rid of loading plaque

    if (cls.state > ca_disconnected && !cls.demo.playback) {
        EXEC_TRIGGER(cl_disconnectcmd);
    }

    //cls.connect_time = 0;
    //cls.connect_count = 0;
    cls.passive = false;
#if USE_ICMP
    cls.errorReceived = false;
#endif

    if (cls.netchan.init) {
        // send a disconnect message to the server
        MSG_WriteByte(clc_stringcmd);
        MSG_WriteData("disconnect", 11);

        Netchan_Transmit(&cls.netchan, msg_write.cursize, msg_write.data, 3);

        SZ_Clear(&msg_write);

        Netchan_Close(&cls.netchan);
    }

    // stop playback and/or recording
    CL_CleanupDemos();

    // stop download
    //CL_CleanupDownloads();

    CL_ClearState();

    CL_ShutdownCGame();

    cls.state = ca_disconnected;
    cls.userinfo_modified = 0;

    // start menu track, or stop music
    if (type == ERR_DROP || type == ERR_DISCONNECT) {
        OGG_Play();
    }

    if (type == ERR_DISCONNECT) {
        UI_OpenMenu(UIMENU_DEFAULT);
    } else {
        UI_OpenMenu(UIMENU_NONE);
    }

    CL_CheckForPause();

    CL_UpdateFrameTimes();
}

/*
================
CL_Disconnect_f
================
*/
static void CL_Disconnect_f(void)
{
    if (cls.state > ca_disconnected) {
        Com_Error(ERR_DISCONNECT, "Disconnected from server");
    }
}

static void CL_ServerStatus_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        CL_RecentIP_g(ctx);
        Com_Address_g(ctx);
    }
}

/*
================
CL_ServerStatus_f
================
*/
static void CL_ServerStatus_f(void)
{
    char        *s;
    netadr_t    adr;

    if (Cmd_Argc() < 2) {
        adr = cls.netchan.remote_address;
        if (!adr.type) {
            Com_Printf("Usage: %s [address]\n", Cmd_Argv(0));
            return;
        }
    } else {
        s = Cmd_Argv(1);
        if (!NET_StringToAdr(s, &adr, PORT_SERVER)) {
            Com_Printf("Bad address: %s\n", s);
            return;
        }
    }

    CL_AddRequest(&adr, REQ_STATUS_CL);

    NET_Config(NET_CLIENT);

    OOB_PRINT(NS_CLIENT, &adr, "status");
}

/*
====================
SortPlayers
====================
*/
static int SortPlayers(const void *v1, const void *v2)
{
    const playerStatus_t *p1 = (const playerStatus_t *)v1;
    const playerStatus_t *p2 = (const playerStatus_t *)v2;

    if (p1->score < p2->score)
        return 1;
    if (p1->score > p2->score)
        return -1;
    return 0;
}

/*
====================
CL_ParseStatusResponse
====================
*/
static void CL_ParseStatusResponse(serverStatus_t *status, const char *string)
{
    playerStatus_t *player;
    const char *s;
    size_t infolen;

    // parse '\n' terminated infostring
    s = Q_strchrnul(string, '\n');

    // due to off-by-one error in the original version of Info_SetValueForKey,
    // some servers produce infostrings up to 512 characters long. work this
    // bug around by cutting off the last character(s).
    infolen = s - string;
    if (infolen >= MAX_INFO_STRING)
        infolen = MAX_INFO_STRING - 1;

    // copy infostring off
    memcpy(status->infostring, string, infolen);
    status->infostring[infolen] = 0;

    if (!Info_Validate(status->infostring))
        strcpy(status->infostring, "\\hostname\\badinfo");

    // parse optional player list
    status->numPlayers = 0;
    while (status->numPlayers < MAX_STATUS_PLAYERS) {
        player = &status->players[status->numPlayers];
        player->score = Q_atoi(COM_Parse(&s));
        player->ping = Q_atoi(COM_Parse(&s));
        COM_ParseToken(&s, player->name, sizeof(player->name));
        if (!s)
            break;
        status->numPlayers++;
    }

    // sort players by frags
    qsort(status->players, status->numPlayers,
          sizeof(status->players[0]), SortPlayers);
}

static void CL_DumpStatusResponse(const serverStatus_t *status)
{
    int i;

    Com_Printf("Status response from %s\n\n", NET_AdrToString(&net_from));

    Info_Print(status->infostring);

    Com_Printf("\nNum Score Ping Name\n");
    for (i = 0; i < status->numPlayers; i++) {
        Com_Printf("%3i %5i %4i %s\n", i + 1,
                   status->players[i].score,
                   status->players[i].ping,
                   status->players[i].name);
    }
}

/*
====================
CL_ParsePrintMessage
====================
*/
static void CL_ParsePrintMessage(void)
{
    char string[MAX_NET_STRING];
    serverStatus_t status;
    request_t *r;

    MSG_ReadString(string, sizeof(string));

    r = CL_FindRequest();
    if (r) {
        switch (r->type) {
        case REQ_STATUS_CL:
            CL_ParseStatusResponse(&status, string);
            CL_DumpStatusResponse(&status);
            break;
#if USE_UI
        case REQ_STATUS_UI:
            CL_ParseStatusResponse(&status, string);
            UI_StatusEvent(&status);
            break;
#endif
        case REQ_RCON:
            Com_Printf("%s", string);
            return; // rcon may come in multiple packets

        default:
            return;
        }

        if (r->adr.type != NA_BROADCAST)
            r->type = REQ_FREE;
        return;
    }

    // finally, check is this is response from the server we are connecting to
    // and if so, start channenge cycle again
    if ((cls.state == ca_challenging || cls.state == ca_connecting) &&
        NET_IsEqualBaseAdr(&net_from, &cls.serverAddress)) {
        Com_Printf("%s", string);
        cls.state = ca_challenging;
        //cls.connect_count = 0;
        return;
    }

    Com_DPrintf("%s: dropped unrequested packet\n", __func__);
}

/*
=================
CL_ParseInfoMessage

Handle a reply from a ping
=================
*/
static void CL_ParseInfoMessage(void)
{
    char string[MAX_QPATH];
    request_t *r;

    r = CL_FindRequest();
    if (!r)
        return;
    if (r->type != REQ_INFO)
        return;

    MSG_ReadString(string, sizeof(string));
    Com_Printf("%s", string);
    if (r->adr.type != NA_BROADCAST)
        r->type = REQ_FREE;
}

/*
=================
CL_Changing_f

Just sent as a hint to the client that they should
drop to full console
=================
*/
void CL_Changing_f(void)
{
    int i, j;
    char *s;

    if (cls.state < ca_connected) {
        return;
    }

    if (cls.demo.recording)
        CL_Stop_f();

    Com_Printf("Changing map...\n");

    if (!cls.demo.playback) {
        EXEC_TRIGGER(cl_changemapcmd);
        Cmd_ExecTrigger("#cl_changelevel");
    }

    SCR_BeginLoadingPlaque();

    cls.state = ca_connected;   // not active anymore, but not disconnected
    cl.mapname[0] = 0;
    cl.configstrings[CS_NAME][0] = 0;

    CL_CheckForPause();

    CL_UpdateFrameTimes();

    // parse additional parameters
    j = Cmd_Argc();
    for (i = 1; i < j; i++) {
        s = Cmd_Argv(i);
        if (!strncmp(s, "map=", 4)) {
            Q_strlcpy(cl.mapname, s + 4, sizeof(cl.mapname));
        }
    }

    SCR_UpdateScreen();
}


/*
=================
CL_Reconnect_f

The server is changing levels
=================
*/
void CL_Reconnect_f(void)
{
    if (cls.demo.playback) {
        Com_Printf("No server to reconnect to.\n");
        return;
    }

    if (cls.state >= ca_precached || Cmd_From() != FROM_STUFFTEXT) {
        CL_Disconnect(ERR_RECONNECT);
    }

    if (cls.state >= ca_connected) {
        cls.state = ca_connected;

        if (cls.download.file) {
            return; // if we are downloading, we don't change!
        }

        Com_Printf("Reconnecting...\n");

        CL_ClientCommand("new");
        return;
    }

    // issued manually at console
    if (cls.serverAddress.type == NA_UNSPECIFIED) {
        Com_Printf("No server to reconnect to.\n");
        return;
    }
    if (cls.serverAddress.type == NA_LOOPBACK && !sv_running->integer) {
        Com_Printf("Can not reconnect to loopback.\n");
        return;
    }

    Com_Printf("Reconnecting...\n");

    cls.serverProtocol = cl_protocol->integer;
    cls.state = ca_challenging;
    cls.connect_time -= CONNECT_FAST;
    cls.connect_count = 0;

    SCR_UpdateScreen();
}

#if USE_UI
/*
=================
CL_SendStatusRequest
=================
*/
void CL_SendStatusRequest(const netadr_t *address)
{
    NET_Config(NET_CLIENT);

    CL_AddRequest(address, REQ_STATUS_UI);

    OOB_PRINT(NS_CLIENT, address, "status");
}
#endif

/*
=================
CL_PingServers_f
=================
*/
static void CL_PingServers_f(void)
{
    netadr_t address;
    cvar_t *var;
    int i;

    NET_Config(NET_CLIENT);

    // send a broadcast packet
    memset(&address, 0, sizeof(address));
    address.type = NA_BROADCAST;
    address.port = BigShort(PORT_SERVER);

    Com_DPrintf("Pinging broadcast...\n");
    CL_AddRequest(&address, REQ_INFO);

    OOB_PRINT(NS_CLIENT, &address, "info 34");

    // send a packet to each address book entry
    for (i = 0; i < 64; i++) {
        var = Cvar_FindVar(va("adr%i", i));
        if (!var)
            break;

        if (!var->string[0])
            continue;

        if (!NET_StringToAdr(var->string, &address, PORT_SERVER)) {
            Com_Printf("Bad address: %s\n", var->string);
            continue;
        }

        Com_DPrintf("Pinging %s...\n", var->string);
        CL_AddRequest(&address, REQ_INFO);

        OOB_PRINT(NS_CLIENT, &address, "info 34");
    }
}

/*
=================
CL_ConnectionlessPacket

Responses to broadcasts, etc
=================
*/
static void CL_ConnectionlessPacket(void)
{
    char    string[MAX_STRING_CHARS];
    char    *s, *c;
    int     i, j;

    MSG_BeginReading();
    MSG_ReadLong(); // skip the -1

    if (MSG_ReadStringLine(string, sizeof(string)) >= sizeof(string)) {
        Com_DPrintf("Oversize message received.  Ignored.\n");
        return;
    }

    Cmd_TokenizeString(string, false);

    c = Cmd_Argv(0);

    Com_DPrintf("%s: %s\n", NET_AdrToString(&net_from), COM_MakePrintable(string));

    // challenge from the server we are connecting to
    if (!strcmp(c, "challenge")) {
        if (cls.state < ca_challenging) {
            Com_DPrintf("Challenge received while not connecting.  Ignored.\n");
            return;
        }
        if (!NET_IsEqualBaseAdr(&net_from, &cls.serverAddress)) {
            Com_DPrintf("Challenge from different address.  Ignored.\n");
            return;
        }
        if (cls.state > ca_challenging) {
            Com_DPrintf("Dup challenge received.  Ignored.\n");
            return;
        }

        cls.challenge = Q_atoi(Cmd_Argv(1));
        cls.state = ca_connecting;
        cls.connect_time -= CONNECT_INSTANT; // fire immediately
        //cls.connect_count = 0;
        cls.serverProtocol = PROTOCOL_VERSION_MAJOR;

        Com_DPrintf("Selected protocol %d\n", cls.serverProtocol);

        CL_CheckForResend();
        return;
    }

    // server connection
    if (!strcmp(c, "client_connect")) {
        char mapname[MAX_QPATH];
        bool got_server = false;

        if (cls.state < ca_connecting) {
            Com_DPrintf("Connect received while not connecting.  Ignored.\n");
            return;
        }
        if (!NET_IsEqualBaseAdr(&net_from, &cls.serverAddress)) {
            Com_DPrintf("Connect from different address.  Ignored.\n");
            return;
        }
        if (cls.state > ca_connecting) {
            Com_DPrintf("Dup connect received.  Ignored.\n");
            return;
        }

        mapname[0] = 0;

        // parse additional parameters
        j = Cmd_Argc();
        for (i = 1; i < j; i++) {
            s = Cmd_Argv(i);
            if (!strncmp(s, "map=", 4)) {
                Q_strlcpy(mapname, s + 4, sizeof(mapname));
            } else if (!strncmp(s, "dlserver=", 9)) {
                if (!got_server) {
                    HTTP_SetServer(s + 9);
                    got_server = true;
                }
            }
        }

        if (!got_server) {
            HTTP_SetServer(NULL);
        }

        Com_Printf("Connected to %s (protocol %d).\n",
                   NET_AdrToString(&cls.serverAddress), cls.serverProtocol);
        Netchan_Close(&cls.netchan);
        Netchan_Setup(&cls.netchan, NS_CLIENT, &cls.serverAddress, cls.quakePort, 1024);

        CL_ClientCommand("new");
        cls.state = ca_connected;
        cls.connect_count = 0;
        Q_strlcpy(cl.mapname, mapname, sizeof(cl.mapname)); // for levelshot screen
        return;
    }

    if (!strcmp(c, "passive_connect")) {
        const char *adr;

        if (!cls.passive) {
            Com_DPrintf("Passive connect received while not connecting.  Ignored.\n");
            return;
        }
        adr = NET_AdrToString(&net_from);
        Com_Printf("Received passive connect from %s.\n", adr);

        cls.serverAddress = net_from;
        cls.serverProtocol = cl_protocol->integer;
        Q_strlcpy(cls.servername, adr, sizeof(cls.servername));
        cls.passive = false;

        cls.state = ca_challenging;
        cls.connect_time -= CONNECT_FAST;
        cls.connect_count = 0;

        CL_CheckForResend();
        return;
    }

    // print command from somewhere
    if (!strcmp(c, "print")) {
        CL_ParsePrintMessage();
        return;
    }

    // server responding to a status broadcast
    if (!strcmp(c, "info")) {
        CL_ParseInfoMessage();
        return;
    }

    Com_DPrintf("Unknown connectionless packet command.\n");
}

/*
=================
CL_PacketEvent
=================
*/
static void CL_PacketEvent(void)
{
    if (msg_read.cursize < 4) {
        return;
    }

    //
    // remote command packet
    //
    if (*(int *)msg_read.data == -1) {
        CL_ConnectionlessPacket();
        return;
    }

    if (cls.state < ca_connected) {
        return;
    }

    if (cls.demo.playback) {
        return;     // dump it if not connected
    }

    if (msg_read.cursize < 8) {
        Com_DPrintf("%s: runt packet\n", NET_AdrToString(&net_from));
        return;
    }

    //
    // packet from server
    //
    if (!NET_IsEqualAdr(&net_from, &cls.netchan.remote_address)) {
        Com_DPrintf("%s: sequenced packet without connection\n",
                    NET_AdrToString(&net_from));
        return;
    }

    if (!Netchan_Process(&cls.netchan))
        return;     // wasn't accepted for some reason

#if USE_ICMP
    cls.errorReceived = false; // don't drop
#endif

    cl.suppress_count = 0;

    CL_ParseServerMessage();

    SCR_AddNetgraph();

    // if recording demo, write the message out
    if (cls.demo.recording && !cls.demo.paused) {
        CL_WriteDemoMessage(&cls.demo.buffer);
    }
}

#if USE_ICMP
void CL_ErrorEvent(const netadr_t *from)
{
    UI_ErrorEvent(from);

    //
    // error packet from server
    //
    if (cls.state < ca_connected) {
        return;
    }
    if (cls.demo.playback) {
        return;     // dump it if not connected
    }
    if (!NET_IsEqualBaseAdr(from, &cls.netchan.remote_address)) {
        return;
    }
    if (from->port && from->port != cls.netchan.remote_address.port) {
        return;
    }

    cls.errorReceived = true; // drop connection soon
}
#endif


//=============================================================================

/*
==============
CL_FixUpGender_f
==============
*/
static void CL_FixUpGender(void)
{
    char *p;
    char sk[MAX_QPATH];

    Q_strlcpy(sk, info_skin->string, sizeof(sk));
    if ((p = strchr(sk, '/')) != NULL)
        *p = 0;
    if (Q_stricmp(sk, "male") == 0 || Q_stricmp(sk, "cyborg") == 0)
        Cvar_Set("gender", "male");
    else if (Q_stricmp(sk, "female") == 0 || Q_stricmp(sk, "crackhor") == 0)
        Cvar_Set("gender", "female");
    else
        Cvar_Set("gender", "none");
    info_gender->modified = false;
}

void CL_UpdateUserinfo(cvar_t *var, from_t from)
{
    int i;

    if (var == info_skin && from > FROM_CONSOLE && gender_auto->integer) {
        CL_FixUpGender();
    }

    if (cls.state < ca_connected) {
        return;
    }

    if (cls.demo.playback) {
        return;
    }

    if (var->flags & CVAR_PRIVATE) {
        return;
    }

    if (cls.userinfo_modified == MAX_PACKET_USERINFOS) {
        // can't hold any more
        goto done;
    }

    // check for the same variable being modified twice
    for (i = 0; i < cls.userinfo_modified; i++) {
        if (cls.userinfo_updates[i] == var) {
            Com_DDPrintf("%s: %u: %s [DUP]\n",
                         __func__, com_framenum, var->name);
            return;
        }
    }

    cls.userinfo_updates[cls.userinfo_modified++] = var;

done:
    Com_DDPrintf("%s: %u: %s [%d]\n",
                 __func__, com_framenum, var->name, cls.userinfo_modified);
}

/*
==============
CL_Userinfo_f
==============
*/
static void CL_Userinfo_f(void)
{
    char userinfo[MAX_INFO_STRING];

    Cvar_BitInfo(userinfo, CVAR_USERINFO);

    Com_Printf("User info settings:\n");
    Info_Print(userinfo);
}

/*
=================
CL_RestartSound_f

Restart the sound subsystem so it can pick up
new parameters and flush all sounds
=================
*/
static void CL_RestartSound_f(void)
{
    S_Shutdown();
    S_Init();
    //CL_RegisterSounds();
}

/*
=================
CL_PlaySound_f

Moved here from sound code so that command is always registered.
=================
*/
static void CL_PlaySound_c(genctx_t *ctx, int state)
{
    FS_File_g("sound", ".wav", FS_SEARCH_RECURSIVE | FS_SEARCH_STRIPEXT, ctx);
}

static void CL_PlaySound_f(void)
{
    int     i;
    char name[MAX_QPATH];

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <sound> [...]\n", Cmd_Argv(0));
        return;
    }

    for (i = 1; i < Cmd_Argc(); i++) {
        Cmd_ArgvBuffer(i, name, sizeof(name));
        COM_DefaultExtension(name, ".wav", sizeof(name));
        S_StartLocalSound(name);
    }
}

static int precache_spawncount;

/*
=================
CL_Begin

Called after all downloads are done. Not used for demos.
=================
*/
void CL_Begin(void)
{
    Cvar_FixCheats();

    CL_InitCGame();
    cls.state = ca_precached;

    CL_ClientCommand(va("begin %i\n", precache_spawncount));
}

/*
=================
CL_Precache_f

The server will send this command right
before allowing the client into the server
=================
*/
void CL_Precache_f(void)
{
    if (cls.state < ca_connected) {
        return;
    }

    cls.state = ca_loading;

    S_StopAllSounds();

    // demos use different precache sequence
    if (cls.demo.playback) {
        CL_InitCGame();
        cls.state = ca_precached;
        return;
    }

    precache_spawncount = Q_atoi(Cmd_Argv(1));

    //CL_ResetPrecacheCheck();
    //CL_RequestNextDownload();
    CL_Begin();

    if (cls.state != ca_precached) {
        cls.state = ca_connected;
    }
}

static const cmd_option_t o_writeconfig[] = {
    { "a", "aliases", "write aliases" },
    { "b", "bindings", "write bindings" },
    { "c", "cvars", "write archived cvars" },
    { "h", "help", "display this help message" },
    { "m", "modified", "write modified cvars" },
    { NULL }
};

static void CL_WriteConfig_c(genctx_t *ctx, int argnum)
{
    Cmd_Option_c(o_writeconfig, Cmd_Config_g, ctx, argnum);
}

/*
===============
CL_WriteConfig_f
===============
*/
static void CL_WriteConfig_f(void)
{
    char buffer[MAX_OSPATH];
    bool aliases = false, bindings = false, modified = false;
    int c, mask = 0;
    qhandle_t f;

    while ((c = Cmd_ParseOptions(o_writeconfig)) != -1) {
        switch (c) {
        case 'a':
            aliases = true;
            break;
        case 'b':
            bindings = true;
            break;
        case 'c':
            mask |= CVAR_ARCHIVE;
            break;
        case 'h':
            Cmd_PrintUsage(o_writeconfig, "<filename>");
            Com_Printf("Save current configuration into file.\n");
            Cmd_PrintHelp(o_writeconfig);
            return;
        case 'm':
            modified = true;
            mask = ~0;
            break;
        default:
            return;
        }
    }

    if (!cmd_optarg[0]) {
        Com_Printf("Missing filename argument.\n");
        Cmd_PrintHint();
        return;
    }

    if (!aliases && !bindings && !mask) {
        bindings = true;
        mask = CVAR_ARCHIVE;
    }

    f = FS_EasyOpenFile(buffer, sizeof(buffer), FS_MODE_WRITE | FS_FLAG_TEXT,
                        "configs/", cmd_optarg, ".cfg");
    if (!f) {
        return;
    }

    FS_FPrintf(f, "// generated by q2pro\n");

    if (bindings) {
        FS_FPrintf(f, "\n// key bindings\n");
        Key_WriteBindings(f);
    }
    if (aliases) {
        FS_FPrintf(f, "\n// command aliases\n");
        Cmd_WriteAliases(f);
    }
    if (mask) {
        FS_FPrintf(f, "\n//%s cvars\n", modified ? "modified" : "archived");
        Cvar_WriteVariables(f, mask, modified);
    }

    if (FS_CloseFile(f))
        Com_EPrintf("Error writing %s\n", buffer);
    else
        Com_Printf("Wrote %s.\n", buffer);
}

static size_t CL_Mapname_m(char *buffer, size_t size)
{
    return Q_strlcpy(buffer, cl.mapname, size);
}

static size_t CL_Server_m(char *buffer, size_t size)
{
    return Q_strlcpy(buffer, cls.servername, size);
}

static size_t CL_Ups_m(char *buffer, size_t size)
{
#if 0
    vec3_t vel;

    if (!cls.demo.playback && cl_predict->integer &&
        !(cl.frame.ps.pmove.pm_flags & PMF_NO_PREDICTION)) {
        VectorCopy(cl.predicted_velocity, vel);
    } else {
        VectorCopy(cl.frame.ps.pmove.velocity, vel);
    }

    return Q_snprintf(buffer, size, "%.f", VectorLength(vel));
#endif
    return Q_strlcpy(buffer, "", size);
}

static size_t CL_Timer_m(char *buffer, size_t size)
{
    int hour, min, sec;

    sec = cl.time / 1000;
    min = sec / 60; sec %= 60;
    hour = min / 60; min %= 60;

    if (hour) {
        return Q_snprintf(buffer, size, "%i:%i:%02i", hour, min, sec);
    }
    return Q_snprintf(buffer, size, "%i:%02i", min, sec);
}

static size_t CL_DemoPos_m(char *buffer, size_t size)
{
    int sec, min, framenum = 0;

    if (cls.demo.playback)
        framenum = cls.demo.frames_read;

    sec = framenum / BASE_FRAMERATE; framenum %= BASE_FRAMERATE;
    min = sec / 60; sec %= 60;

    return Q_snprintf(buffer, size, "%d:%02d.%d", min, sec, framenum);
}

static size_t CL_Fps_m(char *buffer, size_t size)
{
    return Q_snprintf(buffer, size, "%i", C_FPS);
}

static size_t R_Fps_m(char *buffer, size_t size)
{
    return Q_snprintf(buffer, size, "%i", R_FPS);
}

static size_t CL_Mps_m(char *buffer, size_t size)
{
    return Q_snprintf(buffer, size, "%i", C_MPS);
}

static size_t CL_Pps_m(char *buffer, size_t size)
{
    return Q_snprintf(buffer, size, "%i", C_PPS);
}

static size_t CL_Ping_m(char *buffer, size_t size)
{
    return Q_snprintf(buffer, size, "%i", cls.measure.ping);
}

static size_t CL_Lag_m(char *buffer, size_t size)
{
    float f = 0.0f;

    if (cls.netchan.total_received)
        f = (float)cls.netchan.total_dropped / cls.netchan.total_received;

    return Q_snprintf(buffer, size, "%.2f%%", f * 100.0f);
}

static size_t CL_Health_m(char *buffer, size_t size)
{
    return Q_snprintf(buffer, size, "%i", cl.frame.ps.stats[STAT_HEALTH]);
}

static size_t CL_Ammo_m(char *buffer, size_t size)
{
    return Q_snprintf(buffer, size, "%i", cl.frame.ps.stats[STAT_AMMO]);
}

static size_t CL_Armor_m(char *buffer, size_t size)
{
    return Q_snprintf(buffer, size, "%i", cl.frame.ps.stats[STAT_ARMOR]);
}

static size_t CL_WeaponModel_m(char *buffer, size_t size)
{
    int i = CS_MODELS + (cl.frame.ps.gunindex & GUNINDEX_MASK);
    return Q_strlcpy(buffer, cl.configstrings[i], size);
}

static size_t CL_NumEntities_m(char *buffer, size_t size)
{
    return Q_snprintf(buffer, size, "%i", cl.frame.numEntities);
}

static size_t CL_Surface_m(char *buffer, size_t size)
{
#if 0
    trace_t trace;
    vec3_t end;

    if (cls.state == ca_active) {
        VectorMA(cl.refdef.vieworg, 8192, cl.v_forward, end);
        CL_Trace(&trace, cl.refdef.vieworg, vec3_origin, vec3_origin, end, ENTITYNUM_NONE, MASK_SOLID | MASK_WATER);
        if (trace.surface_id)
            return Q_snprintf(buffer, size, "%s %#x", cl.bsp->texinfo[trace.surface_id - 1].name, trace.surface_flags);
    }
#endif
    return Q_strlcpy(buffer, "", size);
}

/*
===============
CL_WriteConfig

Writes key bindings and archived cvars to config.cfg
===============
*/
static void CL_WriteConfig(void)
{
    qhandle_t f;
    int ret;

    ret = FS_OpenFile(COM_CONFIG_CFG, &f, FS_MODE_WRITE | FS_FLAG_TEXT);
    if (!f) {
        Com_EPrintf("Couldn't open %s for writing: %s\n",
                    COM_CONFIG_CFG, Q_ErrorString(ret));
        return;
    }

    FS_FPrintf(f, "// generated by " APPLICATION ", do not modify\n");

    Key_WriteBindings(f);
    Cvar_WriteVariables(f, CVAR_ARCHIVE, false);

    if (FS_CloseFile(f))
        Com_EPrintf("Error writing %s\n", COM_CONFIG_CFG);
}

/*
====================
CL_RestartFilesystem

Flush caches and restart the VFS.
====================
*/
void CL_RestartFilesystem(bool total)
{
    int cls_state;

    if (!cl_running->integer) {
        FS_Restart(total);
        return;
    }

    Com_DPrintf("%s(%d)\n", __func__, total);

    // temporary switch to loading state
    cls_state = cls.state;
    if (cls.state >= ca_precached) {
        cls.state = ca_loading;
    }

    Con_Popup(false);

    CL_ShutdownCGame();
    UI_Shutdown();

    S_StopAllSounds();
    S_FreeAllSounds();

    // write current config before changing game directory
    CL_WriteConfig();

    if (cls.ref_initialized) {
        R_Shutdown(false);

        FS_Restart(total);

        R_Init(false);

        Con_RegisterMedia();
        UI_Init();
    } else {
        FS_Restart(total);
    }

    OGG_LoadTrackList();

    if (cls_state == ca_disconnected) {
        UI_OpenMenu(UIMENU_DEFAULT);
    } else if (cls_state >= ca_loading && cls_state <= ca_active) {
        CL_InitCGame();
    } else if (cls_state == ca_cinematic) {
        SCR_ReloadCinematic();
    }

    // switch back to original state
    cls.state = cls_state;

    Con_Close(false);

    CL_UpdateFrameTimes();

    cvar_modified &= ~CVAR_FILES;
}

void CL_RestartRefresh(bool total)
{
    int cls_state;

    if (!cls.ref_initialized) {
        return;
    }

    // temporary switch to loading state
    cls_state = cls.state;
    if (cls.state >= ca_precached) {
        cls.state = ca_loading;
    }

    Con_Popup(false);

    CL_ShutdownCGame();

    S_StopAllSounds();

    if (total) {
        IN_Shutdown();
        CL_ShutdownRefresh();
        CL_InitRefresh();
        IN_Init();
    } else {
        UI_Shutdown();
        R_Shutdown(false);
        R_Init(false);
        Con_RegisterMedia();
        UI_Init();
    }

    if (cls_state == ca_disconnected) {
        UI_OpenMenu(UIMENU_DEFAULT);
    } else if (cls_state >= ca_loading && cls_state <= ca_active) {
        CL_InitCGame();
    } else if (cls_state == ca_cinematic) {
        SCR_ReloadCinematic();
    }

    // switch back to original state
    cls.state = cls_state;

    Con_Close(false);

    CL_UpdateFrameTimes();

    cvar_modified &= ~CVAR_FILES;
}

/*
====================
CL_ReloadRefresh

Flush caches and reload all models and textures.
====================
*/
static void CL_ReloadRefresh_f(void)
{
    CL_RestartRefresh(false);
}

/*
====================
CL_RestartRefresh

Perform complete restart of the renderer subsystem.
====================
*/
static void CL_RestartRefresh_f(void)
{
    static bool warned;

    if (!cl_allow_vid_restart->integer && strcmp(Cmd_Argv(1), "force")) {
        if (Cmd_From() == FROM_STUFFTEXT)
            return;

        Com_Printf("Manual `vid_restart' command ignored.\n");
        if (warned)
            return;

        Com_Printf("Video settings are automatically applied by " PRODUCT ", thus manual "
                   "`vid_restart' is never needed. To force restart of video subsystem, "
                   "use `vid_restart force', or set `cl_allow_vid_restart' variable to 1 "
                   "to restore old behavior of this command.\n");
        warned = true;
        return;
    }
    CL_RestartRefresh(true);
}

static void cl_sync_changed(cvar_t *self)
{
    CL_UpdateFrameTimes();
}

// allow downloads to be permanently disabled as a
// protection measure from malicious (or just stupid) servers
// that force downloads by stuffing commands
static void cl_allow_download_changed(cvar_t *self)
{
    if (self->integer == -1) {
        self->flags |= CVAR_ROM;
    }
}

void cl_timeout_changed(cvar_t *self)
{
    self->integer = 1000 * Cvar_ClampValue(self, 0, 24 * 24 * 60 * 60);
}

static const cmdreg_t c_client[] = {
    { "cmd", CL_ForwardToServer_f },
    { "pause", CL_Pause_f },
    { "pingservers", CL_PingServers_f },
    { "userinfo", CL_Userinfo_f },
    { "snd_restart", CL_RestartSound_f },
    { "play", CL_PlaySound_f, CL_PlaySound_c },
    { "disconnect", CL_Disconnect_f },
    { "connect", CL_Connect_f, CL_Connect_c },
    { "followip", CL_FollowIP_f },
    { "passive", CL_PassiveConnect_f },
    { "reconnect", CL_Reconnect_f },
    { "rcon", CL_Rcon_f, CL_Rcon_c },
    { "serverstatus", CL_ServerStatus_f, CL_ServerStatus_c },
    { "writeconfig", CL_WriteConfig_f, CL_WriteConfig_c },
    { "vid_restart", CL_RestartRefresh_f },
    { "r_reload", CL_ReloadRefresh_f },

    //
    // forward to server commands
    //
    // the only thing this does is allow command completion
    // to work -- all unknown commands are automatically
    // forwarded to the server
    { "say" },
    { "say_team" },

    { "wave" }, { "inven" }, { "kill" }, { "use" },
    { "drop" }, { "info" }, { "prog" },
    { "give" }, { "god" }, { "notarget" }, { "noclip" },
    { "invuse" }, { "invprev" }, { "invnext" }, { "invdrop" },
    { "weapnext" }, { "weapprev" },

    { NULL }
};

/*
=================
CL_InitLocal
=================
*/
static void CL_InitLocal(void)
{
    cvar_t *var;
    int i;

    cls.state = ca_disconnected;
    cls.connect_time -= CONNECT_INSTANT;

    CL_RegisterInput();
    CL_InitDemos();
    //CL_InitDownloads();

    Cmd_Register(c_client);

    for (i = 0; i < MAX_LOCAL_SERVERS; i++) {
        var = Cvar_Get(va("adr%i", i), "", CVAR_ARCHIVE);
        var->generator = Com_Address_g;
    }

    //
    // register our variables
    //
    cl_warn_on_fps_rounding = Cvar_Get("cl_warn_on_fps_rounding", "1", 0);
    cl_maxfps = Cvar_Get("cl_maxfps", "62", 0);
    cl_maxfps->changed = cl_sync_changed;
    cl_async = Cvar_Get("cl_async", "1", 0);
    cl_async->changed = cl_sync_changed;
    r_maxfps = Cvar_Get("r_maxfps", "0", 0);
    r_maxfps->changed = cl_sync_changed;
    cl_autopause = Cvar_Get("cl_autopause", "1", 0);

    // hack for timedemo
    com_timedemo->changed = cl_sync_changed;

    CL_UpdateFrameTimes();

#if USE_DEBUG
    cl_shownet = Cvar_Get("cl_shownet", "0", 0);
    cl_showmiss = Cvar_Get("cl_showmiss", "0", 0);
    cl_showclamp = Cvar_Get("showclamp", "0", 0);
#endif

    cl_timeout = Cvar_Get("cl_timeout", "120", 0);
    cl_timeout->changed = cl_timeout_changed;
    cl_timeout_changed(cl_timeout);

    rcon_address = Cvar_Get("rcon_address", "", CVAR_PRIVATE);
    rcon_address->generator = Com_Address_g;

#if USE_FPS
    cl_updaterate = Cvar_Get("cl_updaterate", "0", 0);
    cl_updaterate->changed = cl_updaterate_changed;
#endif

    cl_disconnectcmd = Cvar_Get("cl_disconnectcmd", "", 0);
    cl_changemapcmd = Cvar_Get("cl_changemapcmd", "", 0);
    cl_beginmapcmd = Cvar_Get("cl_beginmapcmd", "", 0);

    cl_allow_vid_restart = Cvar_Get("cl_allow_vid_restart", "0", 0);

    cl_protocol = Cvar_Get("cl_protocol", "0", 0);

    gender_auto = Cvar_Get("gender_auto", "1", CVAR_ARCHIVE);

    allow_download->changed = cl_allow_download_changed;
    cl_allow_download_changed(allow_download);

    //
    // userinfo
    //
    info_password = Cvar_Get("password", "", CVAR_USERINFO);
    info_spectator = Cvar_Get("spectator", "0", CVAR_USERINFO);
    info_name = Cvar_Get("name", "unnamed", CVAR_USERINFO | CVAR_ARCHIVE);
    info_skin = Cvar_Get("skin", "male/grunt", CVAR_USERINFO | CVAR_ARCHIVE);
    info_rate = Cvar_Get("rate", "15000", CVAR_USERINFO | CVAR_ARCHIVE);
    info_msg = Cvar_Get("msg", "1", CVAR_USERINFO | CVAR_ARCHIVE);
    info_hand = Cvar_Get("hand", "0", CVAR_USERINFO | CVAR_ARCHIVE);
    //info_hand->changed = info_hand_changed;
    info_fov = Cvar_Get("fov", "90", CVAR_USERINFO | CVAR_ARCHIVE);
    info_gender = Cvar_Get("gender", "male", CVAR_USERINFO | CVAR_ARCHIVE);
    info_gender->modified = false; // clear this so we know when user sets it manually
    info_uf = Cvar_Get("uf", "", CVAR_USERINFO);


    //
    // macros
    //
    Cmd_AddMacro("cl_mapname", CL_Mapname_m);
    Cmd_AddMacro("cl_server", CL_Server_m);
    Cmd_AddMacro("cl_timer", CL_Timer_m);
    Cmd_AddMacro("cl_demopos", CL_DemoPos_m);
    Cmd_AddMacro("cl_ups", CL_Ups_m);
    Cmd_AddMacro("cl_fps", CL_Fps_m);
    Cmd_AddMacro("r_fps", R_Fps_m);
    Cmd_AddMacro("cl_mps", CL_Mps_m);   // moves per second
    Cmd_AddMacro("cl_pps", CL_Pps_m);   // packets per second
    Cmd_AddMacro("cl_ping", CL_Ping_m);
    Cmd_AddMacro("cl_lag", CL_Lag_m);
    Cmd_AddMacro("cl_health", CL_Health_m);
    Cmd_AddMacro("cl_ammo", CL_Ammo_m);
    Cmd_AddMacro("cl_armor", CL_Armor_m);
    Cmd_AddMacro("cl_weaponmodel", CL_WeaponModel_m);
    Cmd_AddMacro("cl_numentities", CL_NumEntities_m);
    Cmd_AddMacro("cl_surface", CL_Surface_m);
}

/*
=================
CL_PreInit

Called before executing configs to register commands such as `bind' or
`ignoretext'.
=================
*/
void CL_PreInit(void)
{
    Key_Init();
}

/*
==================
CL_CheatsOK
==================
*/
bool CL_CheatsOK(void)
{
    // can cheat when disconnected or playing a demo
    if (cls.state < ca_connected || cls.demo.playback)
        return true;

    // can't cheat on remote servers
    if (!sv_running->integer)
        return false;

    // developer option
    if (Cvar_VariableInteger("cheats"))
        return true;

    // single player can cheat
    if (cls.state > ca_connected && cl.maxclients == 1)
        return true;

    return false;
}

//============================================================================

/*
==================
CL_Activate
==================
*/
void CL_Activate(active_t active)
{
    if (cls.active != active) {
        Com_DDPrintf("%s: %u\n", __func__, active);
        cls.active = active;
        cls.disable_screen = 0;
        Key_ClearStates();
        IN_Activate();
        S_Activate();
        CL_UpdateFrameTimes();
    }
}

static void CL_SetClientTime(void)
{
    int prevtime;

    if (cls.state != ca_active || sv_paused->integer)
        return;

    if (com_timedemo->integer) {
        cl.time = cl.servertime;
        cl.lerpfrac = 1.0f;
        return;
    }

    prevtime = cl.servertime - BASE_FRAMETIME;
    if (cl.time > cl.servertime) {
        SHOWCLAMP(2, "high clamp %i\n", cl.time - cl.servertime);
        cl.time = cl.servertime;
        cl.lerpfrac = 1.0f;
    } else if (cl.time < prevtime) {
        SHOWCLAMP(2, "low clamp %i\n", prevtime - cl.time);
        cl.time = prevtime;
        cl.lerpfrac = 0;
    } else {
        cl.lerpfrac = (cl.time - prevtime) * BASE_1_FRAMETIME;
    }

    SHOWCLAMP(3, "time %d %d, lerpfrac %.3f\n",
              cl.time, cl.servertime, cl.lerpfrac);
}

static void CL_MeasureStats(void)
{
    int i;

    if (com_localTime - cls.measure.time < 1000) {
        return;
    }

    // measure average ping
    if (cls.netchan.init) {
        int ack = cls.netchan.incoming_acknowledged;
        int ping = 0;
        int j, k = 0;

        i = ack - 16 + 1;
        if (i < cl.initialSeq) {
            i = cl.initialSeq;
        }
        for (j = i; j <= ack; j++) {
            client_history_t *h = &cl.history[j & CMD_MASK];
            if (h->rcvd > h->sent) {
                ping += h->rcvd - h->sent;
                k++;
            }
        }

        cls.measure.ping = k ? ping / k : 0;
    }

    // measure main/refresh frame counts
    for (i = 0; i < 4; i++) {
        cls.measure.fps[i] = cls.measure.frames[i];
        cls.measure.frames[i] = 0;
    }

    cls.measure.time = com_localTime;
}

static void CL_CheckTimeout(void)
{
    if (!cls.netchan.init) {
        return;
    }
    if (NET_IsLocalAddress(&cls.netchan.remote_address)) {
        return;
    }

#if USE_ICMP
    if (cls.errorReceived && com_localTime - cls.netchan.last_received > 5000) {
        Com_Error(ERR_DISCONNECT, "Server connection was reset.");
    }
#endif

    if (cl_timeout->integer && com_localTime - cls.netchan.last_received > cl_timeout->integer) {
        // timeoutcount saves debugger
        if (++cl.timeoutcount > 5) {
            Com_Error(ERR_DISCONNECT, "Server connection timed out.");
        }
    } else {
        cl.timeoutcount = 0;
    }
}

/*
=================
CL_CheckForPause

=================
*/
void CL_CheckForPause(void)
{
    if (cls.state != ca_active) {
        // only pause when active
        Cvar_Set("cl_paused", "0");
        Cvar_Set("sv_paused", "0");
        return;
    }

    if (cls.key_dest & (KEY_CONSOLE | KEY_MENU)) {
        // only pause in single player
        if (cl_paused->integer == 0 && cl_autopause->integer) {
            Cvar_Set("cl_paused", "1");
        }
    } else if (cl_paused->integer == 1) {
        // only resume after automatic pause
        Cvar_Set("cl_paused", "0");
    }

    // hack for demo playback pause/unpause
    if (cls.demo.playback) {
        // don't pause when running timedemo!
        if (cl_paused->integer && !com_timedemo->integer) {
            if (!sv_paused->integer) {
                Cvar_Set("sv_paused", "1");
                IN_Activate();
            }
        } else {
            if (sv_paused->integer) {
                Cvar_Set("sv_paused", "0");
                IN_Activate();
            }
        }
    }
}

typedef enum {
    SYNC_TIMEDEMO,
    SYNC_MAXFPS,
    SYNC_SLEEP_10,
    SYNC_SLEEP_60,
    ASYNC_VIDEO,
    ASYNC_FULL
} sync_mode_t;

#if USE_DEBUG
static const char *const sync_names[] = {
    "SYNC_TIMEDEMO",
    "SYNC_MAXFPS",
    "SYNC_SLEEP_10",
    "SYNC_SLEEP_60",
    "ASYNC_VIDEO",
    "ASYNC_FULL"
};
#endif

static int ref_msec, phys_msec, main_msec;
static int ref_extra, phys_extra, main_extra;
static sync_mode_t sync_mode;

#define MIN_PHYS_HZ 10
#define MAX_PHYS_HZ 125
#define MIN_REF_HZ MIN_PHYS_HZ
#define MAX_REF_HZ 1000

static inline int fps_to_msec(int fps)
{
    return 1000 / fps;
}

static void warn_on_fps_rounding(const cvar_t *cvar, int msec)
{
    static bool warned = false;
    int real_maxfps;

    if (cl_warn_on_fps_rounding->integer <= 0)
        return;

    if (!msec)
        return;

    real_maxfps = 1000 / msec;
    if (cvar->integer == real_maxfps)
        return;

    Com_WPrintf("%s value `%d' is inexact and will be rounded to `%d'.\n",
                cvar->name, cvar->integer, real_maxfps);
    if (!warned) {
        Com_Printf("(Set `%s' to `0' to disable this warning.)\n",
                   cl_warn_on_fps_rounding->name);
        warned = true;
    }
}

static int fps_to_clamped_msec(cvar_t *cvar, int min, int max)
{
    int msec;

    if (cvar->integer == 0)
        return fps_to_msec(max);

    msec = fps_to_msec(Cvar_ClampInteger(cvar, min, max));

    if (cvar->modified) {
        warn_on_fps_rounding(cvar, msec);
        cvar->modified = false;
    }

    return msec;
}

/*
==================
CL_UpdateFrameTimes

Called whenever async/fps cvars change, but not every frame
==================
*/
void CL_UpdateFrameTimes(void)
{
    if (!cls.state) {
        return; // not yet fully initialized
    }

    phys_msec = ref_msec = main_msec = 0;
    ref_extra = phys_extra = main_extra = 0;
    cls.frametime = 0.0f;

    if (com_timedemo->integer) {
        // timedemo just runs at full speed
        sync_mode = SYNC_TIMEDEMO;
    } else if (cls.active == ACT_MINIMIZED) {
        // run at 10 fps if minimized
        main_msec = fps_to_msec(10);
        sync_mode = SYNC_SLEEP_10;
    } else if (cls.active == ACT_RESTORED || cls.state != ca_active) {
        // run at 60 fps if not active
        main_msec = fps_to_msec(60);
        sync_mode = SYNC_SLEEP_60;
    } else if (cl_async->integer > 0) {
        // run physics and refresh separately
        phys_msec = fps_to_clamped_msec(cl_maxfps, MIN_PHYS_HZ, MAX_PHYS_HZ);
        if (cl_async->integer > 1 && r_config.flags & QVF_VIDEOSYNC) {
            sync_mode = ASYNC_VIDEO;
        } else {
            ref_msec = fps_to_clamped_msec(r_maxfps, MIN_REF_HZ, MAX_REF_HZ);
            sync_mode = ASYNC_FULL;
        }
    } else {
        // everything ticks in sync with refresh
        main_msec = fps_to_clamped_msec(cl_maxfps, MIN_PHYS_HZ, MAX_PHYS_HZ);
        sync_mode = SYNC_MAXFPS;
    }

    Com_DDPrintf("%s: mode=%s main_msec=%d ref_msec=%d, phys_msec=%d\n",
                 __func__, sync_names[sync_mode], main_msec, ref_msec, phys_msec);
}

/*
==================
CL_Frame

==================
*/
unsigned CL_Frame(unsigned msec)
{
    bool phys_frame = true, ref_frame = true;

    time_after_ref = time_before_ref = 0;

    if (!cl_running->integer) {
        return UINT_MAX;
    }

    main_extra += msec;
    cls.realtime += msec;

    CL_ProcessEvents();

    switch (sync_mode) {
    case SYNC_TIMEDEMO:
        // timedemo just runs at full speed
        break;
    case SYNC_SLEEP_10:
        // don't run refresh at all
        ref_frame = false;
        // fall through
    case SYNC_SLEEP_60:
        // run at limited fps if not active
        if (main_extra < main_msec) {
            return main_msec - main_extra;
        }
        break;
    case ASYNC_VIDEO:
    case ASYNC_FULL:
        // run physics and refresh separately
        phys_extra += main_extra;

        if (phys_extra < phys_msec) {
            phys_frame = false;
        } else if (phys_extra > phys_msec * 4) {
            phys_extra = phys_msec;
        }

        if (sync_mode == ASYNC_VIDEO) {
            ref_frame = R_VideoSync();
        } else {
            ref_extra += main_extra;
            if (ref_extra < ref_msec) {
                ref_frame = false;
            } else if (ref_extra > ref_msec * 4) {
                ref_extra = ref_msec;
            }
        }
        break;
    case SYNC_MAXFPS:
        // everything ticks in sync with refresh
        if (main_extra < main_msec) {
            if (!cl.sendPacketNow) {
                return 0;
            }
            ref_frame = false;
        }
        break;
    }

    Com_DDDDPrintf("main_extra=%d ref_frame=%d ref_extra=%d "
                   "phys_frame=%d phys_extra=%d\n",
                   main_extra, ref_frame, ref_extra,
                   phys_frame, phys_extra);

    // decide the simulation time
    cls.frametime += main_extra * 0.001f;

    if (cls.frametime > 1.0f / 5)
        cls.frametime = 1.0f / 5;

    if (!sv_paused->integer) {
        cl.time += main_extra;
#if USE_FPS
        cl.keytime += main_extra;
#endif
    }

    // read next demo frame
    CL_DemoFrame();

    // calculate local time
    CL_SetClientTime();

    // resend a connection request if necessary
    CL_CheckForResend();

    // read user intentions
    CL_UpdateCmd(main_extra);

    // finalize pending cmd
    phys_frame |= cl.sendPacketNow;
    if (phys_frame) {
        CL_FinalizeCmd();
        phys_extra -= phys_msec;
        M_FRAMES++;

        // don't let the time go too far off
        // this can happen due to cl.sendPacketNow
        if (phys_extra < -phys_msec * 4) {
            phys_extra = 0;
        }
    }

    // send pending cmds
    CL_SendCmd();

    SCR_RunCinematic();

    UI_Frame(main_extra);

    if (ref_frame) {
        Con_RunConsole();

        // update the screen
        if (host_speeds->integer)
            time_before_ref = Sys_Milliseconds();

        SCR_UpdateScreen();

        if (host_speeds->integer)
            time_after_ref = Sys_Milliseconds();

        cls.frametime = 0.0f;

        ref_extra -= ref_msec;
        R_FRAMES++;

        // update audio after the 3D view was drawn
        S_Update();
    }

    // check connection timeout
    CL_CheckTimeout();

    C_FRAMES++;

    CL_MeasureStats();

    main_extra = 0;
    return 0;
}

/*
============
CL_ProcessEvents
============
*/
bool CL_ProcessEvents(void)
{
    if (!cl_running->integer) {
        return false;
    }

    CL_RunRefresh();

    IN_Frame();

    NET_GetPackets(NS_CLIENT, CL_PacketEvent);

    // process console and stuffed commands
    Cbuf_Execute(&cmd_buffer);

    HTTP_RunDownloads();

    return cl.sendPacketNow;
}

//============================================================================

/*
====================
CL_Init
====================
*/
void CL_Init(void)
{
    if (dedicated->integer) {
        return; // nothing running on the client
    }

    if (cl_running->integer) {
        return;
    }

    // all archived variables will now be loaded

    // start with full screen console
    cls.key_dest = KEY_CONSOLE;

    CL_InitRefresh();

    OGG_Init();
    S_Init();   // sound must be initialized after window is created

    CL_InitLocal();
    IN_Init();

#if USE_ZLIB
    Q_assert(inflateInit2(&cls.z, -MAX_WBITS) == Z_OK);
#endif

    SCR_InitCinematics();

    HTTP_Init();

    UI_OpenMenu(UIMENU_DEFAULT);

    Con_PostInit();
    Con_RunConsole();

    Cvar_Set("cl_running", "1");
}

/*
===============
CL_Shutdown

FIXME: this is a callback from Com_Quit and Com_Error.  It would be better
to run quit through here before the final handoff to the sys code.
===============
*/
void CL_Shutdown(void)
{
    static bool isdown = false;

    if (isdown) {
        Com_Printf("CL_Shutdown: recursive shutdown\n");
        return;
    }
    isdown = true;

    if (!cl_running || !cl_running->integer) {
        return;
    }

    CL_Disconnect(ERR_FATAL);

#if USE_ZLIB
    inflateEnd(&cls.z);
#endif

    HTTP_Shutdown();
    OGG_Shutdown();
    S_Shutdown();
    IN_Shutdown();
    Con_Shutdown();
    CL_ShutdownRefresh();
    CL_WriteConfig();

    memset(&cls, 0, sizeof(cls));

    Cvar_Set("cl_running", "0");

    isdown = false;
}
