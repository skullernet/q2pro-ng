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
// cl_parse.c  -- parse a message received from the server

#include "client.h"

/*
=====================================================================

  DELTA FRAME PARSING

=====================================================================
*/

static void CL_ParseDeltaEntity(server_frame_t *frame, int newnum,
                                const entity_state_t *old, bool changed)
{
    entity_state_t  *state;

    if (frame->num_entities >= MAX_PACKET_ENTITIES) {
        Com_Error(ERR_DROP, "%s: too many entities", __func__);
    }

    state = &cl.entities[cl.next_entity & PARSE_ENTITIES_MASK];
    cl.next_entity++;
    frame->num_entities++;

    *state = *old;
    state->number = newnum;

    if (changed)
        MSG_ParseDeltaEntity(old, state);
}

static void CL_ParsePacketEntities(const server_frame_t *oldframe, server_frame_t *frame)
{
    bool                    removed, changed;
    const entity_state_t    *oldstate;
    int                     i, oldindex, oldnum, newnum;

    frame->first_entity = cl.next_entity;
    frame->num_entities = 0;

    // delta from the entities present in oldframe
    oldindex = 0;
    oldstate = NULL;
    if (!oldframe) {
        oldnum = MAX_EDICTS;
    } else {
        if (oldindex >= oldframe->num_entities) {
            oldnum = MAX_EDICTS;
        } else {
            i = (oldframe->first_entity + oldindex) & PARSE_ENTITIES_MASK;
            oldstate = &cl.entities[i];
            oldnum = oldstate->number;
        }
    }

    while (1) {
#if USE_DEBUG
        uint32_t readcount = msg_read.readcount;
#endif
        newnum = MSG_ReadBits(ENTITYNUM_BITS);
        if (newnum == ENTITYNUM_NONE) {
            break;
        }
        if (newnum < 0 || newnum >= ENTITYNUM_WORLD) {
            Com_Error(ERR_DROP, "%s: bad number: %d", __func__, newnum);
        }

        removed = MSG_ReadBit();
        changed = !removed && MSG_ReadBit();

        while (oldnum < newnum) {
            // one or more entities from the old packet are unchanged
            SHOWNET(4, "   unchanged:%i\n", oldnum);
            CL_ParseDeltaEntity(frame, oldnum, oldstate, false);

            oldindex++;

            if (oldindex >= oldframe->num_entities) {
                oldnum = MAX_EDICTS;
            } else {
                i = (oldframe->first_entity + oldindex) & PARSE_ENTITIES_MASK;
                oldstate = &cl.entities[i];
                oldnum = oldstate->number;
            }
        }

        if (removed) {
            // the entity present in oldframe is not in the current frame
            SHOWNET(3, "%3u:remove:%i\n", readcount, newnum);
            if (oldnum != newnum) {
                Com_DPrintf("U_REMOVE: oldnum != newnum\n");
            }
            if (!oldframe) {
                Com_Error(ERR_DROP, "%s: U_REMOVE with NULL oldframe", __func__);
            }

            oldindex++;

            if (oldindex >= oldframe->num_entities) {
                oldnum = MAX_EDICTS;
            } else {
                i = (oldframe->first_entity + oldindex) & PARSE_ENTITIES_MASK;
                oldstate = &cl.entities[i];
                oldnum = oldstate->number;
            }
            continue;
        }

        if (oldnum == newnum) {
            // delta from previous state
            SHOWNET(3, "%3u:delta:%i ", readcount, newnum);
            CL_ParseDeltaEntity(frame, newnum, oldstate, changed);
            SHOWNET(3, "\n");

            oldindex++;

            if (oldindex >= oldframe->num_entities) {
                oldnum = MAX_EDICTS;
            } else {
                i = (oldframe->first_entity + oldindex) & PARSE_ENTITIES_MASK;
                oldstate = &cl.entities[i];
                oldnum = oldstate->number;
            }
            continue;
        }

        if (oldnum > newnum) {
            // delta from baseline
            SHOWNET(3, "%3u:baseline:%i ", readcount, newnum);
            CL_ParseDeltaEntity(frame, newnum, &cl.baselines[newnum], changed);
            SHOWNET(3, "\n");
            continue;
        }
    }

    // any remaining entities in the old frame are copied over
    while (oldnum != MAX_EDICTS) {
        // one or more entities from the old packet are unchanged
        SHOWNET(4, "   unchanged:%i\n", oldnum);
        CL_ParseDeltaEntity(frame, oldnum, oldstate, false);

        oldindex++;

        if (oldindex >= oldframe->num_entities) {
            oldnum = MAX_EDICTS;
        } else {
            i = (oldframe->first_entity + oldindex) & PARSE_ENTITIES_MASK;
            oldstate = &cl.entities[i];
            oldnum = oldstate->number;
        }
    }
}

static void CL_SetActiveState(void)
{
    cls.state = ca_active;

    cl.serverdelta = cl.frame.number;
    cl.time = cl.frame.servertime; // set time, needed for demos
    cl.frameflags = 0;
    cl.initialSeq = cls.netchan.outgoing_sequence;

    SCR_EndLoadingPlaque();     // get rid of loading plaque
    Con_Close(false);           // get rid of connection screen

    CL_CheckForPause();

    CL_UpdateFrameTimes();

    IN_Activate();

    // init some demo things
    CL_FirstDemoFrame();
}

static void CL_ParseFrame(void)
{
    int                     currentframe, deltaframe, delta, suppressed, length;
    server_frame_t          frame;
    const server_frame_t    *oldframe;
    const player_state_t    *from;

    memset(&frame, 0, sizeof(frame));

    cl.frameflags = 0;

    currentframe = MSG_ReadBits(FRAMENUM_BITS);
    delta = MSG_ReadBits(DELTAFRAME_BITS);
    frame.servertime = MSG_ReadBits(32);

    if (delta == 31) {
        deltaframe = -1;
    } else {
        deltaframe = currentframe - delta;
    }

    suppressed = MSG_ReadBits(SUPPRESSCOUNT_BITS);
    if (suppressed & FF_CLIENTPRED) {
        // CLIENTDROP is implied, don't draw both
        suppressed &= ~FF_CLIENTDROP;
    }
    if (suppressed & FF_SUPPRESSED) {
        cl.suppress_count = 1;
    }
    cl.frameflags |= suppressed;

    frame.number = currentframe;
    frame.cmdnum = cl.history[cls.netchan.incoming_acknowledged & CMD_MASK].cmdNumber;

    if (cls.netchan.dropped) {
        cl.frameflags |= FF_SERVERDROP;
    }

    // if the frame is delta compressed from data that we no longer have
    // available, we must suck up the rest of the frame, but not use it, then
    // ask for a non-compressed message
    if (deltaframe > 0) {
        oldframe = &cl.frames[deltaframe & UPDATE_MASK];
        from = &oldframe->ps;
        if (deltaframe == currentframe) {
            // old servers may cause this on map change
            Com_DPrintf("%s: delta from current frame\n", __func__);
            cl.frameflags |= FF_BADFRAME;
        } else if (oldframe->number != deltaframe) {
            // the frame that the server did the delta from
            // is too old, so we can't reconstruct it properly.
            Com_DPrintf("%s: delta frame was never received or too old\n", __func__);
            cl.frameflags |= FF_OLDFRAME;
        } else if (!oldframe->valid) {
            // should never happen
            Com_DPrintf("%s: delta from invalid frame\n", __func__);
            cl.frameflags |= FF_BADFRAME;
        } else if (cl.next_entity - oldframe->first_entity >
                   MAX_PARSE_ENTITIES - MAX_PACKET_ENTITIES) {
            Com_DPrintf("%s: delta entities too old\n", __func__);
            cl.frameflags |= FF_OLDENT;
        } else {
            frame.valid = true; // valid delta parse
        }
        if (!frame.valid && cl.frame.valid && cls.demo.playback) {
            Com_DPrintf("%s: recovering broken demo\n", __func__);
            oldframe = &cl.frame;
            from = &oldframe->ps;
            frame.valid = true;
        }
    } else {
        oldframe = NULL;
        from = NULL;
        frame.valid = true; // uncompressed frame
        cl.frameflags |= FF_NODELTA;
    }

    // read areabits
    length = MSG_ReadBits(6);
    memset(frame.areabits, 255, sizeof(frame.areabits));
    if (length) {
        if (length > sizeof(frame.areabits)) {
            Com_Error(ERR_DROP, "%s: invalid areabits length", __func__);
        }
        for (int i = 0; i < length; i++)
            frame.areabits[i] = MSG_ReadBits(8);
        //memcpy(frame.areabits, MSG_ReadData(length), length);
        frame.areabytes = length;
    } else {
        frame.areabytes = 0;
    }

    SHOWNET(3, "%3u:playerinfo\n", msg_read.readcount);

    // parse playerstate
    if (from)
        frame.ps = *from;
    MSG_ParseDeltaPlayerstate(&frame.ps);

    SHOWNET(3, "\n%3u:packetentities\n", msg_read.readcount);

    // parse packetentities
    CL_ParsePacketEntities(oldframe, &frame);

    // save the frame off in the backup array for later delta comparisons
    cl.frames[currentframe & UPDATE_MASK] = frame;

#if USE_DEBUG
    if (cl_shownet->integer >= 3) {
        int seq = cls.netchan.incoming_acknowledged & CMD_MASK;
        int rtt = cls.demo.playback ? 0 : cls.realtime - cl.history[seq].sent;
        Com_LPrintf(PRINT_DEVELOPER, "%3u:frame:%d  delta:%d  rtt:%d  svtime:%d\n",
                    msg_read.readcount, frame.number, deltaframe, rtt, frame.servertime);
    }
#endif

    if (!frame.valid) {
        cl.frame.valid = false;
        return; // do not change anything
    }

    if (!frame.ps.fov) {
        // fail out early to prevent spurious errors later
        Com_Error(ERR_DROP, "%s: bad fov", __func__);
    }

    if (cls.state < ca_precached)
        return;

    cl.oldframe = cl.frame;
    cl.frame = frame;

    cls.demo.frames_read++;

    // getting a valid frame message ends the connection process
    if (cls.state == ca_precached)
        CL_SetActiveState();

    if (cls.demo.recording && !cls.demo.paused && !cls.demo.seeking)
        CL_EmitDemoFrame();
}

/*
=====================================================================

  SERVER CONNECTING MESSAGES

=====================================================================
*/

static void CL_ParseConfigstring(unsigned index)
{
    char    string[MAX_NET_STRING];
    size_t  len;
    char    **dst;

    if (index >= MAX_CONFIGSTRINGS)
        Com_Error(ERR_DROP, "%s: bad index: %d", __func__, index);

    len = MSG_ReadString(string, sizeof(string));
    if (len >= sizeof(string))
        Com_Error(ERR_DROP, "%s: oversize string: %d", __func__, index);

    SHOWNET(3, "    %d \"%s\"\n", index, COM_MakePrintable(string));

    dst = &cl.configstrings[index];

    Z_Free(*dst);
    *dst = NULL;

    if (len) {
        *dst = Z_Malloc(len + 1);
        memcpy(*dst, string, len + 1);
    }

    if (cls.demo.seeking) {
        Q_SetBit(cl.dcs, index);
        return;
    }

    if (cls.demo.recording && cls.demo.paused) {
        Q_SetBit(cl.dcs, index);
    }

    // do something appropriate
    if (cge)
        cge->UpdateConfigstring(index);
}

static void CL_ParseBaseline(unsigned index)
{
    if (index >= ENTITYNUM_WORLD) {
        Com_Error(ERR_DROP, "%s: bad index: %d", __func__, index);
    }

    SHOWNET(3, "    baseline:%i ", index);
    entity_state_t *s = &cl.baselines[index];
    s->number = index;
    MSG_ParseDeltaEntity(s, s);
    SHOWNET(3, "\n");
}

static void CL_ParseServerData(void)
{
    char    levelname[MAX_QPATH];
    int     i, protocol, attractloop q_unused;
    bool    cinematic;

    // wipe the client_state_t struct
    CL_ClearState();

    // parse protocol version number
    protocol = MSG_ReadLong();
    cl.servercount = MSG_ReadLong();
    attractloop = MSG_ReadByte();

    Com_DPrintf("Serverdata packet received "
                "(protocol=%d, servercount=%d, attractloop=%d)\n",
                protocol, cl.servercount, attractloop);

    // check protocol
    if (cls.serverProtocol != protocol) {
        if (!cls.demo.playback) {
            Com_Error(ERR_DROP, "Requested protocol version %d, but server returned %d.",
                      cls.serverProtocol, protocol);
        }
        if (protocol != PROTOCOL_VERSION_MAJOR) {
            Com_Error(ERR_DROP, "Demo uses unsupported protocol version %d.", protocol);
        }
        cls.serverProtocol = protocol;
    }

    // game directory
    if (MSG_ReadString(cl.gamedir, sizeof(cl.gamedir)) >= sizeof(cl.gamedir)) {
        Com_Error(ERR_DROP, "Oversize gamedir string");
    }

    // never allow demos to change gamedir
    // do not change gamedir if connected to local sever either,
    // as it was already done by SV_InitGame, and changing it
    // here will not work since server is now running
    if (!cls.demo.playback && !sv_running->integer) {
        // pretend it has been set by user, so that 'changed' hook
        // gets called and filesystem is restarted
        Cvar_UserSet("game", cl.gamedir);

        // protect it from modifications while we are connected
        fs_game->flags |= CVAR_ROM;
    }

    // parse player entity number
    cl.clientNum = MSG_ReadByte();

    // get the map name
    MSG_ReadString(cl.mapname, sizeof(cl.mapname));

    // get the full level name
    MSG_ReadString(levelname, sizeof(levelname));

    // setup default server state
    cl.serverstate = ss_game;

    i = MSG_ReadShort();
    if (!Q2PRO_SUPPORTED(i)) {
        Com_Error(ERR_DROP,
                  "Q2PRO server reports unsupported protocol version %d.\n"
                  "Current client version is %d.", i, PROTOCOL_VERSION_MINOR);
    }
    Com_DPrintf("Using minor Q2PRO protocol version %d\n", i);
    cls.protocolVersion = i;
    i = MSG_ReadByte();
    Com_DPrintf("Q2PRO server state %d\n", i);
    cl.serverstate = i;
    cinematic = i == ss_pic || i == ss_cinematic;
    cl.mapchecksum = MSG_ReadLong();

    if (cinematic) {
        SCR_PlayCinematic(cl.mapname);
    } else {
        // separate the printfs so the server message can have a color
        Con_Printf(
            "\n\n"
            "\35\36\36\36\36\36\36\36\36\36\36\36"
            "\36\36\36\36\36\36\36\36\36\36\36\36"
            "\36\36\36\36\36\36\36\36\36\36\36\37"
            "\n\n");

        Com_SetColor(COLOR_ALT);
        Com_Printf("%s\n", levelname);
        Com_SetColor(COLOR_NONE);
    }
}

/*
=====================================================================

ACTION MESSAGES

=====================================================================
*/

static void CL_ParseReconnect(void)
{
    if (cls.demo.playback) {
        Com_Error(ERR_DISCONNECT, "Server disconnected");
    }

    Com_Printf("Server disconnected, reconnecting\n");

    // close netchan now to prevent `disconnect'
    // message from being sent to server
    Netchan_Close(&cls.netchan);

    CL_Disconnect(ERR_RECONNECT);

    cls.state = ca_challenging;
    cls.connect_time -= CONNECT_FAST;
    cls.connect_count = 0;

    CL_CheckForResend();
}

static void CL_ParseServerCommand(void)
{
    char text[MAX_STRING_CHARS];
    const char *s;

    MSG_ReadString(text, sizeof(text));
    SHOWNET(3, "    \"%s\"\n", COM_MakePrintable(text));

    Cmd_TokenizeString(text, false);

    // handle private client commands
    s = Cmd_Argv(0);
    if (!strcmp(s, "changing")) {
        CL_Changing_f();
        return;
    }

    if (!strcmp(s, "precache")) {
        CL_Precache_f();
        return;
    }

    if (!strcmp(s, "reconnect")) {
        cls.state = ca_connected;
        Com_Printf("Reconnecting...\n");
        CL_ClientCommand("new");
        return;
    }

    if (cge)
        cge->ServerCommand();
}

static void CL_ParseZPacket(void)
{
#if USE_ZLIB
    sizebuf_t   temp;
    byte        buffer[MAX_MSGLEN];
    uInt        inlen, outlen;
    int         ret;

    if (msg_read.data != msg_read_buffer) {
        Com_Error(ERR_DROP, "%s: recursively entered", __func__);
    }

    inlen = MSG_ReadShort();
    outlen = MSG_ReadShort();
    if (outlen > MAX_MSGLEN) {
        Com_Error(ERR_DROP, "%s: invalid output length", __func__);
    }

    inflateReset(&cls.z);

    cls.z.next_in = MSG_ReadData(inlen);
    cls.z.avail_in = inlen;
    cls.z.next_out = buffer;
    cls.z.avail_out = outlen;
    ret = inflate(&cls.z, Z_FINISH);
    if (ret != Z_STREAM_END) {
        Com_Error(ERR_DROP, "%s: inflate() failed with error %d", __func__, ret);
    }

    temp = msg_read;
    SZ_InitRead(&msg_read, buffer, outlen);

    CL_ParseServerMessage();

    msg_read = temp;
#else
    Com_Error(ERR_DROP, "Compressed server packet received, "
              "but no zlib support linked in.");
#endif
}

/*
=====================
CL_ParseServerMessage
=====================
*/
void CL_ParseServerMessage(void)
{
    int         cmd, index;
    uint32_t    readcount;

#if USE_DEBUG
    if (cl_shownet->integer == 1) {
        Com_LPrintf(PRINT_DEVELOPER, "%u ", msg_read.cursize);
    } else if (cl_shownet->integer >= 2) {
        Com_LPrintf(PRINT_DEVELOPER, "------------------\n");
    }
#endif

    msg_read.allowunderflow = false;

//
// parse the message
//
    while (1) {
        readcount = msg_read.readcount;
        if (readcount == msg_read.cursize) {
            SHOWNET(2, "%3u:END OF MESSAGE\n", readcount);
            break;
        }

        cmd = MSG_ReadByte();
        SHOWNET(2, "%3u:%s\n", msg_read.readcount - 1, MSG_ServerCommandString(cmd));

        // other commands
        switch (cmd) {
        default:
            Com_Error(ERR_DROP, "%s: illegible server message: %d", __func__, cmd);
            break;

        case svc_nop:
            break;

        case svc_disconnect:
            Com_Error(ERR_DISCONNECT, "Server disconnected");
            break;

        case svc_reconnect:
            CL_ParseReconnect();
            return;

        case svc_stringcmd:
            CL_ParseServerCommand();
            break;

        case svc_serverdata:
            CL_ParseServerData();
            continue;

        case svc_configstring:
            index = MSG_ReadShort();
            CL_ParseConfigstring(index);
            break;

        case svc_frame:
            CL_ParseFrame();
            continue;

        case svc_zpacket:
            CL_ParseZPacket();
            continue;

        case svc_configstringstream:
            while (1) {
                index = MSG_ReadShort();
                if (index == MAX_CONFIGSTRINGS) {
                    break;
                }
                CL_ParseConfigstring(index);
            }
            break;

        case svc_baselinestream:
            while (1) {
                index = MSG_ReadBits(ENTITYNUM_BITS);
                if (index == ENTITYNUM_NONE) {
                    break;
                }
                CL_ParseBaseline(index);
            }
            continue;
        }

        // if recording demos, copy off protocol invariant stuff
        if (cls.demo.recording && !cls.demo.paused) {
            uint32_t len = msg_read.readcount - readcount;

            // it is very easy to overflow standard 1390 bytes
            // demo frame with modern servers... attempt to preserve
            // reliable messages at least, assuming they come first
            if (cls.demo.buffer.cursize + len < cls.demo.buffer.maxsize) {
                SZ_Write(&cls.demo.buffer, msg_read.data + readcount, len);
            } else {
                cls.demo.others_dropped++;
            }
        }
    }
}

/*
=====================
CL_SeekDemoMessage

A variant of ParseServerMessage that skips over non-important action messages,
used for seeking in demos. Returns true if seeking should be aborted (got serverdata).
=====================
*/
bool CL_SeekDemoMessage(void)
{
    int         cmd, index;
    bool        serverdata = false;

#if USE_DEBUG
    if (cl_shownet->integer == 1) {
        Com_LPrintf(PRINT_DEVELOPER, "%u ", msg_read.cursize);
    } else if (cl_shownet->integer >= 2) {
        Com_LPrintf(PRINT_DEVELOPER, "------------------\n");
    }
#endif

    msg_read.allowunderflow = false;

//
// parse the message
//
    while (1) {
        if (msg_read.readcount == msg_read.cursize) {
            SHOWNET(2, "%3u:END OF MESSAGE\n", msg_read.readcount);
            break;
        }

        cmd = MSG_ReadByte();
        SHOWNET(2, "%3u:%s\n", msg_read.readcount - 1, MSG_ServerCommandString(cmd));

        // other commands
        switch (cmd) {
        default:
            Com_Error(ERR_DROP, "%s: illegible server message: %d", __func__, cmd);
            break;

        case svc_nop:
            break;

        case svc_disconnect:
        case svc_reconnect:
            Com_Error(ERR_DISCONNECT, "Server disconnected");
            break;

        case svc_stringcmd:
            CL_ParseServerCommand();
            break;

        case svc_serverdata:
            CL_ParseServerData();
            serverdata = true;
            break;

        case svc_configstring:
            index = MSG_ReadShort();
            CL_ParseConfigstring(index);
            break;

        case svc_frame:
            CL_ParseFrame();
            continue;
        }
    }

    return serverdata;
}
