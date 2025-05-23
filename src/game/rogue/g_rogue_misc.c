// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"

//======================
// ROGUE
void USE(misc_nuke_core_use)(edict_t *self, edict_t *other, edict_t *activator)
{
    if (self->r.svflags & SVF_NOCLIENT)
        self->r.svflags &= ~SVF_NOCLIENT;
    else
        self->r.svflags |= SVF_NOCLIENT;
}

/*QUAKED misc_nuke_core (1 0 0) (-16 -16 -16) (16 16 16)
toggles visible/not visible. starts visible.
*/
void SP_misc_nuke_core(edict_t *ent)
{
    ent->s.modelindex = G_ModelIndex("models/objects/core/tris.md2");
    trap_LinkEntity(ent);

    ent->use = misc_nuke_core_use;
}
// ROGUE
//======================
