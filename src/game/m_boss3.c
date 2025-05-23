// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
==============================================================================

boss3

==============================================================================
*/

#include "g_local.h"
#include "m_boss32.h"

void USE(Use_Boss3)(edict_t *self, edict_t *other, edict_t *activator)
{
    G_AddEvent(self, EV_BOSSTPORT, 0);

    // just hide, don't kill ent so we can trigger it again
    self->s.modelindex = 0;
    self->r.solid = SOLID_NOT;

    trap_LinkEntity(self);
}

void THINK(Think_Boss3Stand)(edict_t *self)
{
    if (self->s.frame == FRAME_stand260)
        self->s.frame = FRAME_stand201;
    else
        self->s.frame++;
    self->nextthink = level.time + HZ(10);
}

/*QUAKED monster_boss3_stand (1 .5 0) (-32 -32 0) (32 32 90)

Just stands and cycles in one place until targeted, then teleports away.
*/
void SP_monster_boss3_stand(edict_t *self)
{
    if (!M_AllowSpawn(self)) {
        G_FreeEdict(self);
        return;
    }

    self->movetype = MOVETYPE_STEP;
    self->r.solid = SOLID_BBOX;
    self->s.modelindex = gi.modelindex("models/monsters/boss3/rider/tris.md2");
    self->s.frame = FRAME_stand201;

    gi.soundindex("misc/bigtele.wav");

    VectorSet(self->r.mins, -32, -32, 0);
    VectorSet(self->r.maxs, 32, 32, 90);

    self->use = Use_Boss3;
    self->think = Think_Boss3Stand;
    self->nextthink = level.time + FRAME_TIME;
    trap_LinkEntity(self);
}
