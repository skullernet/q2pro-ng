/*
Copyright (C) 2003-2006 Andrey Nazarov

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
// cl_demo.c - demo recording and playback
//

#include "client.h"

static byte     demo_buffer[MAX_MSGLEN];

static cvar_t   *cl_demosnaps;
static cvar_t   *cl_demowait;
static cvar_t   *cl_demosuspendtoggle;

// =========================================================================

/*
====================
CL_WriteDemoMessage

Dumps the current demo message, prefixed by the length.
Stops demo recording and returns false on write error.
====================
*/
bool CL_WriteDemoMessage(sizebuf_t *buf)
{
    uint32_t msglen;
    int ret;

    if (buf->overflowed) {
        SZ_Clear(buf);
        Com_WPrintf("Demo message overflowed (should never happen).\n");
        return true;
    }

    if (!buf->cursize)
        return true;

    msglen = LittleLong(buf->cursize);
    ret = FS_Write(&msglen, 4, cls.demo.recording);
    if (ret != 4)
        goto fail;
    ret = FS_Write(buf->data, buf->cursize, cls.demo.recording);
    if (ret != buf->cursize)
        goto fail;

    Com_DDPrintf("%s: wrote %u bytes\n", __func__, buf->cursize);

    SZ_Clear(buf);
    return true;

fail:
    SZ_Clear(buf);
    Com_EPrintf("Couldn't write demo: %s\n", Q_ErrorString(ret));
    CL_Stop_f();
    return false;
}

// writes a delta update of an entity_state_t list to the message.
static void emit_packet_entities(const server_frame_t *from, const server_frame_t *to)
{
    entity_state_t *oldent, *newent;
    int     oldindex, newindex;
    int     oldnum, newnum;
    int     i, from_num_entities;

    if (!from)
        from_num_entities = 0;
    else
        from_num_entities = from->num_entities;

    newindex = 0;
    oldindex = 0;
    oldent = newent = NULL;
    while (newindex < to->num_entities || oldindex < from_num_entities) {
        if (newindex >= to->num_entities) {
            newnum = MAX_EDICTS;
        } else {
            i = (to->first_entity + newindex) & PARSE_ENTITIES_MASK;
            newent = &cl.entities[i];
            newnum = newent->number;
        }

        if (oldindex >= from_num_entities) {
            oldnum = MAX_EDICTS;
        } else {
            i = (from->first_entity + oldindex) & PARSE_ENTITIES_MASK;
            oldent = &cl.entities[i];
            oldnum = oldent->number;
        }

        if (newnum == oldnum) {
            // Delta update from old position. Because the force param is false,
            // this will not result in any bytes being emitted if the entity has
            // not changed at all. Note that players are always 'newentities',
            // this updates their old_origin always and prevents warping in case
            // of packet loss.
            MSG_WriteDeltaEntity(oldent, newent, false);
            oldindex++;
            newindex++;
            continue;
        }

        if (newnum < oldnum) {
            // this is a new entity, send it from the baseline
            MSG_WriteDeltaEntity(&cl.baselines[newnum], newent, true);
            newindex++;
            continue;
        }

        if (newnum > oldnum) {
            // the old entity isn't present in the new message
            MSG_WriteDeltaEntity(oldent, NULL, true);
            oldindex++;
            continue;
        }
    }

    MSG_WriteBits(ENTITYNUM_NONE, ENTITYNUM_BITS);  // end of packetentities
}

static void emit_delta_frame(const server_frame_t *from, const server_frame_t *to)
{
    MSG_WriteByte(svc_frame);
    MSG_WriteBits(from ? 1 : 0, FRAMEDELTA_BITS);
    MSG_WriteBits(to->flags, FRAMEFLAGS_BITS);
    MSG_WriteBits(to->servertime, SERVERTIME_BITS);

    // send over the areabits
    MSG_WriteAreaBits(to->areabits, to->areabytes);

    // delta encode the playerstate
    MSG_WriteDeltaPlayerstate(from ? &from->ps : NULL, &to->ps);

    // delta encode the entities
    emit_packet_entities(from, to);
    MSG_FlushBits();
}

/*
====================
CL_EmitDemoFrame

Writes delta from the last frame we got to the current frame.
====================
*/
void CL_EmitDemoFrame(void)
{
    server_frame_t  *oldframe;

    if (!cl.frame.valid)
        return;

    // the first frame is delta uncompressed
    if (cls.demo.last_server_frame == -1) {
        oldframe = NULL;
    } else {
        oldframe = &cl.frames[cls.demo.last_server_frame & UPDATE_MASK];
        if (oldframe->number != cls.demo.last_server_frame || !oldframe->valid ||
            cl.next_entity - oldframe->first_entity > MAX_PARSE_ENTITIES) {
            oldframe = NULL;
        }
    }

    // emit and flush frame
    emit_delta_frame(oldframe, &cl.frame);

    if (msg_write.overflowed) {
        Com_WPrintf("%s: message buffer overflowed\n", __func__);
    } else if (cls.demo.buffer.cursize + msg_write.cursize > cls.demo.buffer.maxsize) {
        Com_DPrintf("Demo frame overflowed (%u + %u > %u)\n",
                    cls.demo.buffer.cursize, msg_write.cursize, cls.demo.buffer.maxsize);
        cls.demo.frames_dropped++;
    } else {
        SZ_Write(&cls.demo.buffer, msg_write.data, msg_write.cursize);
        cls.demo.last_server_frame = cl.frame.number;
        cls.demo.frames_written++;
    }

    SZ_Clear(&msg_write);
}

static size_t format_demo_size(char *buffer, size_t size)
{
    return Com_FormatSizeLong(buffer, size, FS_Tell(cls.demo.recording));
}

static size_t format_demo_status(char *buffer, size_t size)
{
    size_t len = format_demo_size(buffer, size);
    int min, sec, frames = cls.demo.frames_written;

    sec = frames / BASE_FRAMERATE; frames %= BASE_FRAMERATE;
    min = sec / 60; sec %= 60;

    len += Q_scnprintf(buffer + len, size - len, ", %d:%02d.%d",
                       min, sec, frames);

    if (cls.demo.frames_dropped) {
        len += Q_scnprintf(buffer + len, size - len, ", %d frame%s dropped",
                           cls.demo.frames_dropped,
                           cls.demo.frames_dropped == 1 ? "" : "s");
    }

    if (cls.demo.others_dropped) {
        len += Q_scnprintf(buffer + len, size - len, ", %d message%s dropped",
                           cls.demo.others_dropped,
                           cls.demo.others_dropped == 1 ? "" : "s");
    }

    return len;
}

/*
====================
CL_Stop_f

stop recording a demo
====================
*/
void CL_Stop_f(void)
{
    uint32_t msglen;
    char buffer[MAX_QPATH];

    if (!cls.demo.recording) {
        Com_Printf("Not recording a demo.\n");
        return;
    }

// finish up
    msglen = (uint32_t)-1;
    FS_Write(&msglen, 4, cls.demo.recording);

    format_demo_size(buffer, sizeof(buffer));

// close demofile
    FS_CloseFile(cls.demo.recording);
    cls.demo.recording = 0;
    cls.demo.paused = false;
    cls.demo.frames_written = 0;
    cls.demo.frames_dropped = 0;
    cls.demo.others_dropped = 0;

// print some statistics
    Com_Printf("Stopped demo (%s).\n", buffer);
}

static const cmd_option_t o_record[] = {
    { "h", "help", "display this message" },
    { "z", "compress", "compress demo with gzip" },
    { NULL }
};

/*
====================
CL_Record_f

record <demoname>

Begins recording a demo from the current position
====================
*/
static void CL_Record_f(void)
{
    char    buffer[MAX_OSPATH];
    int     i, c;
    size_t  len;
    entity_state_t  *ent;
    char            *s;
    qhandle_t       f;
    unsigned        mode = FS_MODE_WRITE;

    while ((c = Cmd_ParseOptions(o_record)) != -1) {
        switch (c) {
        case 'h':
            Cmd_PrintUsage(o_record, "<filename>");
            Com_Printf("Begin client demo recording.\n");
            Cmd_PrintHelp(o_record);
            return;
        case 'z':
            mode |= FS_FLAG_GZIP;
            break;
        default:
            return;
        }
    }

    if (cls.demo.recording) {
        format_demo_status(buffer, sizeof(buffer));
        Com_Printf("Already recording (%s).\n", buffer);
        return;
    }

    if (!cmd_optarg[0]) {
        Com_Printf("Missing filename argument.\n");
        Cmd_PrintHint();
        return;
    }

    if (cls.state != ca_active) {
        Com_Printf("You must be in a level to record.\n");
        return;
    }

    //
    // open the demo file
    //
    f = FS_EasyOpenFile(buffer, sizeof(buffer), mode,
                        "demos/", cmd_optarg, ".dm2");
    if (!f) {
        return;
    }

    Com_Printf("Recording client demo to %s.\n", buffer);

    cls.demo.recording = f;
    cls.demo.paused = false;

    // the first frame will be delta uncompressed
    cls.demo.last_server_frame = -1;

    SZ_InitWrite(&cls.demo.buffer, demo_buffer, MAX_MSGLEN);

    // clear dirty configstrings
    memset(cl.dcs, 0, sizeof(cl.dcs));

    //
    // write out messages to hold the startup information
    //
    MSG_BeginWriting();

    // send the serverdata
    MSG_WriteByte(svc_serverdata);
    MSG_WriteLong(PROTOCOL_VERSION_MAJOR);
    MSG_WriteLong(cl.servercount);
    MSG_WriteByte(1);      // demos are always attract loops
    MSG_WriteString(cl.gamedir);
    MSG_WriteByte(cl.clientNum);
    MSG_WriteString(cl.mapname);
    MSG_WriteString(cl.configstrings[CS_NAME]);
    MSG_WriteShort(cls.protocolVersion);
    MSG_WriteByte(cl.serverstate);
    MSG_WriteLong(cl.mapchecksum);

    // configstrings
    MSG_WriteByte(svc_configstringstream);
    for (i = 0; i < MAX_CONFIGSTRINGS; i++) {
        s = cl.configstrings[i];
        if (!s)
            continue;

        len = strlen(s);
        if (msg_write.cursize + len + 4 > msg_write.maxsize) {
            MSG_WriteShort(MAX_CONFIGSTRINGS);
            if (!CL_WriteDemoMessage(&msg_write))
                return;
            MSG_BeginWriting();
            MSG_WriteByte(svc_configstringstream);
        }

        MSG_WriteShort(i);
        MSG_WriteData(s, len + 1);
    }
    MSG_WriteShort(MAX_CONFIGSTRINGS);

    // baselines
    MSG_WriteByte(svc_baselinestream);
    for (i = 0; i < ENTITYNUM_WORLD; i++) {
        ent = &cl.baselines[i];
        if (i && !ent->number)
            continue;

        if (msg_write.cursize + msg_max_entity_bytes > msg_write.maxsize) {
            MSG_WriteBits(ENTITYNUM_NONE, ENTITYNUM_BITS);
            MSG_FlushBits();
            if (!CL_WriteDemoMessage(&msg_write))
                return;
            MSG_BeginWriting();
            MSG_WriteByte(svc_baselinestream);
        }

        MSG_WriteDeltaEntity(NULL, ent, false);
    }
    MSG_WriteBits(ENTITYNUM_NONE, ENTITYNUM_BITS);
    MSG_FlushBits();

    MSG_WriteByte(svc_stringcmd);
    MSG_WriteString("precache\n");

    // write it to the demo file
    CL_WriteDemoMessage(&msg_write);

    // the rest of the demo file will be individual frames
}

// resumes demo recording after pause or seek. tries to fit flushed
// configstrings and frame into single packet for seamless 'stitch'
static void resume_record(void)
{
    size_t len;
    const char *s;

    // write dirty configstrings
    BC_FOR_EACH(cl.dcs, index) {
        s = cl.configstrings[index];
        len = s ? strlen(s) : 0;

        if (cls.demo.buffer.cursize + len + 4 > cls.demo.buffer.maxsize) {
            if (!CL_WriteDemoMessage(&cls.demo.buffer))
                return;
            // multiple packets = not seamless
        }

        SZ_WriteByte(&cls.demo.buffer, svc_configstring);
        SZ_WriteShort(&cls.demo.buffer, index);
        SZ_Write(&cls.demo.buffer, s, len);
        SZ_WriteByte(&cls.demo.buffer, 0);
    } BC_FOR_EACH_END

    // write delta uncompressed frame
    //cls.demo.last_server_frame = -1;
    CL_EmitDemoFrame();

    // FIXME: write layout if it fits? most likely it won't

    // write it to the demo file
    CL_WriteDemoMessage(&cls.demo.buffer);
}

static void CL_Resume_f(void)
{
    if (!cls.demo.recording) {
        Com_Printf("Not recording a demo.\n");
        return;
    }

    if (!cls.demo.paused) {
        Com_Printf("Demo recording is already resumed.\n");
        return;
    }

    resume_record();

    if (!cls.demo.recording)
        // write failed
        return;

    Com_Printf("Resumed demo recording.\n");

    cls.demo.paused = false;

    // clear dirty configstrings
    memset(cl.dcs, 0, sizeof(cl.dcs));
}

static void CL_Suspend_f(void)
{
    if (!cls.demo.recording) {
        Com_Printf("Not recording a demo.\n");
        return;
    }

    if (!cls.demo.paused) {
        Com_Printf("Suspended demo recording.\n");
        cls.demo.paused = true;
        return;
    }

    // only resume if cl_demosuspendtoggle is enabled
    if (!cl_demosuspendtoggle->integer) {
        Com_Printf("Demo recording is already suspended.\n");
        return;
    }

    CL_Resume_f();
}

static int read_first_message(qhandle_t f)
{
    uint32_t    ul;
    uint16_t    us;
    size_t      msglen;
    int         read, type;

    // read magic/msglen
    read = FS_Read(&ul, 4, f);
    if (read != 4) {
        return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
    }

    // determine demo type
    if (ul == MVD_MAGIC) {
        read = FS_Read(&us, 2, f);
        if (read != 2) {
            return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
        }
        if (!us) {
            return Q_ERR_UNEXPECTED_EOF;
        }
        msglen = LittleShort(us);
        type = 1;
    } else {
        if (ul == (uint32_t)-1) {
            return Q_ERR_UNEXPECTED_EOF;
        }
        msglen = LittleLong(ul);
        type = 0;
    }

    if (msglen > sizeof(msg_read_buffer)) {
        return Q_ERR_INVALID_FORMAT;
    }

    // read packet data
    read = FS_Read(msg_read_buffer, msglen, f);
    if (read != msglen) {
        return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
    }

    SZ_InitRead(&msg_read, msg_read_buffer, msglen);
    return type;
}

static int read_next_message(qhandle_t f)
{
    uint32_t msglen;
    int read;

    // read msglen
    read = FS_Read(&msglen, 4, f);
    if (read != 4) {
        return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
    }

    // check for EOF packet
    if (msglen == (uint32_t)-1) {
        return 0;
    }

    msglen = LittleLong(msglen);
    if (msglen > sizeof(msg_read_buffer)) {
        return Q_ERR_INVALID_FORMAT;
    }

    // read packet data
    read = FS_Read(msg_read_buffer, msglen, f);
    if (read != msglen) {
        return read < 0 ? read : Q_ERR_UNEXPECTED_EOF;
    }

    SZ_InitRead(&msg_read, msg_read_buffer, msglen);
    return 1;
}

static void finish_demo(int ret)
{
    const char *s = Cvar_VariableString("nextserver");

    if (!s[0]) {
        if (ret == 0) {
            Com_Error(ERR_DISCONNECT, "Demo finished");
        } else {
            Com_Error(ERR_DROP, "Couldn't read demo: %s", Q_ErrorString(ret));
        }
    }

    CL_Disconnect(ERR_RECONNECT);

    Cbuf_AddText(&cmd_buffer, s);
    Cbuf_AddText(&cmd_buffer, "\n");

    Cvar_Set("nextserver", "");
}

static void update_status(void)
{
    if (cls.demo.file_size) {
        int64_t pos = FS_Tell(cls.demo.playback);

        if (pos > cls.demo.file_offset)
            cls.demo.file_progress = (float)(pos - cls.demo.file_offset) / cls.demo.file_size;
        else
            cls.demo.file_progress = 0.0f;
    }
}

static int parse_next_message(int wait)
{
    int ret;

    ret = read_next_message(cls.demo.playback);
    if (ret < 0 || (ret == 0 && wait == 0)) {
        finish_demo(ret);
        return -1;
    }

    update_status();

    if (ret == 0) {
        cls.demo.eof = true;
        return -1;
    }

    CL_ParseServerMessage();

    // if recording demo, write the message out
    if (cls.demo.recording && !cls.demo.paused) {
        CL_WriteDemoMessage(&cls.demo.buffer);
    }

    // save a snapshot once the full packet is parsed
    CL_EmitDemoSnapshot();

    return 0;
}

/*
====================
CL_PlayDemo_f
====================
*/
static void CL_PlayDemo_f(void)
{
    char name[MAX_OSPATH];
    qhandle_t f;
    int type;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s <filename>\n", Cmd_Argv(0));
        return;
    }

    f = FS_EasyOpenFile(name, sizeof(name), FS_MODE_READ | FS_FLAG_GZIP,
                        "demos/", Cmd_Argv(1), ".dm2");
    if (!f) {
        return;
    }

    type = read_first_message(f);
    if (type < 0) {
        Com_Printf("Couldn't read %s: %s\n", name, Q_ErrorString(type));
        FS_CloseFile(f);
        return;
    }

    if (type == 1) {
        Com_Printf("MVD support was not compiled in.\n");
        FS_CloseFile(f);
        return;
    }

    // if running a local server, kill it and reissue
    SV_Shutdown("Server was killed.\n", ERR_DISCONNECT);

    CL_Disconnect(ERR_RECONNECT);

    cls.demo.playback = f;
    cls.demo.compat = !strcmp(Cmd_Argv(2), "compat");
    cls.state = ca_connected;
    Q_strlcpy(cls.servername, COM_SkipPath(name), sizeof(cls.servername));
    cls.serverAddress.type = NA_LOOPBACK;

    Con_Popup(true);
    SCR_UpdateScreen();

    // parse the first message just read
    CL_ParseServerMessage();

    // read and parse messages util `precache' command
    for (int i = 0; cls.state == ca_connected && i < 1000; i++) {
        parse_next_message(0);
    }
}

static void CL_Demo_c(genctx_t *ctx, int argnum)
{
    if (argnum == 1) {
        FS_File_g("demos", ".dm2;.dm2.gz;.mvd2;.mvd2.gz", FS_SEARCH_RECURSIVE, ctx);
    }
}

#define MIN_SNAPSHOTS   64
#define MAX_SNAPSHOTS   250000000

/*
====================
CL_EmitDemoSnapshot

Periodically builds a fake demo packet used to reconstruct delta compression
state, configstrings and layouts at the given server frame.
====================
*/
void CL_EmitDemoSnapshot(void)
{
    demosnap_t *snap;
    int64_t pos;
    const char *from, *to;

    if (cl_demosnaps->integer <= 0)
        return;

    if (cls.demo.numsnapshots >= MAX_SNAPSHOTS)
        return;

    if (!cl.frame.valid)
        return;

    if (!cls.demo.file_size)
        return;

    pos = FS_Tell(cls.demo.playback);
    if (pos < cls.demo.last_snapshot_pos + cl_demosnaps->integer * 1000LL)
        return;

    MSG_BeginWriting();

    // write uncompressed frame
    emit_delta_frame(NULL, &cl.frame);

    // write configstrings
    for (int i = 0; i < MAX_CONFIGSTRINGS; i++) {
        from = cl.baseconfigstrings[i];
        to = cl.configstrings[i];

        if (!Q_strcmp_null(from, to))
            continue;

        MSG_WriteByte(svc_configstring);
        MSG_WriteShort(i);
        MSG_WriteString(to);
    }

    // write layout
    //MSG_WriteByte(svc_stringcmd);
    //MSG_WriteString(va("layout %s", cl.layout));

    if (msg_write.overflowed) {
        Com_DWPrintf("%s: message buffer overflowed\n", __func__);
    } else {
        snap = Z_Malloc(sizeof(*snap) + msg_write.cursize - 1);
        snap->framenum = cls.demo.frames_read - 1;
        snap->servertime = cl.frame.servertime;
        snap->filepos = pos;
        snap->msglen = msg_write.cursize;
        memcpy(snap->data, msg_write.data, msg_write.cursize);

        cls.demo.snapshots = Z_Realloc(cls.demo.snapshots, sizeof(cls.demo.snapshots[0]) * Q_ALIGN(cls.demo.numsnapshots + 1, MIN_SNAPSHOTS));
        cls.demo.snapshots[cls.demo.numsnapshots++] = snap;

        Com_DPrintf("[%d] snaplen %u\n", cls.demo.frames_read, msg_write.cursize);
    }

    SZ_Clear(&msg_write);

    cls.demo.last_snapshot_pos = pos;
}

static demosnap_t *find_snapshot(int64_t dest, bool byte_seek)
{
    int l = 0;
    int r = cls.demo.numsnapshots - 1;

    if (r < 0)
        return NULL;

    do {
        int m = (l + r) / 2;
        demosnap_t *snap = cls.demo.snapshots[m];
        int64_t pos = byte_seek ? snap->filepos : snap->servertime;
        if (pos < dest)
            l = m + 1;
        else if (pos > dest)
            r = m - 1;
        else
            return snap;
    } while (l <= r);

    return cls.demo.snapshots[max(r, 0)];
}

/*
====================
CL_FirstDemoFrame

Called after the first valid frame is parsed from the demo.
====================
*/
void CL_FirstDemoFrame(void)
{
    int64_t len, ofs;

    if (!cls.demo.playback)
        return;

    Com_DPrintf("[%d] first frame\n", cl.frame.number);
    cls.demo.starttime = cl.frame.servertime;

    // save base configstrings
    for (int i = 0; i < MAX_CONFIGSTRINGS; i++)
        cl.baseconfigstrings[i] = Z_CopyString(cl.configstrings[i]);

    // obtain file length and offset of the second frame
    len = FS_Length(cls.demo.playback);
    ofs = FS_Tell(cls.demo.playback);
    if (ofs > 0 && ofs < len) {
        cls.demo.file_offset = ofs;
        cls.demo.file_size = len - ofs;
    }

    // begin timedemo
    if (com_timedemo->integer) {
        cls.demo.time_frames = 0;
        cls.demo.time_start = Sys_Milliseconds();
    }

    // force initial snapshot
    cls.demo.last_snapshot_pos = INT64_MIN;
}

/*
====================
CL_FreeDemoSnapshots
====================
*/
void CL_FreeDemoSnapshots(void)
{
    for (int i = 0; i < cls.demo.numsnapshots; i++)
        Z_Free(cls.demo.snapshots[i]);
    cls.demo.numsnapshots = 0;

    Z_Freep(&cls.demo.snapshots);
}

/*
====================
CL_Seek_f
====================
*/
static void CL_Seek_f(void)
{
    demosnap_t *snap;
    int64_t dest, frames, pos;
    bool byte_seek, back_seek;
    char *from, *to;
    int ret;

    if (Cmd_Argc() < 2) {
        Com_Printf("Usage: %s [+-]<timespec|percent>[%%]\n", Cmd_Argv(0));
        return;
    }

    if (!cls.demo.playback) {
        Com_Printf("Not playing a demo.\n");
        return;
    }

    to = Cmd_Argv(1);

    if (strchr(to, '%')) {
        char *suf;
        float percent = strtof(to, &suf);
        if (suf == to || strcmp(suf, "%") || !isfinite(percent)) {
            Com_Printf("Invalid percentage.\n");
            return;
        }

        if (!cls.demo.file_size) {
            Com_Printf("Unknown file size, can't seek.\n");
            return;
        }

        percent = Q_clipf(percent, 0, 100);
        dest = cls.demo.file_offset + cls.demo.file_size * percent / 100;

        byte_seek = true;
        back_seek = dest < FS_Tell(cls.demo.playback);
    } else {
        if (*to == '-' || *to == '+') {
            // relative to current frame
            if (!Com_ParseTimespec(to + 1, &frames)) {
                Com_Printf("Invalid relative timespec.\n");
                return;
            }
            if (*to == '-')
                frames = -frames;
            dest = cl.frame.servertime + frames;
        } else {
            // relative to first frame
            if (!Com_ParseTimespec(to, &frames)) {
                Com_Printf("Invalid absolute timespec.\n");
                return;
            }
            dest = cls.demo.starttime + frames;
            frames = dest - cl.frame.servertime;
        }

        if (!frames)
            return; // already there

        byte_seek = false;
        back_seek = frames < 0;
    }

    if (!back_seek && cls.demo.eof && cl_demowait->integer)
        return; // already at end

    // disable effects processing
    cls.demo.seeking = true;

    // clear dirty configstrings
    memset(cl.dcs, 0, sizeof(cl.dcs));

    // stop sounds
    S_StopAllSounds();

    Com_DPrintf("[%d] seeking to %"PRId64"\n", cls.demo.frames_read, dest);

    pos = FS_Tell(cls.demo.playback);

    // seek to the previous most recent snapshot
    if (back_seek || cls.demo.last_snapshot_pos > pos) {
        snap = find_snapshot(dest, byte_seek);

        if (snap) {
            Com_DPrintf("found snap at %d\n", snap->framenum);
            ret = FS_Seek(cls.demo.playback, snap->filepos, SEEK_SET);
            if (ret < 0) {
                Com_EPrintf("Couldn't seek demo: %s\n", Q_ErrorString(ret));
                goto done;
            }

            // clear end-of-file flag
            cls.demo.eof = false;

            // reset configstrings
            for (int i = 0; i < MAX_CONFIGSTRINGS; i++) {
                from = cl.baseconfigstrings[i];
                to = cl.configstrings[i];

                if (!Q_strcmp_null(from, to))
                    continue;

                Q_SetBit(cl.dcs, i);
                Z_Free(to);
                cl.configstrings[i] = Z_CopyString(from);
            }

            SZ_InitRead(&msg_read, snap->data, snap->msglen);

            cls.demo.frames_read = snap->framenum;
            CL_SeekDemoMessage();
            Com_DPrintf("[%d] after snap parse %d\n", cls.demo.frames_read, cl.frame.number);
        } else if (back_seek) {
            Com_Printf("Couldn't seek backwards without snapshots!\n");
            goto done;
        }
    }

    // skip forward to destination frame/position
    while (1) {
        pos = byte_seek ? FS_Tell(cls.demo.playback) : cl.frame.servertime;
        if (pos >= dest)
            break;

        ret = read_next_message(cls.demo.playback);
        if (ret == 0 && cl_demowait->integer) {
            cls.demo.eof = true;
            break;
        }
        if (ret <= 0) {
            finish_demo(ret);
            return;
        }

        if (CL_SeekDemoMessage())
            goto done;
        CL_EmitDemoSnapshot();
    }

    Com_DPrintf("[%d] after skip %d\n", cls.demo.frames_read, cl.frame.number);

    // update dirty configstrings
    BC_FOR_EACH(cl.dcs, index) {
        cge->UpdateConfigstring(index);
    } BC_FOR_EACH_END

    // clear old effects
    cge->ClearState();

    // fix time delta
    cl.time = cl.frame.servertime;

    if (cls.demo.recording && !cls.demo.paused)
        resume_record();

    update_status();

done:
    cls.demo.seeking = false;
}

static void parse_info_string(demoInfo_t *info, int clientNum, int index)
{
#if 0
    char string[MAX_QPATH], *p;

    MSG_ReadString(string, sizeof(string));

    if (index >= CS_PLAYERSKINS && index < CS_PLAYERSKINS + MAX_CLIENTS) {
        if (index - CS_PLAYERSKINS == clientNum) {
            Q_strlcpy(info->pov, string, sizeof(info->pov));
            p = strchr(info->pov, '\\');
            if (p) {
                *p = 0;
            }
        }
    }
#endif
}

/*
====================
CL_GetDemoInfo
====================
*/
bool CL_GetDemoInfo(const char *path, demoInfo_t *info)
{
    qhandle_t f;
    int c, index, clientNum, type;
    bool res = false;

    FS_OpenFile(path, &f, FS_MODE_READ | FS_FLAG_GZIP);
    if (!f) {
        return false;
    }

    type = read_first_message(f);
    if (type != 0) {
        goto fail;
    }

    if (MSG_ReadByte() != svc_serverdata) {
        goto fail;
    }
    c = MSG_ReadLong();
    if (!Q2PRO_SUPPORTED(c)) {
        goto fail;
    }
    MSG_ReadLong();
    MSG_ReadByte();
    MSG_ReadString(NULL, 0);
    clientNum = MSG_ReadShort();
    MSG_ReadString(info->map, sizeof(info->map));
    MSG_ReadString(NULL, 0);

    while (1) {
        c = MSG_ReadByte();
        if (c == -1) {
            if (read_next_message(f) <= 0) {
                break;
            }
            continue; // parse new message
        }
        if (c != svc_configstring) {
            break;
        }
        index = MSG_ReadShort();
        if (index < 0 || index >= MAX_CONFIGSTRINGS) {
            goto fail;
        }
        parse_info_string(info, clientNum, index);
    }
    res = true;

fail:
    FS_CloseFile(f);
    return res;
}

// =========================================================================

void CL_CleanupDemos(void)
{
    if (cls.demo.recording) {
        CL_Stop_f();
    }

    if (cls.demo.playback) {
        FS_CloseFile(cls.demo.playback);

        if (com_timedemo->integer && cls.demo.time_frames) {
            unsigned msec = Sys_Milliseconds();

            if (msec > cls.demo.time_start) {
                float sec = (msec - cls.demo.time_start) * 0.001f;
                float fps = cls.demo.time_frames / sec;

                Com_Printf("%u frames, %3.1f seconds: %3.1f fps\n",
                           cls.demo.time_frames, sec, fps);
            }
        }
    }

    CL_FreeDemoSnapshots();

    memset(&cls.demo, 0, sizeof(cls.demo));
}

/*
====================
CL_DemoFrame
====================
*/
void CL_DemoFrame(void)
{
    if (!cls.demo.playback) {
        return;
    }

    if (cls.state != ca_active) {
        parse_next_message(0);
        return;
    }

    if (com_timedemo->integer) {
        parse_next_message(0);
        cl.time = cl.frame.servertime;
        cls.demo.time_frames++;
        return;
    }

    // wait at the end of demo
    if (cls.demo.eof) {
        if (!cl_demowait->integer)
            finish_demo(0);
        return;
    }

    // cl.time has already been advanced for this client frame
    // read the next frame to start lerp cycle again
    while (cl.frame.servertime < cl.time) {
        if (parse_next_message(cl_demowait->integer))
            break;
        if (cls.state != ca_active)
            break;
    }
}

static const cmdreg_t c_demo[] = {
    { "demo", CL_PlayDemo_f, CL_Demo_c },
    { "record", CL_Record_f, CL_Demo_c },
    { "stop", CL_Stop_f },
    { "suspend", CL_Suspend_f },
    { "resume", CL_Resume_f },
    { "seek", CL_Seek_f },

    { NULL }
};

/*
====================
CL_InitDemos
====================
*/
void CL_InitDemos(void)
{
    cl_demosnaps = Cvar_Get("cl_demosnaps", "100", 0);
    cl_demowait = Cvar_Get("cl_demowait", "0", 0);
    cl_demosuspendtoggle = Cvar_Get("cl_demosuspendtoggle", "1", 0);

    Cmd_Register(c_demo);
}
