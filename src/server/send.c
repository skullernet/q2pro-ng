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
// sv_send.c

#include "server.h"

/*
=============================================================================

MISC

=============================================================================
*/

char sv_outputbuf[SV_OUTPUTBUF_LENGTH];

void SV_FlushRedirect(int redirected, const char *outputbuf, size_t len)
{
    byte    buffer[MAX_PACKETLEN_DEFAULT];

    if (redirected == RD_PACKET) {
        Q_assert(len <= sizeof(buffer) - 10);
        memcpy(buffer, "\xff\xff\xff\xffprint\n", 10);
        memcpy(buffer + 10, outputbuf, len);
        NET_SendPacket(NS_SERVER, buffer, len + 10, &net_from);
    } else if (redirected == RD_CLIENT) {
        MSG_WriteByte(svc_print);
        MSG_WriteByte(PRINT_HIGH);
        MSG_WriteData(outputbuf, len);
        MSG_WriteByte(0);
        SV_ClientAddMessage(sv_client, MSG_RELIABLE | MSG_CLEAR);
    }
}

/*
=======================
SV_RateDrop

Returns true if the client is over its current
bandwidth estimation and should not be sent another packet
=======================
*/
static bool SV_RateDrop(client_t *client)
{
    size_t  total;
    int     i;

    // never drop over the loopback
    if (!client->rate) {
        return false;
    }

    total = 0;
    for (i = 0; i < RATE_MESSAGES; i++) {
        total += client->message_size[i];
    }

#if USE_FPS
    total = total * sv.frametime.div / client->framediv;
#endif

    if (total > client->rate) {
        SV_DPrintf(1, "Frame %d suppressed for %s (total = %zu)\n",
                   client->framenum, client->name, total);
        client->frameflags |= FF_SUPPRESSED;
        client->suppress_count++;
        client->message_size[client->framenum % RATE_MESSAGES] = 0;
        return true;
    }

    return false;
}

static void SV_CalcSendTime(client_t *client, unsigned size)
{
    // never drop over the loopback
    if (!client->rate) {
        client->send_time = svs.realtime;
        client->send_delta = 0;
        return;
    }

    if (client->state == cs_spawned)
        client->message_size[client->framenum % RATE_MESSAGES] = size;

    client->send_time = svs.realtime;
    client->send_delta = size * 1000 / client->rate;
}

/*
=============================================================================

EVENT MESSAGES

=============================================================================
*/


/*
=================
SV_ClientPrintf

Sends text across to be displayed if the level passes.
=================
*/
void SV_ClientPrintf(client_t *client, int level, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    size_t      len;

    if (level < client->messagelevel)
        return;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_print);
    MSG_WriteByte(level);
    MSG_WriteData(string, len + 1);

    SV_ClientAddMessage(client, MSG_RELIABLE | MSG_CLEAR);
}

/*
=================
SV_BroadcastPrintf

Sends text to all active clients.
=================
*/
void SV_BroadcastPrintf(int level, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    client_t    *client;
    size_t      len;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_print);
    MSG_WriteByte(level);
    MSG_WriteData(string, len + 1);

    FOR_EACH_CLIENT(client) {
        if (client->state != cs_spawned)
            continue;
        if (level < client->messagelevel)
            continue;
        SV_ClientAddMessage(client, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);
}

void SV_ClientCommand(client_t *client, const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    size_t      len;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_stufftext);
    MSG_WriteData(string, len + 1);

    SV_ClientAddMessage(client, MSG_RELIABLE | MSG_CLEAR);
}

/*
=================
SV_BroadcastCommand

Sends command to all active clients.
=================
*/
void SV_BroadcastCommand(const char *fmt, ...)
{
    va_list     argptr;
    char        string[MAX_STRING_CHARS];
    client_t    *client;
    size_t      len;

    va_start(argptr, fmt);
    len = Q_vsnprintf(string, sizeof(string), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(string)) {
        Com_WPrintf("%s: overflow\n", __func__);
        return;
    }

    MSG_WriteByte(svc_stufftext);
    MSG_WriteData(string, len + 1);

    FOR_EACH_CLIENT(client) {
        SV_ClientAddMessage(client, MSG_RELIABLE);
    }

    SZ_Clear(&msg_write);
}


/*
=================
SV_Multicast

Sends the contents of the write buffer to a subset of the clients,
then clears the write buffer.

MULTICAST_ALL    same as broadcast (origin can be NULL)
MULTICAST_PVS    send to clients potentially visible from org
MULTICAST_PHS    send to clients potentially hearable from org
=================
*/
void SV_Multicast(const vec3_t origin, multicast_t to)
{
    client_t        *client;
    visrow_t        mask;
    const mleaf_t   *leaf1 = NULL;
    int             flags = 0;

    if (to < MULTICAST_ALL || to > MULTICAST_PVS_R)
        Com_Error(ERR_DROP, "%s: bad to: %d", __func__, to);

    if (to >= MULTICAST_ALL_R) {
        flags |= MSG_RELIABLE;
        to -= MULTICAST_ALL_R;
    }

    if (to && !origin)
        Com_Error(ERR_DROP, "%s: NULL origin", __func__);

    if (msg_write.overflowed)
        Com_Error(ERR_DROP, "%s: message buffer overflowed", __func__);

    if (!msg_write.cursize) {
        Com_DPrintf("%s with empty data\n", __func__);
        return;
    }

    if (to) {
        leaf1 = CM_PointLeaf(&sv.cm, origin);
        BSP_ClusterVis(sv.cm.cache, &mask, leaf1->cluster, MULTICAST_PVS - to);
    }

    // send the data to all relevant clients
    FOR_EACH_CLIENT(client) {
        if (client->state < cs_primed) {
            continue;
        }
        // do not send unreliables to connecting clients
        if (!(flags & MSG_RELIABLE) && !CLIENT_ACTIVE(client)) {
            continue;
        }

        if (to) {
            const mleaf_t *leaf2 = CM_PointLeaf(&sv.cm, client->edict->s.origin);
            if (!CM_AreasConnected(&sv.cm, leaf1->area, leaf2->area))
                continue;
            if (leaf2->cluster == -1)
                continue;
            if (!Q_IsBitSet(mask.b, leaf2->cluster))
                continue;
        }

        SV_ClientAddMessage(client, flags);
    }

    // clear the buffer
    SZ_Clear(&msg_write);
}

#if USE_ZLIB
static bool can_auto_compress(const client_t *client)
{
    if (!client->has_zlib)
        return false;

    // compress only sufficiently large layouts
    if (msg_write.cursize < client->netchan.maxpacketlen / 2)
        return false;

    return true;
}

static int compress_message(const client_t *client)
{
    int     ret, len;
    byte    *hdr;

    if (!client->has_zlib)
        return 0;

    svs.z.next_in = msg_write.data;
    svs.z.avail_in = msg_write.cursize;
    svs.z.next_out = svs.z_buffer + ZPACKET_HEADER;
    svs.z.avail_out = svs.z_buffer_size - ZPACKET_HEADER;

    ret = deflate(&svs.z, Z_FINISH);
    len = svs.z.total_out;

    // prepare for next deflate()
    deflateReset(&svs.z);

    if (ret != Z_STREAM_END) {
        Com_WPrintf("Error %d compressing %u bytes message for %s\n",
                    ret, msg_write.cursize, client->name);
        return 0;
    }

    // write the packet header
    hdr = svs.z_buffer;
    hdr[0] = svc_zpacket;
    WL16(&hdr[1], len);
    WL16(&hdr[3], msg_write.cursize);

    return len + ZPACKET_HEADER;
}

static byte *get_compressed_data(void)
{
    return svs.z_buffer;
}
#else
#define can_auto_compress(c)    false
#define compress_message(c)     0
#define get_compressed_data()   NULL
#endif

/*
=======================
SV_ClientAddMessage

Adds contents of the current write buffer to client's message list.
Does NOT clean the buffer for multicast delivery purpose,
unless told otherwise.
=======================
*/
void SV_ClientAddMessage(client_t *client, int flags)
{
    sizebuf_t *buf;
    int len;

    Q_assert(!msg_write.overflowed);

    if (!msg_write.cursize) {
        return;
    }

    if ((flags & MSG_COMPRESS_AUTO) && can_auto_compress(client)) {
        flags |= MSG_COMPRESS;
    }

    buf = (flags & MSG_RELIABLE) ? &client->netchan.message : &client->datagram;

    if ((flags & MSG_COMPRESS) && (len = compress_message(client)) && len < msg_write.cursize) {
        SZ_Write(buf, get_compressed_data(), len);
        SV_DPrintf(1, "Compressed %sreliable message to %s: %u into %d\n",
                   (flags & MSG_RELIABLE) ? "" : "un", client->name, msg_write.cursize, len);
    } else {
        SZ_Write(buf, msg_write.data, msg_write.cursize);
        SV_DPrintf(2, "Added %sreliable message to %s: %u bytes\n",
                   (flags & MSG_RELIABLE) ? "" : "un", client->name, msg_write.cursize);
    }

    if (flags & MSG_CLEAR) {
        SZ_Clear(&msg_write);
    }
}

/*
===============================================================================

FRAME UPDATES

===============================================================================
*/

static void SV_SendClientDatagram(client_t *client)
{
    int cursize;

    // send over all the relevant entity_state_t
    // and the player_state_t
    SV_WriteFrameToClient(client);

    // now write unreliable messages
    // for this client out to the message
    // it is necessary for this to be after the WriteFrame
    // so that entity references will be current
    if (client->datagram.overflowed)
        Com_WPrintf("Datagram overflowed for %s\n", client->name);
    else
        MSG_WriteData(client->datagram.data, client->datagram.cursize);
    SZ_Clear(&client->datagram);

    if (msg_write.overflowed) {
        // should never really happen
        Com_WPrintf("Message overflowed for %s\n", client->name);
        SZ_Clear(&msg_write);
    }

#if USE_DEBUG
    if (sv_pad_packets->integer > 0) {
        int pad = min(msg_write.maxsize, sv_pad_packets->integer);

        while (msg_write.cursize < pad)
            MSG_WriteByte(svc_nop);
    }
#endif

    // send the datagram
    cursize = Netchan_Transmit(&client->netchan,
                               msg_write.cursize,
                               msg_write.data,
                               client->numpackets);

    // record the size for rate estimation
    SV_CalcSendTime(client, cursize);

    // clear the write buffer
    SZ_Clear(&msg_write);
}


/*
===============================================================================

COMMON STUFF

===============================================================================
*/

#if USE_DEBUG && USE_FPS
static void check_key_sync(const client_t *client)
{
    int div = sv.frametime.div / client->framediv;
    int key1 = !(sv.framenum % sv.frametime.div);
    int key2 = !(client->framenum % div);

    if (key1 != key2) {
        Com_LPrintf(PRINT_DEVELOPER,
                    "[%d] frame %d for %s not synced (%d != %d)\n",
                    sv.framenum, client->framenum, client->name, key1, key2);
    }
}
#endif

/*
=======================
SV_SendClientMessages

Called each game frame, sends svc_frame messages to spawned clients only.
Clients in earlier connection state are handled in SV_SendAsyncPackets.
=======================
*/
void SV_SendClientMessages(void)
{
    client_t    *client;
    int         cursize;

    // send a message to each connected client
    FOR_EACH_CLIENT(client) {
        if (!CLIENT_ACTIVE(client))
            goto finish;

        if (!SV_CLIENTSYNC(client))
            continue;

#if USE_DEBUG && USE_FPS
        if (developer->integer)
            check_key_sync(client);
#endif

        // if the reliable message overflowed,
        // drop the client (should never happen)
        if (client->netchan.message.overflowed) {
            SZ_Clear(&client->netchan.message);
            SV_DropClient(client, "reliable message overflowed");
            goto finish;
        }

        // don't overrun bandwidth
        if (SV_RateDrop(client))
            goto advance;

        // don't write any frame data until all fragments are sent
        if (client->netchan.fragment_pending) {
            client->frameflags |= FF_SUPPRESSED;
            cursize = Netchan_TransmitNextFragment(&client->netchan);
            SV_CalcSendTime(client, cursize);
            goto advance;
        }

        // build the new frame and write it
        SV_BuildClientFrame(client);
        SV_SendClientDatagram(client);

advance:
        // advance for next frame
        client->framenum++;

finish:
        // clear all unreliable messages still left
        SZ_Clear(&client->datagram);
    }
}

static void write_pending_download(client_t *client)
{
    sizebuf_t   *buf = &client->netchan.message;
    int         chunk;

    if (!client->download)
        return;

    if (!client->downloadpending)
        return;

    if (client->netchan.reliable_length)
        return;

    if (buf->cursize >= client->netchan.maxpacketlen - 4)
        return;

    chunk = min(client->downloadsize - client->downloadcount,
                client->netchan.maxpacketlen - buf->cursize - 4);

    client->downloadpending = false;
    client->downloadcount += chunk;

    SZ_WriteByte(buf, client->downloadcmd);
    SZ_WriteShort(buf, chunk);
    SZ_WriteByte(buf, client->downloadcount * 100 / client->downloadsize);
    SZ_Write(buf, client->download + client->downloadcount - chunk, chunk);

    if (client->downloadcount == client->downloadsize) {
        SV_CloseDownload(client);
        SV_AlignKeyFrames(client);
    }
}

/*
==================
SV_SendAsyncPackets

If the client is just connecting, it is pointless to wait another 100ms
before sending next command and/or reliable acknowledge, send it as soon
as client rate limit allows.

For spawned clients, this is not used, as we are forced to send svc_frame
packets synchronously with game DLL ticks.
==================
*/
void SV_SendAsyncPackets(void)
{
    bool        retransmit;
    client_t    *client;
    netchan_t   *netchan;
    int         cursize;

    FOR_EACH_CLIENT(client) {
        // don't overrun bandwidth
        if (svs.realtime - client->send_time < client->send_delta) {
            continue;
        }

        netchan = &client->netchan;

        // make sure all fragments are transmitted first
        if (netchan->fragment_pending) {
            cursize = Netchan_TransmitNextFragment(netchan);
            SV_DPrintf(2, "%s: frag: %d\n", client->name, cursize);
            goto calctime;
        }

        // spawned clients are handled elsewhere
        if (CLIENT_ACTIVE(client) && !SV_PAUSED) {
            continue;
        }

        // see if it's time to resend a (possibly dropped) packet
        retransmit = (com_localTime - netchan->last_sent > 1000);

        // don't write new reliables if not yet acknowledged
        if (netchan->reliable_length && !retransmit && client->state != cs_zombie) {
            continue;
        }

        // now fill up remaining buffer space with download
        write_pending_download(client);

        if (netchan->message.cursize || netchan->reliable_ack_pending ||
            netchan->reliable_length || retransmit) {
            cursize = Netchan_Transmit(netchan, 0, NULL, 1);
            SV_DPrintf(2, "%s: send: %d\n", client->name, cursize);
calctime:
            SV_CalcSendTime(client, cursize);
        }
    }
}
