// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"

/*
=================
findradius2

Returns entities that have origins within a spherical area

ROGUE - tweaks for performance for tesla specific code
only returns entities that can be damaged
only returns entities that are FL_DAMAGEABLE

findradius2 (origin, radius)
=================
*/
edict_t *findradius2(edict_t *from, const vec3_t org, float rad)
{
    // rad must be positive
    vec3_t eorg;
    vec3_t mid;

    if (!from)
        from = g_edicts;
    else
        from++;
    for (; from < &g_edicts[level.num_edicts]; from++) {
        if (!from->r.inuse)
            continue;
        if (from->r.solid == SOLID_NOT)
            continue;
        if (!from->takedamage)
            continue;
        if (!(from->flags & FL_DAMAGEABLE))
            continue;
        VectorAvg(from->r.mins, from->r.maxs, mid);
        VectorAdd(from->s.origin, mid, eorg);
        if (Distance(eorg, org) > rad)
            continue;
        return from;
    }

    return NULL;
}
