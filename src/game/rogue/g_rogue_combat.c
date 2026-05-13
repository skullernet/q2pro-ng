// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
// g_combat.c

#include "g_local.h"

/*
ROGUE
clean up heal targets for medic
*/
void cleanupHealTarget(edict_t *ent)
{
    ent->monsterinfo.healer = NULL;
    ent->takedamage = true;
    ent->monsterinfo.aiflags &= ~AI_RESURRECTING;
    M_SetEffects(ent);
}

// **********************
// ROGUE

/*
============
T_RadiusNukeDamage

Like T_RadiusDamage, but ignores walls (skips CanDamage check, among others)
// up to KILLZONE radius, do 10,000 points
// after that, do damage linearly out to KILLZONE2 radius
============
*/
void T_RadiusNukeDamage(edict_t *inflictor, edict_t *attacker, float damage, edict_t *ignore, float radius, mod_t mod)
{
    float    points;
    edict_t *ent = NULL;
    vec3_t   dir;
    float    len;
    float    killzone, killzone2;
    trace_t  tr;
    float    dist;

    killzone = radius;
    killzone2 = radius * 2.0f;

    while ((ent = findradius(ent, inflictor->s.origin, killzone2)) != NULL) {
        // ignore nobody
        if (ent == ignore)
            continue;
        if (!ent->takedamage)
            continue;
        if (!ent->r.inuse)
            continue;
        if (!(ent->client || (ent->r.svflags & SVF_MONSTER) || (ent->flags & FL_DAMAGEABLE)))
            continue;

        len = Vec3_Distance(inflictor->s.origin, G_EntityCenter(ent));
        if (len <= killzone) {
            if (ent->client)
                ent->flags |= FL_NOGIB;
            points = 10000;
        } else if (len <= killzone2)
            points = (damage / killzone) * (killzone2 - len);
        else
            points = 0;

        if (points > 0) {
            if (ent->client)
                ent->client->nuke_time = level.time + SEC(2);
            dir = Vec3_Sub(ent->s.origin, inflictor->s.origin);
            T_Damage(ent, inflictor, attacker, dir, inflictor->s.origin, 0, points, points, DAMAGE_RADIUS, mod);
        }
    }
    ent = g_edicts; // skip the worldspawn
    // cycle through players
    while (ent) {
        if ((ent->client) && (ent->client->nuke_time != level.time + SEC(2)) && (ent->r.inuse)) {
            tr = G_TraceLine(inflictor->s.origin, ent->s.origin, inflictor->s.number, MASK_SOLID);
            if (tr.fraction == 1.0f)
                ent->client->nuke_time = level.time + SEC(2);
            else {
                dist = realrange(ent, inflictor);
                if (dist < 2048)
                    ent->client->nuke_time = max(ent->client->nuke_time, level.time + SEC(1.5f));
                else
                    ent->client->nuke_time = max(ent->client->nuke_time, level.time + SEC(1));
            }
            ent++;
        } else
            ent = NULL;
    }
}

// ROGUE
// ********************
