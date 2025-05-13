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
#include "shared/m_flash.h"

/*
=====================================================================

  DELTA FRAME PARSING

=====================================================================
*/

static void CL_ParseDeltaEntity(server_frame_t *frame, int newnum,
                                const entity_state_t *old, bool changed)
{
    entity_state_t  *state;

    // suck up to MAX_EDICTS for servers that don't cap at MAX_PACKET_ENTITIES
    if (frame->numEntities >= MAX_EDICTS) {
        Com_Error(ERR_DROP, "%s: too many entities", __func__);
    }

    state = &cl.entityStates[cl.numEntityStates & PARSE_ENTITIES_MASK];
    cl.numEntityStates++;
    frame->numEntities++;

    *state = *old;

    // shuffle previous origin to old
    if (/*!(bits & U_OLDORIGIN) &&*/ !(state->renderfx & RF_BEAM))
        VectorCopy(old->origin, state->old_origin);

    if (changed)
        MSG_ParseDeltaEntity(state, newnum);

}

static void CL_ParsePacketEntities(const server_frame_t *oldframe, server_frame_t *frame)
{
    bool                    removed, changed;
    const entity_state_t    *oldstate;
    int                     i, oldindex, oldnum, newnum;

    frame->firstEntity = cl.numEntityStates;
    frame->numEntities = 0;

    // delta from the entities present in oldframe
    oldindex = 0;
    oldstate = NULL;
    if (!oldframe) {
        oldnum = MAX_EDICTS;
    } else {
        if (oldindex >= oldframe->numEntities) {
            oldnum = MAX_EDICTS;
        } else {
            i = (oldframe->firstEntity + oldindex) & PARSE_ENTITIES_MASK;
            oldstate = &cl.entityStates[i];
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

            if (oldindex >= oldframe->numEntities) {
                oldnum = MAX_EDICTS;
            } else {
                i = (oldframe->firstEntity + oldindex) & PARSE_ENTITIES_MASK;
                oldstate = &cl.entityStates[i];
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

            if (oldindex >= oldframe->numEntities) {
                oldnum = MAX_EDICTS;
            } else {
                i = (oldframe->firstEntity + oldindex) & PARSE_ENTITIES_MASK;
                oldstate = &cl.entityStates[i];
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

            if (oldindex >= oldframe->numEntities) {
                oldnum = MAX_EDICTS;
            } else {
                i = (oldframe->firstEntity + oldindex) & PARSE_ENTITIES_MASK;
                oldstate = &cl.entityStates[i];
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

        if (oldindex >= oldframe->numEntities) {
            oldnum = MAX_EDICTS;
        } else {
            i = (oldframe->firstEntity + oldindex) & PARSE_ENTITIES_MASK;
            oldstate = &cl.entityStates[i];
            oldnum = oldstate->number;
        }
    }
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
    frame.delta = deltaframe;

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
        } else if (cl.numEntityStates - oldframe->firstEntity >
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
        Com_LPrintf(PRINT_DEVELOPER, "%3u:frame:%d  delta:%d  rtt:%d\n",
                    msg_read.readcount, frame.number, frame.delta, rtt);
    }
#endif

    if (!frame.valid) {
        cl.frame.valid = false;
#if USE_FPS
        cl.keyframe.valid = false;
#endif
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

#if USE_FPS
    if (CL_FRAMESYNC) {
        cl.oldkeyframe = cl.keyframe;
        cl.keyframe = cl.frame;
    }
#endif

    cls.demo.frames_read++;

    if (!cls.demo.seeking)
        CL_DeltaFrame();
}

/*
=====================================================================

  SERVER CONNECTING MESSAGES

=====================================================================
*/

static void CL_ParseConfigstring(int index)
{
    size_t  len, maxlen;
    char    *s;

    if (index < 0 || index >= MAX_CONFIGSTRINGS) {
        Com_Error(ERR_DROP, "%s: bad index: %d", __func__, index);
    }

    s = cl.configstrings[index];
    maxlen = Com_ConfigstringSize(index);
    len = MSG_ReadString(s, maxlen);

    SHOWNET(3, "    %d \"%s\"\n", index, COM_MakePrintable(s));

    if (len >= maxlen) {
        Com_WPrintf(
            "%s: index %d overflowed: %zu > %zu\n",
            __func__, index, len, maxlen - 1);
    }

    if (cls.demo.seeking) {
        Q_SetBit(cl.dcs, index);
        return;
    }

    if (cls.demo.recording && cls.demo.paused) {
        Q_SetBit(cl.dcs, index);
    }

    // do something appropriate
    CL_UpdateConfigstring(index);
}

static void CL_ParseBaseline(int index)
{
    if (index < 0 || index >= ENTITYNUM_WORLD) {
        Com_Error(ERR_DROP, "%s: bad index: %d", __func__, index);
    }

#if 0//USE_DEBUG
    if (cl_shownet->integer >= 3) {
        Com_LPrintf(PRINT_DEVELOPER, "   baseline:%i ", index);
        MSG_ShowDeltaEntityBits(bits);
        Com_LPrintf(PRINT_DEVELOPER, "\n");
    }
#endif
    if (MSG_ReadBit()) {
        Com_Error(ERR_DROP, "%s: removed entity", __func__);
        return;
    }

    if (MSG_ReadBit())
        MSG_ParseDeltaEntity(&cl.baselines[index], index);
}

// instead of wasting space for svc_configstring and svc_spawnbaseline
// bytes, entire game state is compressed into a single stream.
static void CL_ParseGamestate(int cmd)
{
    int         index;

    if (cmd == svc_configstringstream) {
        while (1) {
            index = MSG_ReadWord();
            if (index == MAX_CONFIGSTRINGS) {
                break;
            }
            CL_ParseConfigstring(index);
        }
    }

    if (cmd == svc_baselinestream) {
        while (1) {
            index = MSG_ReadBits(ENTITYNUM_BITS);
            if (!index) {
                break;
            }
            CL_ParseBaseline(index);
        }
    }
}

static void CL_ParseServerData(void)
{
    char    levelname[MAX_QPATH];
    int     i, protocol, attractloop q_unused;
    bool    cinematic;

    Cbuf_Execute(&cl_cmdbuf);          // make sure any stuffed commands are done

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
    cl.clientNum = MSG_ReadShort();

    // get the full level name
    MSG_ReadString(levelname, sizeof(levelname));

#if USE_FPS
    // setup default frame times
    cl.frametime = Com_ComputeFrametime(BASE_FRAMERATE);
    cl.frametime_inv = cl.frametime.div * BASE_1_FRAMETIME;
#endif

    // setup default server state
    cl.serverstate = ss_game;
    cinematic = cl.clientNum == -1;

    i = MSG_ReadWord();
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
    i = MSG_ReadWord();

    if (cinematic) {
        SCR_PlayCinematic(levelname);
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

    // make sure clientNum is in range
    if (!VALIDATE_CLIENTNUM(cl.clientNum)) {
        Com_WPrintf("Serverdata has invalid playernum %d\n", cl.clientNum);
        cl.clientNum = -1;
    }

    CL_InitCGame();
}

/*
=====================================================================

ACTION MESSAGES

=====================================================================
*/

tent_params_t   te;
mz_params_t     mz;
snd_params_t    snd;

static void CL_ParseTEntPacket(void)
{
    te.type = MSG_ReadByte();

    switch (te.type) {
    case TE_BLOOD:
    case TE_GUNSHOT:
    case TE_SPARKS:
    case TE_BULLET_SPARKS:
    case TE_SCREEN_SPARKS:
    case TE_SHIELD_SPARKS:
    case TE_SHOTGUN:
    case TE_BLASTER:
    case TE_GREENBLOOD:
    case TE_BLASTER2:
    case TE_FLECHETTE:
    case TE_HEATBEAM_SPARKS:
    case TE_HEATBEAM_STEAM:
    case TE_MOREBLOOD:
    case TE_ELECTRIC_SPARKS:
    case TE_BLUEHYPERBLASTER_2:
    case TE_BERSERK_SLAM:
        MSG_ReadPos(te.pos1);
        MSG_ReadDir(te.dir);
        break;

    case TE_SPLASH:
    case TE_LASER_SPARKS:
    case TE_WELDING_SPARKS:
    case TE_TUNNEL_SPARKS:
        te.count = MSG_ReadByte();
        MSG_ReadPos(te.pos1);
        MSG_ReadDir(te.dir);
        te.color = MSG_ReadByte();
        break;

    case TE_BLUEHYPERBLASTER:
    case TE_RAILTRAIL:
    case TE_RAILTRAIL2:
    case TE_BUBBLETRAIL:
    case TE_DEBUGTRAIL:
    case TE_BUBBLETRAIL2:
    case TE_BFG_LASER:
    case TE_BFG_ZAP:
        MSG_ReadPos(te.pos1);
        MSG_ReadPos(te.pos2);
        break;

    case TE_GRENADE_EXPLOSION:
    case TE_GRENADE_EXPLOSION_WATER:
    case TE_EXPLOSION2:
    case TE_PLASMA_EXPLOSION:
    case TE_ROCKET_EXPLOSION:
    case TE_ROCKET_EXPLOSION_WATER:
    case TE_EXPLOSION1:
    case TE_EXPLOSION1_NP:
    case TE_EXPLOSION1_BIG:
    case TE_BFG_EXPLOSION:
    case TE_BFG_BIGEXPLOSION:
    case TE_BOSSTPORT:
    case TE_PLAIN_EXPLOSION:
    case TE_CHAINFIST_SMOKE:
    case TE_TRACKER_EXPLOSION:
    case TE_TELEPORT_EFFECT:
    case TE_DBALL_GOAL:
    case TE_WIDOWSPLASH:
    case TE_NUKEBLAST:
    case TE_EXPLOSION1_NL:
    case TE_EXPLOSION2_NL:
        MSG_ReadPos(te.pos1);
        break;

    case TE_PARASITE_ATTACK:
    case TE_MEDIC_CABLE_ATTACK:
    case TE_HEATBEAM:
    case TE_MONSTER_HEATBEAM:
    case TE_GRAPPLE_CABLE_2:
    case TE_LIGHTNING_BEAM:
        te.entity1 = MSG_ReadShort();
        MSG_ReadPos(te.pos1);
        MSG_ReadPos(te.pos2);
        break;

    case TE_GRAPPLE_CABLE:
        te.entity1 = MSG_ReadShort();
        MSG_ReadPos(te.pos1);
        MSG_ReadPos(te.pos2);
        MSG_ReadPos(te.offset);
        break;

    case TE_LIGHTNING:
        te.entity1 = MSG_ReadShort();
        te.entity2 = MSG_ReadShort();
        MSG_ReadPos(te.pos1);
        MSG_ReadPos(te.pos2);
        break;

    case TE_FLASHLIGHT:
        MSG_ReadPos(te.pos1);
        te.entity1 = MSG_ReadShort();
        break;

    case TE_FORCEWALL:
        MSG_ReadPos(te.pos1);
        MSG_ReadPos(te.pos2);
        te.color = MSG_ReadByte();
        break;

    case TE_STEAM:
        te.entity1 = MSG_ReadShort();
        te.count = MSG_ReadByte();
        MSG_ReadPos(te.pos1);
        MSG_ReadDir(te.dir);
        te.color = MSG_ReadByte();
        te.entity2 = MSG_ReadShort();
        if (te.entity1 != -1) {
            te.time = MSG_ReadLong();
        }
        break;

    case TE_WIDOWBEAMOUT:
        te.entity1 = MSG_ReadShort();
        MSG_ReadPos(te.pos1);
        break;

    case TE_POWER_SPLASH:
        te.entity1 = MSG_ReadShort();
        te.count = MSG_ReadByte();
        break;

    case TE_DAMAGE_DEALT:
        te.count = MSG_ReadShort();
        break;

    default:
        Com_Error(ERR_DROP, "%s: bad type", __func__);
    }
}

static void CL_ParseMuzzleFlashPacket(int mask)
{
    int entity, weapon;

    entity = MSG_ReadWord();
    weapon = MSG_ReadByte();

    if (!mask) {
        weapon |= entity >> ENTITYNUM_BITS << 8;
        entity &= ENTITYNUM_MASK;
    }

    if (entity < 0 || entity >= ENTITYNUM_WORLD)
        Com_Error(ERR_DROP, "%s: bad entity", __func__);

    if (!mask && weapon >= q_countof(monster_flash_offset))
        Com_Error(ERR_DROP, "%s: bad weapon", __func__);

    mz.silenced = weapon & mask;
    mz.weapon = weapon & ~mask;
    mz.entity = entity;
}

static void CL_ParseStartSoundPacket(void)
{
    int flags, channel, entity;

    snd.flags = flags = MSG_ReadByte();

    if (flags & SND_INDEX16)
        snd.index = MSG_ReadWord();
    else
        snd.index = MSG_ReadByte();

    if (snd.index >= MAX_SOUNDS)
        Com_Error(ERR_DROP, "%s: bad index: %d", __func__, snd.index);

    if (flags & SND_VOLUME)
        snd.volume = MSG_ReadByte() / 255.0f;
    else
        snd.volume = DEFAULT_SOUND_PACKET_VOLUME;

    if (flags & SND_ATTENUATION)
        snd.attenuation = MSG_ReadByte() / 64.0f;
    else
        snd.attenuation = DEFAULT_SOUND_PACKET_ATTENUATION;

    if (flags & SND_OFFSET)
        snd.timeofs = MSG_ReadByte() / 1000.0f;
    else
        snd.timeofs = 0;

    if (flags & SND_ENT) {
        // entity relative
        channel = MSG_ReadWord();
        entity = channel >> 3;
        if (entity < 0 || entity >= ENTITYNUM_WORLD)
            Com_Error(ERR_DROP, "%s: bad entity: %d", __func__, entity);
        snd.entity = entity;
        snd.channel = channel & 7;
    } else {
        snd.entity = 0;
        snd.channel = 0;
    }

    // positioned in space
    if (flags & SND_POS)
        MSG_ReadPos(snd.pos);

    SHOWNET(3, "    %s\n", cl.configstrings[CS_SOUNDS + snd.index]);
}

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

#if USE_AUTOREPLY
static void CL_CheckForVersion(const char *s)
{
    char *p;

    p = strstr(s, ": ");
    if (!p) {
        return;
    }

    if (strncmp(p + 2, "!version", 8)) {
        return;
    }

    if (cl.reply_time && cls.realtime - cl.reply_time < 120000) {
        return;
    }

    cl.reply_time = cls.realtime;
    cl.reply_delta = 1024 + (Q_rand() & 1023);
}
#endif

// attempt to scan out an IP address in dotted-quad notation and
// add it into circular array of recent addresses
static void CL_CheckForIP(const char *s)
{
    unsigned b1, b2, b3, b4, port;
    netadr_t *a;
    int n;

    while (*s) {
        n = sscanf(s, "%3u.%3u.%3u.%3u:%u", &b1, &b2, &b3, &b4, &port);
        if (n >= 4 && (b1 | b2 | b3 | b4) < 256) {
            if (n == 5) {
                if (port < 1024 || port > 65535) {
                    break; // privileged or invalid port
                }
            } else {
                port = PORT_SERVER;
            }

            a = &cls.recent_addr[cls.recent_head++ & RECENT_MASK];
            a->type = NA_IP;
            a->ip.u8[0] = b1;
            a->ip.u8[1] = b2;
            a->ip.u8[2] = b3;
            a->ip.u8[3] = b4;
            a->port = BigShort(port);
            break;
        }

        s++;
    }
}

static void CL_ParsePrint(void)
{
    int level;
    char s[MAX_STRING_CHARS];
    const char *fmt;

    level = MSG_ReadByte();
    MSG_ReadString(s, sizeof(s));

    SHOWNET(3, "    %i \"%s\"\n", level, COM_MakePrintable(s));

    if (level != PRINT_CHAT) {
        if (level == PRINT_TYPEWRITER || level == PRINT_CENTER)
            SCR_CenterPrint(s, level == PRINT_TYPEWRITER);
        else
            Com_Printf("%s", s);
        if (!cls.demo.playback && cl.serverstate != ss_broadcast) {
            COM_strclr(s);
            Cmd_ExecTrigger(s);
        }
        return;
    }

    if (CL_CheckForIgnore(s)) {
        return;
    }

#if USE_AUTOREPLY
    if (!cls.demo.playback && cl.serverstate != ss_broadcast) {
        CL_CheckForVersion(s);
    }
#endif

    CL_CheckForIP(s);

    // disable notify
    if (!cl_chat_notify->integer) {
        Con_SkipNotify(true);
    }

    // filter text
    if (cl_chat_filter->integer) {
        COM_strclr(s);
        fmt = "%s\n";
    } else {
        fmt = "%s";
    }

    Com_LPrintf(PRINT_TALK, fmt, s);

    Con_SkipNotify(false);

    SCR_AddToChatHUD(s);

    // play sound
    if (cl_chat_sound->integer > 1)
        S_StartLocalSoundOnce("misc/talk1.wav");
    else if (cl_chat_sound->integer > 0)
        S_StartLocalSoundOnce("misc/talk.wav");
}

static void CL_ParseCenterPrint(void)
{
    char s[MAX_STRING_CHARS];

    MSG_ReadString(s, sizeof(s));
    SHOWNET(3, "    \"%s\"\n", COM_MakePrintable(s));
    SCR_CenterPrint(s, false);

    if (!cls.demo.playback && cl.serverstate != ss_broadcast) {
        COM_strclr(s);
        Cmd_ExecTrigger(s);
    }
}

static void CL_ParseStuffText(void)
{
    char s[MAX_STRING_CHARS];

    MSG_ReadString(s, sizeof(s));
    SHOWNET(3, "    \"%s\"\n", COM_MakePrintable(s));
    Cbuf_AddText(&cl_cmdbuf, s);
}

static void CL_ParseLayout(void)
{
    MSG_ReadString(cl.layout, sizeof(cl.layout));
    SHOWNET(3, "    \"%s\"\n", COM_MakePrintable(cl.layout));
}

static void CL_ParseInventory(void)
{
    int        i;

    for (i = 0; i < MAX_ITEMS; i++) {
        cl.inventory[i] = MSG_ReadShort();
    }
}

static void CL_ParseDownload(int cmd)
{
    int size, percent;

    if (!cls.download.temp[0]) {
        Com_Error(ERR_DROP, "%s: no download requested", __func__);
    }

    // read the data
    size = MSG_ReadShort();
    percent = MSG_ReadByte();
    if (size == -1) {
        CL_HandleDownload(NULL, size, percent, 0);
        return;
    }

    // read optional decompressed packet size
    if (cmd == svc_zdownload) {
#if !USE_ZLIB
        Com_Error(ERR_DROP, "Compressed server packet received, "
                  "but no zlib support linked in.");
#endif
    }

    if (size < 0) {
        Com_Error(ERR_DROP, "%s: bad size: %d", __func__, size);
    }

    CL_HandleDownload(MSG_ReadData(size), size, percent, cmd);
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

    inlen = MSG_ReadWord();
    outlen = MSG_ReadWord();
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

#if USE_FPS
static void set_server_fps(int value)
{
    cl.frametime = Com_ComputeFrametime(value);
    cl.frametime_inv = cl.frametime.div * BASE_1_FRAMETIME;

    // fix time delta
    if (cls.state == ca_active) {
        int delta = cl.frame.number - cl.servertime / cl.frametime.time;
        cl.serverdelta = Q_align_down(delta, cl.frametime.div);
    }

    Com_DPrintf("client framediv=%d time=%d delta=%d\n",
                cl.frametime.div, cl.servertime, cl.serverdelta);
}
#endif

static void CL_ParseSetting(void)
{
    int index q_unused;
    int value q_unused;

    index = MSG_ReadLong();
    value = MSG_ReadLong();

    switch (index) {
#if USE_FPS
    case SVS_FPS:
        set_server_fps(value);
        break;
#endif
    default:
        break;
    }
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

        case svc_print:
            CL_ParsePrint();
            break;

        case svc_centerprint:
            CL_ParseCenterPrint();
            break;

        case svc_stufftext:
            CL_ParseStuffText();
            break;

        case svc_serverdata:
            CL_ParseServerData();
            continue;

        case svc_configstring:
            index = MSG_ReadWord();
            CL_ParseConfigstring(index);
            break;

        case svc_sound:
            CL_ParseStartSoundPacket();
            S_ParseStartSound();
            break;

        case svc_spawnbaseline:
            index = MSG_ReadBits(ENTITYNUM_BITS);
            CL_ParseBaseline(index);
            break;

        case svc_temp_entity:
            CL_ParseTEntPacket();
            CL_ParseTEnt();
            break;

        case svc_muzzleflash:
            CL_ParseMuzzleFlashPacket(MZ_SILENCED);
            CL_MuzzleFlash();
            break;

        case svc_muzzleflash2:
            CL_ParseMuzzleFlashPacket(0);
            CL_MuzzleFlash2();
            break;

        case svc_download:
            CL_ParseDownload(cmd);
            continue;

        case svc_frame:
            CL_ParseFrame();
            continue;

        case svc_inventory:
            CL_ParseInventory();
            break;

        case svc_layout:
            CL_ParseLayout();
            break;

        case svc_zpacket:
            CL_ParseZPacket();
            continue;

        case svc_zdownload:
            CL_ParseDownload(cmd);
            continue;

        case svc_gamestate:
        case svc_configstringstream:
        case svc_baselinestream:
            CL_ParseGamestate(cmd);
            continue;

        case svc_setting:
            CL_ParseSetting();
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

        case svc_print:
            MSG_ReadByte();
            // fall through

        case svc_centerprint:
        case svc_stufftext:
            MSG_ReadString(NULL, 0);
            break;

        case svc_serverdata:
            CL_ParseServerData();
            serverdata = true;
            break;

        case svc_configstring:
            index = MSG_ReadWord();
            CL_ParseConfigstring(index);
            break;

        case svc_sound:
            CL_ParseStartSoundPacket();
            break;

        case svc_spawnbaseline:
            index = MSG_ReadBits(ENTITYNUM_BITS);
            CL_ParseBaseline(index);
            break;

        case svc_temp_entity:
            CL_ParseTEntPacket();
            break;

        case svc_muzzleflash:
            CL_ParseMuzzleFlashPacket(MZ_SILENCED);
            break;

        case svc_muzzleflash2:
            CL_ParseMuzzleFlashPacket(0);
            break;

        case svc_frame:
            CL_ParseFrame();
            continue;

        case svc_inventory:
            CL_ParseInventory();
            break;

        case svc_layout:
            CL_ParseLayout();
            break;
        }
    }

    return serverdata;
}
