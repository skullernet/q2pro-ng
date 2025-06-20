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

/*
=============================================================================

Encode a client frame onto the network channel

=============================================================================
*/

// some protocol optimizations are disabled when recording a demo
#define Q2PRO_OPTIMIZE(c) (!(c)->settings[CLS_RECORDING])

/*
=============
SV_EmitPacketEntities

Writes a delta update of an entity_state_t list to the message.
=============
*/
static void SV_EmitPacketEntities(client_t *client, const client_frame_t *from, const client_frame_t *to)
{
    const entity_state_t *oldent, *newent;
    int i, oldnum, newnum, oldindex, newindex, from_num_entities;

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
            i = (to->first_entity + newindex) & (client->num_entities - 1);
            newent = &client->entities[i];
            newnum = newent->number;
        }

        if (oldindex >= from_num_entities) {
            oldnum = MAX_EDICTS;
        } else {
            i = (from->first_entity + oldindex) & (client->num_entities - 1);
            oldent = &client->entities[i];
            oldnum = oldent->number;
        }

        if (newnum == oldnum) {
            // Delta update from old position. Because the force param is false,
            // this will not result in any bytes being emitted if the entity has
            // not changed at all. Note that players are always 'newentities',
            // this updates their old_origin always and prevents warping in case
            // of packet loss.
            if (newnum <= svs.maxclients) {
            }
            MSG_WriteDeltaEntity(oldent, newent, false);
            oldindex++;
            newindex++;
            continue;
        }

        if (newnum < oldnum) {
            // this is a new entity, send it from the baseline
            oldent = client->baselines[newnum >> SV_BASELINES_SHIFT];
            if (oldent) {
                oldent += (newnum & SV_BASELINES_MASK);
            } else {
                oldent = &nullEntityState;
            }
            MSG_WriteDeltaEntity(oldent, newent, true);
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

    MSG_WriteBits(ENTITYNUM_NONE, ENTITYNUM_BITS);   // end of packetentities
}

static client_frame_t *get_last_frame(client_t *client)
{
    client_frame_t *frame;

    if (client->lastframe <= 0) {
        // client is asking for a retransmit
        client->frames_nodelta++;
        return NULL;
    }

    client->frames_nodelta = 0;

    if (client->framenum - client->lastframe >= UPDATE_BACKUP) {
        // client hasn't gotten a good message through in a long time
        Com_DPrintf("%s: delta request from out-of-date packet.\n", client->name);
        return NULL;
    }

    // we have a valid message to delta from
    frame = &client->frames[client->lastframe & UPDATE_MASK];
    if (frame->number != client->lastframe) {
        // but it got never sent
        Com_DPrintf("%s: delta request from dropped frame.\n", client->name);
        return NULL;
    }

    if (client->next_entity - frame->first_entity > client->num_entities) {
        // but entities are too old
        Com_DPrintf("%s: delta request from out-of-date entities.\n", client->name);
        return NULL;
    }

    return frame;
}

/*
==================
SV_WriteFrameToClient
==================
*/
void SV_WriteFrameToClient(client_t *client)
{
    client_frame_t  *frame, *oldframe;
    int delta;

    // this is the frame we are creating
    frame = &client->frames[client->framenum & UPDATE_MASK];

    // this is the frame we are delta'ing from
    oldframe = get_last_frame(client);
    if (oldframe)
        delta = client->framenum - client->lastframe;
    else
        delta = 31;

    MSG_BeginWriting();
    MSG_WriteByte(svc_frame);
    MSG_WriteBits(client->framenum, FRAMENUM_BITS);
    MSG_WriteBits(delta, DELTAFRAME_BITS);
    MSG_WriteBits(sv.time, 32);
    MSG_WriteBits(client->frameflags, FRAMEFLAGS_BITS);

    // send over the areabits
    MSG_WriteBits(frame->areabytes, 6);
    for (int i = 0; i < frame->areabytes; i++)
        MSG_WriteBits(frame->areabits[i], 8);

    // delta encode the playerstate
    MSG_WriteDeltaPlayerstate(oldframe ? &oldframe->ps : NULL, &frame->ps);

    client->suppress_count = 0;
    client->frameflags = 0;

    // delta encode the entities
    SV_EmitPacketEntities(client, oldframe, frame);
    MSG_FlushBits();
}

/*
=============================================================================

Build a client frame structure

=============================================================================
*/

static vec3_t clientorg;

static bool SV_EntityVisible(int e, const visrow_t *mask)
{
    const server_entity_t *ent = &sv.entities[e];

    if (ent->num_clusters == -1)
        // too many leafs for individual check, go by headnode
        return CM_HeadnodeVisible(CM_NodeNum(&sv.cm, ent->headnode), mask->b);

    // check individual leafs
    for (int i = 0; i < ent->num_clusters; i++)
        if (Q_IsBitSet(mask->b, ent->clusternums[i]))
            return true;

    return false;
}

static bool SV_EntityAttenuatedAway(const edict_t *ent)
{
    float mult = Com_GetEntityLoopDistMult(&ent->s);
    float dist = Distance(clientorg, ent->s.origin) - SOUND_FULLVOLUME;

    return dist * mult > 1.0f;
}

/*
=============
SV_BuildClientFrame

Decides which entities are going to be visible to the client, and
copies off the playerstat and areabits.
=============
*/
void SV_BuildClientFrame(client_t *client)
{
    int         e;
    edict_t     *ent;
    edict_t     *clent;
    client_frame_t  *frame;
    entity_state_t  *state;
    const mleaf_t   *leaf;
    int         clientarea, clientcluster;
    visrow_t    clientphs;
    visrow_t    clientpvs;

    clent = client->edict;
    Q_assert(client->entities);

    // this is the frame we are creating
    frame = &client->frames[client->framenum & UPDATE_MASK];
    frame->number = client->framenum;
    frame->sentTime = com_eventTime; // save it for ping calc later
    frame->latency = -1; // not yet acked

    client->frames_sent++;

    // find the client's PVS
    SV_GetClient_ViewOrg(client, clientorg);

    leaf = CM_PointLeaf(&sv.cm, clientorg);
    clientarea = leaf->area;
    clientcluster = leaf->cluster;

    // calculate the visible areas
    frame->areabytes = CM_WriteAreaBits(&sv.cm, frame->areabits, clientarea);

    // grab the current player_state_t
    frame->ps = client->client->ps;

    CM_FatPVS(&sv.cm, &clientpvs, clientorg);
    BSP_ClusterVis(sv.cm.cache, &clientphs, clientcluster, DVIS_PHS);

    // build up the list of visible entities
    frame->num_entities = 0;
    frame->first_entity = client->next_entity;

    for (e = 0; e < svs.num_edicts; e++) {
        ent = SV_EdictForNum(e);

        // ignore entities not in use
        if (!ent->r.inuse)
            continue;

        Q_assert_soft(ent->s.number == e);

        // ignore ents without visible models
        if (ent->r.svflags & SVF_NOCLIENT)
            continue;

        // ignore ents without visible models unless they have an effect
        if (!HAS_EFFECTS(ent))
            continue;

#if 0
        // ignore gibs if client says so
        if (client->settings[CLS_NOGIBS]) {
            if (ent->s.effects & EF_GIB && !(ent->s.effects & EF_ROCKET))
                continue;
            if (ent->s.effects & EF_GREENGIB)
                continue;
        }
#endif

        // ignore flares if client says so
        if (ent->s.renderfx & RF_FLARE && client->settings[CLS_NOFLARES])
            continue;

        // ignore if not touching a PV leaf
        if (ent != clent && !sv_novis->integer && !(ent->r.svflags & SVF_NOCULL)) {
            // check area
            if (!CM_AreasConnected(&sv.cm, clientarea, ent->r.areanum)) {
                // doors can legally straddle two areas, so
                // we may need to check another one
                if (!CM_AreasConnected(&sv.cm, clientarea, ent->r.areanum2)) {
                    continue;        // blocked by a door
                }
            }

            if (ent->s.sound) {
                if (!SV_EntityVisible(e, &clientphs))
                    continue;
                // don't send sounds if they will be attenuated away
                if (SV_EntityAttenuatedAway(ent)) {
                    if (!ent->s.modelindex)
                        continue;
                    if (!SV_EntityVisible(e, &clientpvs))
                        continue;
                }
            } else {
                if (!SV_EntityVisible(e, &clientpvs))
                    continue;
            }
        }

        // add it to the circular client_entities array
        state = &client->entities[client->next_entity & (client->num_entities - 1)];
        *state = ent->s;

#if 0
        // clear footsteps
        if (client->settings[CLS_NOFOOTSTEPS] && (state->event[0] == EV_FOOTSTEP ||
            state->event[0] == EV_OTHER_FOOTSTEP || state->event[0] == EV_LADDER_STEP)) {
            state->event[0] = 0;
        }
#endif

        // hide POV entity from renderer, unless this is player's own entity
        if (e == frame->ps.clientnum && ent != clent && !Q2PRO_OPTIMIZE(client)) {
            state->modelindex = 0;
        }

        if (ent->r.ownernum == clent->s.number) {
            // don't mark players missiles as solid
            state->solid = 0;
        }

        frame->num_entities++;
        client->next_entity++;

        if (frame->num_entities == MAX_PACKET_ENTITIES)
            break;
    }
}
