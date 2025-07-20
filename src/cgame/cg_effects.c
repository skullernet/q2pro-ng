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
// cg_effects.c -- entity effects parsing and management

#include "cg_local.h"

static void CG_LogoutEffect(const vec3_t org, int color);

static vec3_t avelocities[NUMVERTEXNORMALS];

/*
==============================================================

LIGHT STYLE MANAGEMENT

==============================================================
*/

/*
================
CG_SetLightStyle
================
*/
void CG_SetLightStyle(int index, const char *s)
{
    int     i;
    cg_lightstyle_t *ls;

    ls = &cgs.lightstyles[index];
    ls->length = strlen(s);
    Q_assert(ls->length < MAX_QPATH);

    for (i = 0; i < ls->length; i++)
        ls->map[i] = (float)(s[i] - 'a') / (float)('m' - 'a');
}

/*
================
CG_AddLightStyles
================
*/
void CG_AddLightStyles(void)
{
    int     i, ofs = cg.time / 100;
    cg_lightstyle_t *ls;

    if (cg_lerp_lightstyles.integer) {
        float f = (cg.time % 100) * 0.01f;
        float b = 1.0f - f;

        for (i = 0, ls = cgs.lightstyles; i < MAX_LIGHTSTYLES; i++, ls++) {
            float value = 1.0f;

            if (ls->length > 1)
                value = ls->map[ofs % ls->length] * b + ls->map[(ofs + 1) % ls->length] * f;
            else if (ls->length)
                value = ls->map[0];

            trap_R_SetLightStyle(i, value);
        }
    } else {
        for (i = 0, ls = cgs.lightstyles; i < MAX_LIGHTSTYLES; i++, ls++) {
            float value = ls->length ? ls->map[ofs % ls->length] : 1.0f;
            trap_R_SetLightStyle(i, value);
        }
    }
}

/*
==============================================================

DLIGHT MANAGEMENT

==============================================================
*/

static cdlight_t       cg_dlights[MAX_DLIGHTS];

static void CG_ClearDlights(void)
{
    memset(cg_dlights, 0, sizeof(cg_dlights));
}

/*
===============
CG_AllocDlight
===============
*/
cdlight_t *CG_AllocDlight(int key)
{
    int     i;
    cdlight_t   *dl;

// first look for an exact key match
    if (key) {
        dl = cg_dlights;
        for (i = 0; i < MAX_DLIGHTS; i++, dl++) {
            if (dl->key == key) {
                memset(dl, 0, sizeof(*dl));
                dl->key = key;
                return dl;
            }
        }
    }

// then look for anything else
    dl = cg_dlights;
    for (i = 0; i < MAX_DLIGHTS; i++, dl++) {
        if (dl->die < cg.time) {
            memset(dl, 0, sizeof(*dl));
            dl->key = key;
            return dl;
        }
    }

    dl = &cg_dlights[0];
    memset(dl, 0, sizeof(*dl));
    dl->key = key;
    return dl;
}

/*
===============
CG_AddDLights
===============
*/
void CG_AddDLights(void)
{
    int         i;
    cdlight_t   *dl;

    dl = cg_dlights;
    for (i = 0; i < MAX_DLIGHTS; i++, dl++) {
        if (dl->die < cg.time)
            continue;
        trap_R_AddLight(dl->origin, dl->radius,
                        dl->color[0], dl->color[1], dl->color[2]);
    }
}

// ==============================================================

static void CG_AddWeaponKick(float scale, float pitch)
{
    VectorScale(cg.v_forward, scale, cg.weapon.kick.origin);
    VectorSet(cg.weapon.kick.angles, pitch, 0, 0);
}

static void CG_AddHyperblasterKick(void)
{
    VectorScale(cg.v_forward, -2, cg.weapon.kick.origin);
    for (int i = 0; i < 3; i++)
        cg.weapon.kick.angles[i] = crand() * 0.7f;
}

static void CG_AddMachinegunKick(void)
{
    for (int i = 0; i < 3; i++) {
        cg.weapon.kick.origin[i] = crand() * 0.35f;
        cg.weapon.kick.angles[i] = crand() * 0.7f;
    }
}

static void CG_AddChaingunKick(int shots)
{
    for (int i = 0; i < 3; i++) {
        cg.weapon.kick.origin[i] = crand() * 0.35f;
        cg.weapon.kick.angles[i] = crand() * (0.5f + (shots * 0.15f));
    }
}

static void CG_AddETFRifleKick(void)
{
    for (int i = 0; i < 3; i++) {
        cg.weapon.kick.origin[i] = crand() * 0.85f;
        cg.weapon.kick.angles[i] = crand() * 0.85f;
    }
}

static void CG_AddBFGKick(void)
{
    VectorScale(cg.v_forward, -2, cg.weapon.kick.origin);
    cg.v_dmg_pitch = -40;
    cg.v_dmg_roll = crand() * 8;
    cg.v_dmg_time = cg.oldframe->servertime + DAMAGE_TIME;
}

/*
==============
CG_MuzzleFlash
==============
*/
void CG_MuzzleFlash(centity_t *pl, int weapon)
{
    vec3_t      fv, rv;
    cdlight_t   *dl;
    float       volume;
    char        soundname[MAX_QPATH];
    bool        silenced, local;
    int         entnum;

    silenced = weapon & MZ_SILENCED;
    weapon &= ~MZ_SILENCED;

    entnum = pl->current.number;
    local = entnum == cg.frame->ps.clientnum;

    dl = CG_AllocDlight(entnum);
    VectorCopy(pl->current.origin, dl->origin);
    AngleVectors(pl->current.angles, fv, rv, NULL);
    VectorMA(dl->origin, 18, fv, dl->origin);
    VectorMA(dl->origin, 16, rv, dl->origin);
    dl->radius = 100 * (2 - silenced) + (Q_rand() & 31);
    dl->die = cg.time + Q_clip(cg_muzzlelight_time.integer, 0, 1000);

    volume = 1.0f - 0.8f * silenced;

    switch (weapon) {
    case MZ_BLASTER:
        VectorSet(dl->color, 1, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/blastf1a.wav"), volume, ATTN_NORM, 0);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_BLAST, (const vec3_t) { 27.0f, 7.4f, -6.6f }, 8.0f);
            CG_AddWeaponKick(-2, -1);
        }
        break;
    case MZ_BLUEHYPERBLASTER:
        VectorSet(dl->color, 0, 0, 1);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/hyprbf1a.wav"), volume, ATTN_NORM, 0);
        break;
    case MZ_HYPERBLASTER:
        VectorSet(dl->color, 1, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/hyprbf1a.wav"), volume, ATTN_NORM, 0);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_BLAST, (const vec3_t) { 23.5f, 6.0f, -6.0f }, 9.0f);
            CG_AddHyperblasterKick();
        }
        break;
    case MZ_MACHINEGUN:
        VectorSet(dl->color, 1, 1, 0);
        Q_snprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (Q_rand() % 5) + 1);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound(soundname), volume, ATTN_NORM, 0);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_MACHN, (const vec3_t) { 29.0f, 9.7f, -8.0f }, 12.0f);
            CG_AddMachinegunKick();
        }
        break;
    case MZ_SHOTGUN:
        VectorSet(dl->color, 1, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/shotgf1b.wav"), volume, ATTN_NORM, 0);
        trap_S_StartSound(NULL, entnum, CHAN_AUTO,   trap_S_RegisterSound("weapons/shotgr1b.wav"), volume, ATTN_NORM, 0.1f);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_SHOTG, (const vec3_t) { 26.5f, 8.6f, -9.5f }, 12.0f);
            CG_AddWeaponKick(-2, -2);
        }
        break;
    case MZ_SSHOTGUN:
        VectorSet(dl->color, 1, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/sshotf1b.wav"), volume, ATTN_NORM, 0);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_SHOTG2, (const vec3_t) { 25.0f, 7.0f, -5.5f }, 12.0f);
            CG_AddWeaponKick(-2, -2);
        }
        break;
    case MZ_CHAINGUN1:
        dl->radius = 200 + (Q_rand() & 31);
        VectorSet(dl->color, 1, 0.25f, 0);
        Q_snprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (Q_rand() % 5) + 1);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound(soundname), volume, ATTN_NORM, 0);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_MACHN, (const vec3_t) { 29.0f, 9.7f, -10.0f }, 12.0f);
            CG_AddChaingunKick(1);
        }
        break;
    case MZ_CHAINGUN2:
        dl->radius = 225 + (Q_rand() & 31);
        VectorSet(dl->color, 1, 0.5f, 0);
        Q_snprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (Q_rand() % 5) + 1);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound(soundname), volume, ATTN_NORM, 0);
        Q_snprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (Q_rand() % 5) + 1);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound(soundname), volume, ATTN_NORM, 0.05f);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_MACHN, (const vec3_t) { 29.0f, 9.7f, -10.0f }, 16.0f);
            CG_AddChaingunKick(2);
        }
        break;
    case MZ_CHAINGUN3:
        dl->radius = 250 + (Q_rand() & 31);
        VectorSet(dl->color, 1, 1, 0);
        Q_snprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (Q_rand() % 5) + 1);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound(soundname), volume, ATTN_NORM, 0);
        Q_snprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (Q_rand() % 5) + 1);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound(soundname), volume, ATTN_NORM, 0.033f);
        Q_snprintf(soundname, sizeof(soundname), "weapons/machgf%ib.wav", (Q_rand() % 5) + 1);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound(soundname), volume, ATTN_NORM, 0.066f);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_MACHN, (const vec3_t) { 29.0f, 9.7f, -10.0f }, 20.0f);
            CG_AddChaingunKick(3);
        }
        break;
    case MZ_RAILGUN:
        VectorSet(dl->color, 0.5f, 0.5f, 1.0f);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/railgf1a.wav"), volume, ATTN_NORM, 0);
        trap_S_StartSound(NULL, entnum, CHAN_AUTO,   trap_S_RegisterSound("weapons/railgr1b.wav"), volume, ATTN_NORM, 0.4f);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_RAIL, (const vec3_t) { 20.0f, 5.2f, -7.0f }, 12.0f);
            CG_AddWeaponKick(-3, -3);
        }
        break;
    case MZ_ROCKET:
        VectorSet(dl->color, 1, 0.5f, 0.2f);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/rocklf1a.wav"), volume, ATTN_NORM, 0);
        trap_S_StartSound(NULL, entnum, CHAN_AUTO,   trap_S_RegisterSound("weapons/rocklr1b.wav"), volume, ATTN_NORM, 0.1f);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_ROCKET, (const vec3_t) { 20.8f, 5.0f, -11.0f }, 10.0f);
            CG_AddWeaponKick(-2, -1);
        }
        break;
    case MZ_GRENADE:
        VectorSet(dl->color, 1, 0.5f, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/grenlf1a.wav"), volume, ATTN_NORM, 0);
        trap_S_StartSound(NULL, entnum, CHAN_AUTO,   trap_S_RegisterSound("weapons/grenlr1b.wav"), volume, ATTN_NORM, 0.1f);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_LAUNCH, (const vec3_t) { 18.0f, 6.0f, -6.5f }, 9.0f);
            CG_AddWeaponKick(-2, -1);
        }
        break;
    case MZ_BFG:
        VectorSet(dl->color, 0, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/bfg__f1y.wav"), volume, ATTN_NORM, 0);
        break;
    case MZ_BFG2:
        VectorSet(dl->color, 0, 1, 0);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_BFG, (const vec3_t) { 18.0f, 8.0f, -7.5f }, 16.0f);
            CG_AddBFGKick();
        }
        break;

    case MZ_LOGIN:
        VectorSet(dl->color, 0, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/grenlf1a.wav"), 1, ATTN_NORM, 0);
        CG_LogoutEffect(pl->current.origin, 0xd0);  // green
        break;
    case MZ_LOGOUT:
        VectorSet(dl->color, 1, 0, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/grenlf1a.wav"), 1, ATTN_NORM, 0);
        CG_LogoutEffect(pl->current.origin, 0x40);  // red
        break;
    case MZ_RESPAWN:
        VectorSet(dl->color, 1, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/grenlf1a.wav"), 1, ATTN_NORM, 0);
        CG_LogoutEffect(pl->current.origin, 0xe0);  // yellow
        break;
    case MZ_PHALANX:
        VectorSet(dl->color, 1, 0.5f, 0.5f);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/plasshot.wav"), volume, ATTN_NORM, 0);
        if (local)
            CG_AddWeaponKick(-2, -2);
        break;
    case MZ_PHALANX2:
        VectorSet(dl->color, 1, 0.5f, 0.5f);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_ROCKET, (const vec3_t) { 18.0f, 10.0f, -6.0f }, 9.0f);
            CG_AddWeaponKick(-2, -2);
        }
        break;
    case MZ_IONRIPPER:
        VectorSet(dl->color, 1, 0.5f, 0.5f);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/rippfire.wav"), volume, ATTN_NORM, 0);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_BOOMER, (const vec3_t) { 24.0f, 3.8f, -5.5f }, 15.0f);
            CG_AddWeaponKick(-3, -3);
        }
        break;

    case MZ_PROX:
        VectorSet(dl->color, 1, 0.5f, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/grenlf1a.wav"), volume, ATTN_NORM, 0);
        trap_S_StartSound(NULL, entnum, CHAN_AUTO,   trap_S_RegisterSound("weapons/proxlr1a.wav"), volume, ATTN_NORM, 0.1f);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_LAUNCH, (const vec3_t) { 18.0f, 6.0f, -6.5f }, 9.0f);
            CG_AddWeaponKick(-2, -1);
        }
        break;
    case MZ_ETF_RIFLE:
        VectorSet(dl->color, 0.9f, 0.7f, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/nail1.wav"), volume, ATTN_NORM, 0);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_ETF_RIFLE, (const vec3_t) { 24.0f, 5.25f, -5.5f }, 4.0f);
            CG_AddETFRifleKick();
        }
        break;
    case MZ_ETF_RIFLE_2:
        VectorSet(dl->color, 0.9f, 0.7f, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/nail1.wav"), volume, ATTN_NORM, 0);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_ETF_RIFLE, (const vec3_t) { 24.0f, 4.0f, -5.5f }, 4.0f);
            CG_AddETFRifleKick();
        }
        break;
    case MZ_HEATBEAM:
        VectorSet(dl->color, 1, 1, 0);
        dl->die = cg.time + 100;
//      trap_S_StartSound (NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/bfg__l1a.wav"), volume, ATTN_NORM, 0);
        if (local)
            CG_AddWeaponMuzzleFX(MFLASH_BEAMER, (const vec3_t) { 18.0f, 6.0f, -8.5f }, 16.0f);
        break;
    case MZ_BLASTER2:
        VectorSet(dl->color, 0, 1, 0);
        // FIXME - different sound for blaster2 ??
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/blastf1a.wav"), volume, ATTN_NORM, 0);
        if (local)
            CG_AddWeaponKick(-2, -1);
        break;
    case MZ_TRACKER:
        // negative flashes handled the same in gl/soft until CG_AddDLights
        VectorSet(dl->color, -1, -1, -1);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/disint2.wav"), volume, ATTN_NORM, 0);
        if (local) {
            CG_AddWeaponMuzzleFX(MFLASH_DIST, (const vec3_t) { 18.0f, 6.0f, -6.5f }, 10.0f);
            CG_AddWeaponKick(-2, -1);
        }
        break;
    case MZ_NUKE1:
        VectorSet(dl->color, 1, 0, 0);
        dl->die = cg.time + 100;
        break;
    case MZ_NUKE2:
        VectorSet(dl->color, 1, 1, 0);
        dl->die = cg.time + 100;
        break;
    case MZ_NUKE4:
        VectorSet(dl->color, 0, 0, 1);
        dl->die = cg.time + 100;
        break;
    case MZ_NUKE8:
        VectorSet(dl->color, 0, 1, 1);
        dl->die = cg.time + 100;
        break;
    }

    if (cg_dlight_hacks.integer & DLHACK_NO_MUZZLEFLASH) {
        switch (weapon) {
        case MZ_MACHINEGUN:
        case MZ_CHAINGUN1:
        case MZ_CHAINGUN2:
        case MZ_CHAINGUN3:
            memset(dl, 0, sizeof(*dl));
            break;
        }
    }
}


/*
==============
CG_MuzzleFlash2
==============
*/
void CG_MuzzleFlash2(centity_t *ent, int weapon)
{
    vec3_t      ofs, origin, flash_origin;
    cdlight_t   *dl;
    vec3_t      forward, right;
    char        soundname[MAX_QPATH];
    float       scale;
    int         entnum;

    // locate the origin
    AngleVectors(ent->current.angles, forward, right, NULL);

    scale = ent->current.scale;
    if (!scale)
        scale = 1.0f;

    if (weapon >= q_countof(monster_flash_offset))
        Com_Error(ERR_DROP, "%s: bad weapon", __func__);

    entnum = ent->current.number;

    VectorScale(monster_flash_offset[weapon], scale, ofs);
    origin[0] = ent->current.origin[0] + forward[0] * ofs[0] + right[0] * ofs[1];
    origin[1] = ent->current.origin[1] + forward[1] * ofs[0] + right[1] * ofs[1];
    origin[2] = ent->current.origin[2] + forward[2] * ofs[0] + right[2] * ofs[1] + ofs[2];

    VectorMA(origin, 4.0f * scale, forward, flash_origin);

    dl = CG_AllocDlight(entnum);
    VectorCopy(origin, dl->origin);
    dl->radius = 200 + (Q_rand() & 31);
    dl->die = cg.time + Q_clip(cg_muzzlelight_time.integer, 0, 1000);

    switch (weapon) {
    case MZ2_INFANTRY_MACHINEGUN_1 ... MZ2_INFANTRY_MACHINEGUN_22:
        VectorSet(dl->color, 1, 1, 0);
        CG_ParticleEffect(origin, vec3_origin, 0, 40);
        CG_SmokeAndFlash(origin);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("infantry/infatck1.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_MACHN, 0, 18.0f * scale);
        break;

    case MZ2_SOLDIER_MACHINEGUN_1 ... MZ2_SOLDIER_MACHINEGUN_9:
        VectorSet(dl->color, 1, 1, 0);
        CG_ParticleEffect(origin, vec3_origin, 0, 40);
        CG_SmokeAndFlash(origin);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("soldier/solatck3.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_MACHN, 0, 13.0f * scale);
        break;

    case MZ2_GUNNER_MACHINEGUN_1 ... MZ2_GUNNER_MACHINEGUN_8:
        VectorSet(dl->color, 1, 1, 0);
        CG_ParticleEffect(origin, vec3_origin, 0, 40);
        CG_SmokeAndFlash(origin);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("gunner/gunatck2.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_MACHN, 0, 24.0f * scale);
        break;

    case MZ2_SUPERTANK_MACHINEGUN_1 ... MZ2_SUPERTANK_MACHINEGUN_6:
    case MZ2_ACTOR_MACHINEGUN_1:
    case MZ2_TURRET_MACHINEGUN:
    case MZ2_BOSS2_MACHINEGUN_L1:
    case MZ2_CARRIER_MACHINEGUN_L1:
    case MZ2_CARRIER_MACHINEGUN_L2:
        VectorSet(dl->color, 1, 1, 0);
        CG_ParticleEffect(origin, vec3_origin, 0, 40);
        CG_SmokeAndFlash(origin);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("infantry/infatck1.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_MACHN, 0, 32.0f * scale);
        break;

    case MZ2_BOSS2_MACHINEGUN_R1:
    case MZ2_CARRIER_MACHINEGUN_R1:
    case MZ2_CARRIER_MACHINEGUN_R2:
        VectorSet(dl->color, 1, 1, 0);
        CG_ParticleEffect(origin, vec3_origin, 0, 40);
        CG_SmokeAndFlash(origin);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_MACHN, 0, 32.0f * scale);
        break;

    case MZ2_BOSS2_HYPERBLASTER_L1:
    case MZ2_BOSS2_HYPERBLASTER_R1:
        VectorSet(dl->color, 1, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("flyer/flyatck3.wav"), 1, ATTN_NONE, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_BLAST, 0, 12.0f * scale);
        break;

    case MZ2_SOLDIER_BLASTER_1 ... MZ2_SOLDIER_BLASTER_9:
    case MZ2_TURRET_BLASTER:
        VectorSet(dl->color, 1, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("soldier/solatck2.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_BLAST, 0, 8.0f * scale);
        break;

    case MZ2_FLYER_BLASTER_1:
    case MZ2_FLYER_BLASTER_2:
        VectorSet(dl->color, 1, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("flyer/flyatck3.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_BLAST, 0, 8.0f * scale);
        break;

    case MZ2_MEDIC_BLASTER_1:
    case MZ2_MEDIC_HYPERBLASTER1_1 ... MZ2_MEDIC_HYPERBLASTER1_12:
        VectorSet(dl->color, 1, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("medic/medatck1.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_BLAST, 0, 8.0f * scale);
        break;

    case MZ2_HOVER_BLASTER_1:
    case MZ2_HOVER_BLASTER_2:
        VectorSet(dl->color, 1, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("hover/hovatck1.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_BLAST, 0, 8.0f * scale);
        break;

    case MZ2_FLOAT_BLASTER_1:
        VectorSet(dl->color, 1, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("floater/fltatck1.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_BLAST, 0, 8.0f * scale);
        break;

    case MZ2_SOLDIER_SHOTGUN_1 ... MZ2_SOLDIER_SHOTGUN_9:
        VectorSet(dl->color, 1, 1, 0);
        CG_SmokeAndFlash(origin);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("soldier/solatck1.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_SHOTG, 0, 17.0f * scale);
        break;

    case MZ2_TANK_BLASTER_1 ... MZ2_TANK_BLASTER_3:
        VectorSet(dl->color, 1, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("tank/tnkatck3.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_BLAST, 0, 24.0f * scale);
        break;

    case MZ2_TANK_MACHINEGUN_1 ... MZ2_TANK_MACHINEGUN_19:
        VectorSet(dl->color, 1, 1, 0);
        CG_ParticleEffect(origin, vec3_origin, 0, 40);
        CG_SmokeAndFlash(origin);
        Q_snprintf(soundname, sizeof(soundname), "tank/tnkatk2%c.wav", 'a' + Q_rand() % 5);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound(soundname), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_MACHN, 0, 20.0f * scale);
        break;

    case MZ2_CHICK_ROCKET_1:
    case MZ2_TURRET_ROCKET:
        VectorSet(dl->color, 1, 0.5f, 0.2f);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("chick/chkatck2.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_ROCKET, 0, 16.0f * scale);
        break;

    case MZ2_TANK_ROCKET_1 ... MZ2_TANK_ROCKET_3:
        VectorSet(dl->color, 1, 0.5f, 0.2f);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("tank/tnkatck1.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_ROCKET, 0, 28.0f * scale);
        break;

    case MZ2_SUPERTANK_ROCKET_1 ... MZ2_SUPERTANK_ROCKET_3:
    case MZ2_BOSS2_ROCKET_1 ... MZ2_BOSS2_ROCKET_4:
    case MZ2_CARRIER_ROCKET_1:
        VectorSet(dl->color, 1, 0.5f, 0.2f);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("tank/rocket.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_ROCKET, 0, 28.0f * scale);
        break;

    case MZ2_GUNNER_GRENADE_1 ... MZ2_GUNNER_GRENADE2_4:
    case MZ2_SUPERTANK_GRENADE_1:
    case MZ2_SUPERTANK_GRENADE_2:
        VectorSet(dl->color, 1, 0.5f, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("gunner/gunatck3.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_LAUNCH, 0, 18.0f * scale);
        break;

    case MZ2_GLADIATOR_RAILGUN_1:
    case MZ2_CARRIER_RAILGUN:
    case MZ2_WIDOW_RAIL:
    case MZ2_MAKRON_RAILGUN_1:
    case MZ2_ARACHNID_RAIL1:
    case MZ2_ARACHNID_RAIL2:
    case MZ2_ARACHNID_RAIL_UP1:
    case MZ2_ARACHNID_RAIL_UP2:
        VectorSet(dl->color, 0.5f, 0.5f, 1.0f);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_RAIL, 0, 32.0f * scale);
        break;

    case MZ2_MAKRON_BFG:
        VectorSet(dl->color, 0.5f, 1, 0.5f);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_BFG, 0, 64.0f * scale);
        break;

    case MZ2_MAKRON_BLASTER_1 ... MZ2_MAKRON_BLASTER_17:
        VectorSet(dl->color, 1, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("makron/blaster.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_BLAST, 0, 22.0f * scale);
        break;

    case MZ2_JORG_MACHINEGUN_L1 ... MZ2_JORG_MACHINEGUN_L6:
        VectorSet(dl->color, 1, 1, 0);
        CG_ParticleEffect(origin, vec3_origin, 0, 40);
        CG_SmokeAndFlash(origin);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("boss3/xfire.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_MACHN, 0, 32.0f * scale);
        break;

    case MZ2_JORG_MACHINEGUN_R1 ... MZ2_JORG_MACHINEGUN_R6:
        VectorSet(dl->color, 1, 1, 0);
        CG_ParticleEffect(origin, vec3_origin, 0, 40);
        CG_SmokeAndFlash(origin);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_MACHN, 0, 32.0f * scale);
        break;

    case MZ2_JORG_BFG_1:
        VectorSet(dl->color, 0.5f, 1, 0.5f);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_BFG, 0, 64.0f * scale);
        break;

    case MZ2_STALKER_BLASTER:
    case MZ2_DAEDALUS_BLASTER_1:
    case MZ2_DAEDALUS_BLASTER_2:
    case MZ2_MEDIC_BLASTER_2:
    case MZ2_WIDOW_BLASTER:
    case MZ2_WIDOW_BLASTER_SWEEP1 ... MZ2_WIDOW_RUN_8:
    case MZ2_MEDIC_HYPERBLASTER2_1 ... MZ2_MEDIC_HYPERBLASTER2_12:
        VectorSet(dl->color, 0, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("tank/tnkatck3.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_BLAST, 2, 22.0f * scale);
        break;

    case MZ2_WIDOW_DISRUPTOR:
        VectorSet(dl->color, -1, -1, -1);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/disint2.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_DIST, 0, 32.0f * scale);
        break;

    case MZ2_WIDOW_PLASMABEAM:
    case MZ2_WIDOW2_BEAMER_1 ... MZ2_WIDOW2_BEAM_SWEEP_11:
        dl->radius = 300 + (Q_rand() & 100);
        VectorSet(dl->color, 1, 1, 0);
        dl->die = cg.time + 200;
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_BEAMER, 0, 32.0f * scale);
        break;

    case MZ2_SOLDIER_RIPPER_1 ... MZ2_SOLDIER_RIPPER_9:
        VectorSet(dl->color, 1, 0.5f, 0.5f);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/rippfire.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_BOOMER, 0, 32.0f * scale);
        break;

    case MZ2_SOLDIER_HYPERGUN_1 ... MZ2_SOLDIER_HYPERGUN_9:
        VectorSet(dl->color, 0, 0, 1);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/hyprbf1a.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_BLAST, 1, 8.0f * scale);
        break;

    case MZ2_GUARDIAN_BLASTER:
        VectorSet(dl->color, 1, 1, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("weapons/hyprbf1a.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_BLAST, 0, 16.0f * scale);
        break;

    case MZ2_GUNCMDR_CHAINGUN_1:
    case MZ2_GUNCMDR_CHAINGUN_2:
        VectorSet(dl->color, 0, 0, 1);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("guncmdr/gcdratck2.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_ETF_RIFLE, 0, 16.0f * scale);
        break;

    case MZ2_GUNCMDR_GRENADE_MORTAR_1 ... MZ2_GUNCMDR_GRENADE_FRONT_3:
        VectorSet(dl->color, 1, 0.5f, 0);
        trap_S_StartSound(NULL, entnum, CHAN_WEAPON, trap_S_RegisterSound("guncmdr/gcdratck3.wav"), 1, ATTN_NORM, 0);
        CG_AddMuzzleFX(flash_origin, ent->current.angles, MFLASH_LAUNCH, 0, 18.0f * scale);
        break;
    }
}

/*
==============================================================

PARTICLE MANAGEMENT

==============================================================
*/

static cparticle_t  *active_particles, *free_particles;

static cparticle_t  particles[MAX_PARTICLES];

static void CG_ClearParticles(void)
{
    int     i;

    free_particles = &particles[0];
    active_particles = NULL;

    for (i = 0; i < MAX_PARTICLES - 1; i++)
        particles[i].next = &particles[i + 1];
    particles[i].next = NULL;
}

cparticle_t *CG_AllocParticle(void)
{
    cparticle_t *p;

    if (!free_particles)
        return NULL;
    p = free_particles;
    free_particles = p->next;
    p->next = active_particles;
    active_particles = p;

    p->scale = 1.0f;
    return p;
}

/*
===============
CG_ParticleEffect

Wall impact puffs
===============
*/
void CG_ParticleEffect(const vec3_t org, const vec3_t dir, int color, int count)
{
    int         i, j;
    cparticle_t *p;
    float       d;

    for (i = 0; i < count; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = color + (Q_rand() & 7);

        d = Q_rand() & 31;
        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((int)(Q_rand() & 7) - 4) + d * dir[j];
            p->vel[j] = crand() * 20;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CG_ParticleEffect2
===============
*/
void CG_ParticleEffect2(const vec3_t org, const vec3_t dir, int color, int count)
{
    int         i, j;
    cparticle_t *p;
    float       d;

    for (i = 0; i < count; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = color;

        d = Q_rand() & 7;
        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((int)(Q_rand() & 7) - 4) + d * dir[j];
            p->vel[j] = crand() * 20;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CG_TeleporterParticles
===============
*/
void CG_TeleporterParticles(const vec3_t org)
{
    int         i, j;
    cparticle_t *p;

    for (i = 0; i < 8; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = 0xdb;

        for (j = 0; j < 2; j++) {
            p->org[j] = org[j] - 16 + (Q_rand() & 31);
            p->vel[j] = crand() * 14;
        }

        p->org[2] = org[2] - 8 + (Q_rand() & 7);
        p->vel[2] = 80 + (Q_rand() & 7);

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -0.5f;
    }
}

/*
===============
CG_LogoutEffect

===============
*/
static void CG_LogoutEffect(const vec3_t org, int color)
{
    int         i, j;
    cparticle_t *p;

    for (i = 0; i < 500; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;

        p->color = color + (Q_rand() & 7);

        p->org[0] = org[0] - 16 + frand() * 32;
        p->org[1] = org[1] - 16 + frand() * 32;
        p->org[2] = org[2] - 24 + frand() * 56;

        for (j = 0; j < 3; j++)
            p->vel[j] = crand() * 20;

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (1.0f + frand() * 0.3f);
    }
}

/*
===============
CG_ItemRespawnParticles

===============
*/
void CG_ItemRespawnParticles(const vec3_t org)
{
    int         i, j;
    cparticle_t *p;

    for (i = 0; i < 64; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;

        p->color = 0xd4 + (Q_rand() & 3); // green

        p->org[0] = org[0] + crand() * 8;
        p->org[1] = org[1] + crand() * 8;
        p->org[2] = org[2] + crand() * 8;

        for (j = 0; j < 3; j++)
            p->vel[j] = crand() * 8;

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY * 0.2f;
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (1.0f + frand() * 0.3f);
    }
}

/*
===============
CG_ExplosionParticles
===============
*/
void CG_ExplosionParticles(const vec3_t org)
{
    int         i, j;
    cparticle_t *p;

    for (i = 0; i < 256; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = 0xe0 + (Q_rand() & 7);

        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((int)(Q_rand() % 32) - 16);
            p->vel[j] = (int)(Q_rand() % 384) - 192;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -0.8f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CG_BigTeleportParticles
===============
*/
void CG_BigTeleportParticles(const vec3_t org)
{
    static const byte   colortable[4] = {2 * 8, 13 * 8, 21 * 8, 18 * 8};
    int         i;
    cparticle_t *p;
    float       angle, dist;

    for (i = 0; i < 4096; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;

        p->color = colortable[Q_rand() & 3];

        angle = (Q_rand() & 1023) * (M_PIf * 2 / 1023);
        dist = Q_rand() & 31;
        p->org[0] = org[0] + cosf(angle) * dist;
        p->vel[0] = cosf(angle) * (70 + (Q_rand() & 63));
        p->accel[0] = -cosf(angle) * 100;

        p->org[1] = org[1] + sinf(angle) * dist;
        p->vel[1] = sinf(angle) * (70 + (Q_rand() & 63));
        p->accel[1] = -sinf(angle) * 100;

        p->org[2] = org[2] + 8 + (Q_rand() % 90);
        p->vel[2] = -100 + (int)(Q_rand() & 31);
        p->accel[2] = PARTICLE_GRAVITY * 4;
        p->alpha = 1.0f;

        p->alphavel = -0.3f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CG_BlasterParticles

Wall impact puffs
===============
*/
void CG_BlasterParticles(const vec3_t org, const vec3_t dir)
{
    int         i, j;
    cparticle_t *p;
    float       d;

    for (i = 0; i < 40; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = 0xe0 + (Q_rand() & 7);

        d = Q_rand() & 15;
        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((int)(Q_rand() & 7) - 4) + d * dir[j];
            p->vel[j] = dir[j] * 30 + crand() * 40;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CG_BlasterTrail

===============
*/
void CG_BlasterTrail(centity_t *ent, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    int         i, j, count;
    cparticle_t *p;
    const int   dec = 5;

    VectorSubtract(end, ent->lerp_origin, vec);
    count = VectorNormalize(vec) / dec;
    if (!count)
        return;

    VectorCopy(ent->lerp_origin, move);
    VectorScale(vec, dec, vec);

    for (i = 0; i < count; i++) {
        p = CG_AllocParticle();
        if (!p)
            break;
        VectorClear(p->accel);

        p->time = cg.time;

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.3f + frand() * 0.2f);
        p->color = 0xe0;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand();
            p->vel[j] = crand() * 5;
        }

        VectorAdd(move, vec, move);
    }

    VectorCopy(move, ent->lerp_origin);
}

/*
===============
CG_FlagTrail

===============
*/
void CG_FlagTrail(centity_t *ent, const vec3_t end, int color)
{
    vec3_t      move;
    vec3_t      vec;
    int         i, j, count;
    cparticle_t *p;
    const int   dec = 5;

    VectorSubtract(end, ent->lerp_origin, vec);
    count = VectorNormalize(vec) / dec;
    if (!count)
        return;

    VectorCopy(ent->lerp_origin, move);
    VectorScale(vec, dec, vec);

    for (i = 0; i < count; i++) {
        p = CG_AllocParticle();
        if (!p)
            break;
        VectorClear(p->accel);

        p->time = cg.time;

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.8f + frand() * 0.2f);
        p->color = color;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand() * 16;
            p->vel[j] = crand() * 5;
        }

        VectorAdd(move, vec, move);
    }

    VectorCopy(move, ent->lerp_origin);
}

/*
===============
CG_DiminishingTrail

Now combined with CG_RocketTrail().
===============
*/
void CG_DiminishingTrail(centity_t *ent, const vec3_t end, diminishing_trail_t type)
{
    static const byte  colors[DT_COUNT] = { 0xe8, 0xdb, 0x04, 0x04, 0xd8 };
    static const float alphas[DT_COUNT] = { 0.4f, 0.4f, 0.2f, 0.2f, 0.4f };
    vec3_t      move;
    vec3_t      vec;
    int         i, j, count;
    cparticle_t *p;
    const float dec = 0.5f;
    float       orgscale;
    float       velscale;

    VectorSubtract(end, ent->lerp_origin, vec);
    count = VectorNormalize(vec) / dec;
    if (!count)
        return;

    VectorCopy(ent->lerp_origin, move);
    VectorScale(vec, dec, vec);

    if (ent->trailcount > 900) {
        orgscale = 4;
        velscale = 15;
    } else if (ent->trailcount > 800) {
        orgscale = 2;
        velscale = 10;
    } else {
        orgscale = 1;
        velscale = 5;
    }

    for (i = 0; i < count; i++) {
        // drop less particles as it flies
        if ((Q_rand() & 1023) < ent->trailcount) {
            p = CG_AllocParticle();
            if (!p)
                break;

            VectorClear(p->accel);
            p->time = cg.time;

            p->alpha = 1.0f;
            p->alphavel = -1.0f / (1 + frand() * alphas[type]);

            for (j = 0; j < 3; j++) {
                p->org[j] = move[j] + crand() * orgscale;
                p->vel[j] = crand() * velscale;
            }

            if (type >= DT_ROCKET)
                p->accel[2] = 20;
            else
                p->vel[2] -= PARTICLE_GRAVITY;

            if (type == DT_FIREBALL)
                p->color = colors[type] + (1024 - ent->trailcount) / 64;
            else
                p->color = colors[type] + (Q_rand() & 7);
        }

        // rocket fire (non-diminishing)
        if (type == DT_ROCKET && (Q_rand() & 15) == 0) {
            p = CG_AllocParticle();
            if (!p)
                break;

            VectorClear(p->accel);
            p->time = cg.time;

            p->alpha = 1.0f;
            p->alphavel = -1.0f / (1 + frand() * 0.2f);
            p->color = 0xdc + (Q_rand() & 3);
            for (j = 0; j < 3; j++) {
                p->org[j] = move[j] + crand() * 5;
                p->vel[j] = crand() * 20;
            }
            p->accel[2] = -PARTICLE_GRAVITY;
        }

        ent->trailcount -= 5;
        if (ent->trailcount < 100)
            ent->trailcount = 100;
        VectorAdd(move, vec, move);
    }

    VectorCopy(move, ent->lerp_origin);
}

/*
===============
CG_RailTrail

===============
*/
void CG_OldRailTrail(const vec3_t start, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
    float       dec;
    vec3_t      right, up;
    int         i;
    float       d, c, s;
    vec3_t      dir;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    MakeNormalVectors(vec, right, up);

    for (i = 0; i < len; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        VectorClear(p->accel);

        d = i * 0.1f;
        c = cosf(d);
        s = sinf(d);

        VectorScale(right, c, dir);
        VectorMA(dir, s, up, dir);

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (1 + frand() * 0.2f);
        p->color = 0x74 + (Q_rand() & 7);
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + dir[j] * 3;
            p->vel[j] = dir[j] * 6;
        }

        VectorAdd(move, vec, move);
    }

    dec = 0.75f;
    VectorScale(vec, dec, vec);
    VectorCopy(start, move);

    while (len > 0) {
        len -= dec;

        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        VectorClear(p->accel);

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.6f + frand() * 0.2f);
        p->color = Q_rand() & 15;

        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand() * 3;
            p->vel[j] = crand() * 3;
        }

        VectorAdd(move, vec, move);
    }
}

/*
===============
CG_BubbleTrail

===============
*/
void CG_BubbleTrail(const vec3_t start, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         i, j;
    cparticle_t *p;
    float       dec;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    dec = 32;
    VectorScale(vec, dec, vec);

    for (i = 0; i < len; i += dec) {
        p = CG_AllocParticle();
        if (!p)
            return;

        VectorClear(p->accel);
        p->time = cg.time;

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (1 + frand() * 0.2f);
        p->color = 4 + (Q_rand() & 7);
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand() * 2;
            p->vel[j] = crand() * 5;
        }
        p->vel[2] += 6;

        VectorAdd(move, vec, move);
    }
}

/*
===============
CG_FlyParticles
===============
*/

#define BEAMLENGTH  16

static void CG_FlyParticles(const vec3_t origin, int count)
{
    int         i;
    cparticle_t *p;
    float       angle;
    float       sp, sy, cp, cy;
    vec3_t      forward;
    float       dist;
    float       ltime;

    if (count > NUMVERTEXNORMALS)
        count = NUMVERTEXNORMALS;

    ltime = cg.time * 0.001f;
    for (i = 0; i < count; i += 2) {
        p = CG_AllocParticle();
        if (!p)
            return;

        angle = ltime * avelocities[i][0];
        sy = sinf(angle);
        cy = cosf(angle);
        angle = ltime * avelocities[i][1];
        sp = sinf(angle);
        cp = cosf(angle);

        forward[0] = cp * cy;
        forward[1] = cp * sy;
        forward[2] = -sp;

        p->time = cg.time;

        dist = sinf(ltime + i) * 64;
        p->org[0] = origin[0] + bytedirs[i][0] * dist + forward[0] * BEAMLENGTH;
        p->org[1] = origin[1] + bytedirs[i][1] * dist + forward[1] * BEAMLENGTH;
        p->org[2] = origin[2] + bytedirs[i][2] * dist + forward[2] * BEAMLENGTH;

        VectorClear(p->vel);
        VectorClear(p->accel);

        p->color = 0;

        p->alpha = 1;
        p->alphavel = INSTANT_PARTICLE;
    }
}

void CG_FlyEffect(centity_t *ent, const vec3_t origin)
{
    int     n;
    int     count;
    int     starttime;

    if (ent->fly_stoptime < cg.time) {
        starttime = cg.time;
        ent->fly_stoptime = cg.time + 60000;
    } else {
        starttime = ent->fly_stoptime - 60000;
    }

    n = cg.time - starttime;
    if (n < 20000)
        count = n * NUMVERTEXNORMALS / 20000;
    else {
        n = ent->fly_stoptime - cg.time;
        if (n < 20000)
            count = n * NUMVERTEXNORMALS / 20000;
        else
            count = NUMVERTEXNORMALS;
    }

    CG_FlyParticles(origin, count);
}

/*
===============
CG_BfgParticles
===============
*/
void CG_BfgParticles(const entity_t *ent)
{
    int         i;
    cparticle_t *p;
    float       angle;
    float       sp, sy, cp, cy;
    vec3_t      forward;
    float       dist;
    float       ltime;

    ltime = cg.time * 0.001f;
    for (i = 0; i < NUMVERTEXNORMALS; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        angle = ltime * avelocities[i][0];
        sy = sinf(angle);
        cy = cosf(angle);
        angle = ltime * avelocities[i][1];
        sp = sinf(angle);
        cp = cosf(angle);

        forward[0] = cp * cy;
        forward[1] = cp * sy;
        forward[2] = -sp;

        p->time = cg.time;

        dist = sinf(ltime + i) * 64;
        p->org[0] = ent->origin[0] + bytedirs[i][0] * dist + forward[0] * BEAMLENGTH;
        p->org[1] = ent->origin[1] + bytedirs[i][1] * dist + forward[1] * BEAMLENGTH;
        p->org[2] = ent->origin[2] + bytedirs[i][2] * dist + forward[2] * BEAMLENGTH;

        VectorClear(p->vel);
        VectorClear(p->accel);

        dist = Distance(p->org, ent->origin) / 90.0f;
        p->color = floorf(0xd0 + dist * 7);

        p->alpha = 1.0f - dist;
        p->alphavel = INSTANT_PARTICLE;
    }
}

/*
===============
CG_BFGExplosionParticles
===============
*/
//FIXME combined with CG_ExplosionParticles
void CG_BFGExplosionParticles(const vec3_t org)
{
    int         i, j;
    cparticle_t *p;

    for (i = 0; i < 256; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = 0xd0 + (Q_rand() & 7);

        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((int)(Q_rand() % 32) - 16);
            p->vel[j] = (int)(Q_rand() % 384) - 192;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -0.8f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CG_TeleportParticles

===============
*/
void CG_TeleportParticles(const vec3_t org)
{
    int         i, j, k;
    cparticle_t *p;
    float       vel;
    vec3_t      dir;

    for (i = -16; i <= 16; i += 4)
        for (j = -16; j <= 16; j += 4)
            for (k = -16; k <= 32; k += 4) {
                p = CG_AllocParticle();
                if (!p)
                    return;

                p->time = cg.time;
                p->color = 7 + (Q_rand() & 7);

                p->alpha = 1.0f;
                p->alphavel = -1.0f / (0.3f + (Q_rand() & 7) * 0.02f);

                p->org[0] = org[0] + i + (Q_rand() & 3);
                p->org[1] = org[1] + j + (Q_rand() & 3);
                p->org[2] = org[2] + k + (Q_rand() & 3);

                dir[0] = j * 8;
                dir[1] = i * 8;
                dir[2] = k * 8;

                VectorNormalize(dir);
                vel = 50 + (Q_rand() & 63);
                VectorScale(dir, vel, p->vel);

                p->accel[0] = p->accel[1] = 0;
                p->accel[2] = -PARTICLE_GRAVITY;
            }
}

void CG_Flashlight(int ent, const vec3_t pos)
{
    cdlight_t   *dl;

    dl = CG_AllocDlight(ent);
    VectorCopy(pos, dl->origin);
    dl->radius = 400;
    dl->die = cg.time + 100;
    VectorSet(dl->color, 1, 1, 1);
}

/*
======
CG_ColorFlash - flash of light
======
*/
void CG_ColorFlash(const vec3_t pos, int ent, int intensity, float r, float g, float b)
{
    cdlight_t   *dl;

    dl = CG_AllocDlight(ent);
    VectorCopy(pos, dl->origin);
    dl->radius = intensity;
    dl->die = cg.time + 100;
    VectorSet(dl->color, r, g, b);
}

/*
======
CG_DebugTrail
======
*/
void CG_DebugTrail(const vec3_t start, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    cparticle_t *p;
    float       dec;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    dec = 3;
    VectorScale(vec, dec, vec);
    VectorCopy(start, move);

    while (len > 0) {
        len -= dec;

        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        VectorClear(p->accel);
        VectorClear(p->vel);
        p->alpha = 1.0f;
        p->alphavel = -0.1f;
        p->color = 0x74 + (Q_rand() & 7);
        VectorCopy(move, p->org);
        VectorAdd(move, vec, move);
    }
}

void CG_ForceWall(const vec3_t start, const vec3_t end, int color)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    VectorScale(vec, 4, vec);

    // FIXME: this is a really silly way to have a loop
    while (len > 0) {
        len -= 4;

        if (frand() > 0.3f) {
            p = CG_AllocParticle();
            if (!p)
                return;
            VectorClear(p->accel);

            p->time = cg.time;

            p->alpha = 1.0f;
            p->alphavel =  -1.0f / (3.0f + frand() * 0.5f);
            p->color = color;
            for (j = 0; j < 3; j++)
                p->org[j] = move[j] + crand() * 3;
            p->vel[0] = 0;
            p->vel[1] = 0;
            p->vel[2] = -40 - (crand() * 10);
        }

        VectorAdd(move, vec, move);
    }
}

/*
===============
CG_BubbleTrail2 (lets you control the # of bubbles by setting the distance between the spawns)

===============
*/
void CG_BubbleTrail2(const vec3_t start, const vec3_t end, int dist)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         i, j;
    cparticle_t *p;
    float       dec;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    dec = dist;
    VectorScale(vec, dec, vec);

    for (i = 0; i < len; i += dec) {
        p = CG_AllocParticle();
        if (!p)
            return;

        VectorClear(p->accel);
        p->time = cg.time;

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (1 + frand() * 0.1f);
        p->color = 4 + (Q_rand() & 7);
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand() * 2;
            p->vel[j] = crand() * 10;
        }
        p->org[2] -= 4;
        p->vel[2] += 20;

        VectorAdd(move, vec, move);
    }
}

void CG_Heatbeam(const vec3_t start, const vec3_t forward)
{
    vec3_t      move;
    vec3_t      vec;
    float       len;
    int         j;
    cparticle_t *p;
    int         i;
    float       c, s;
    vec3_t      dir;
    float       ltime;
    float       step = 32.0f, rstep;
    float       start_pt;
    float       rot;
    float       variance;

    VectorCopy(start, move);
    len = VectorNormalize2(forward, vec);

    ltime = cg.time * 0.001f;
    start_pt = fmodf(ltime * 96.0f, step);
    VectorMA(move, start_pt, vec, move);

    VectorScale(vec, step, vec);

    rstep = M_PIf / 10.0f;
    for (i = start_pt; i < len; i += step) {
        for (rot = 0; rot < M_PIf * 2; rot += rstep) {
            p = CG_AllocParticle();
            if (!p)
                return;

            p->time = cg.time;
            VectorClear(p->accel);
            variance = 0.5f;
            c = cosf(rot) * variance;
            s = sinf(rot) * variance;

            // trim it so it looks like it's starting at the origin
            if (i < 10) {
                VectorScale(cg.v_right, c * (i / 10.0f), dir);
                VectorMA(dir, s * (i / 10.0f), cg.v_up, dir);
            } else {
                VectorScale(cg.v_right, c, dir);
                VectorMA(dir, s, cg.v_up, dir);
            }

            p->alpha = 0.5f;
            p->alphavel = -1000.0f;
            p->color = 223 - (Q_rand() & 7);
            for (j = 0; j < 3; j++) {
                p->org[j] = move[j] + dir[j] * 3;
                p->vel[j] = 0;
            }
        }

        VectorAdd(move, vec, move);
    }
}

/*
===============
CG_ParticleSteamEffect

Puffs with velocity along direction, with some randomness thrown in
===============
*/
void CG_ParticleSteamEffect(const vec3_t org, const vec3_t dir, int color, int count, int magnitude)
{
    int         i, j;
    cparticle_t *p;
    float       d;
    vec3_t      r, u;

    MakeNormalVectors(dir, r, u);

    for (i = 0; i < count; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = color + (Q_rand() & 7);

        for (j = 0; j < 3; j++)
            p->org[j] = org[j] + magnitude * 0.1f * crand();

        VectorScale(dir, magnitude, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, r, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, u, p->vel);

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY / 2;
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CG_TrackerTrail
===============
*/
void CG_TrackerTrail(centity_t *ent, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    vec3_t      forward, up, angle_dir;
    int         i, count, sign;
    cparticle_t *p;
    const int   dec = 3;
    float       dist;

    VectorSubtract(end, ent->lerp_origin, vec);
    count = VectorNormalize(vec) / dec;
    if (!count)
        return;

    VectorCopy(vec, forward);
    vectoangles(forward, angle_dir);
    AngleVectors(angle_dir, NULL, NULL, up);

    VectorCopy(ent->lerp_origin, move);
    VectorScale(vec, dec, vec);

    sign = ent->trailcount;
    for (i = 0; i < count; i++) {
        p = CG_AllocParticle();
        if (!p)
            break;
        VectorClear(p->accel);

        p->time = cg.time;

        p->alpha = 1.0f;
        p->alphavel = -2.0f;
        p->color = 0;
        dist = 8 * cosf(DotProduct(move, forward) * M_PIf / 64);
        if (sign & 1)
            dist = -dist;
        VectorMA(move, dist, up, p->org);
        VectorSet(p->vel, 0, 0, 5);

        VectorAdd(move, vec, move);
        sign ^= 1;
    }

    ent->trailcount = sign;
    VectorCopy(move, ent->lerp_origin);
}

// Marsaglia 1972 rejection method
static void RandomDir(vec3_t dir)
{
    float x, y, s, a;

    do {
        x = crand();
        y = crand();
        s = x * x + y * y;
    } while (s > 1);

    a = 2 * sqrtf(1 - s);
    dir[0] = x * a;
    dir[1] = y * a;
    dir[2] = -1 + 2 * s;
}

void CG_Tracker_Shell(const centity_t *ent, const vec3_t origin)
{
    vec3_t          org, dir, mid;
    int             i, count;
    cparticle_t     *p;
    float           radius, scale;

    VectorAvg(ent->mins, ent->maxs, mid);
    VectorAdd(origin, mid, org);
    radius = ent->radius;
    scale = Q_clipf(ent->radius / 40.0f, 1, 2);
    count = 300 * scale;

    for (i = 0; i < count; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cg.time;

        p->alpha = 1.0f;
        p->alphavel = INSTANT_PARTICLE;
        p->color = 0;
        p->scale = scale;

        RandomDir(dir);
        VectorMA(org, radius, dir, p->org);
    }
}

void CG_MonsterPlasma_Shell(const vec3_t origin)
{
    vec3_t          dir;
    int             i;
    cparticle_t     *p;

    for (i = 0; i < 40; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cg.time;

        p->alpha = 1.0f;
        p->alphavel = INSTANT_PARTICLE;
        p->color = 0xe0;

        RandomDir(dir);
        VectorMA(origin, 10, dir, p->org);
    }
}

void CG_Widowbeamout(cg_sustain_t *self)
{
    static const byte   colortable[4] = {2 * 8, 13 * 8, 21 * 8, 18 * 8};
    vec3_t          dir;
    int             i;
    cparticle_t     *p;
    float           ratio;

    ratio = 1.0f - (self->endtime - cg.time) / 2100.0f;

    for (i = 0; i < 300; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cg.time;

        p->alpha = 1.0f;
        p->alphavel = INSTANT_PARTICLE;
        p->color = colortable[Q_rand() & 3];

        RandomDir(dir);
        VectorMA(self->org, (45.0f * ratio), dir, p->org);
    }
}

void CG_Nukeblast(cg_sustain_t *self)
{
    static const byte   colortable[4] = {110, 112, 114, 116};
    vec3_t          dir;
    int             i;
    cparticle_t     *p;
    float           ratio;

    ratio = 1.0f - (self->endtime - cg.time) / 1000.0f;

    for (i = 0; i < 700; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cg.time;

        p->alpha = 1.0f;
        p->alphavel = INSTANT_PARTICLE;
        p->color = colortable[Q_rand() & 3];

        RandomDir(dir);
        VectorMA(self->org, (200.0f * ratio), dir, p->org);
    }
}

void CG_WidowSplash(const vec3_t pos)
{
    static const byte   colortable[4] = {2 * 8, 13 * 8, 21 * 8, 18 * 8};
    int         i;
    cparticle_t *p;
    vec3_t      dir;

    for (i = 0; i < 256; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = colortable[Q_rand() & 3];

        RandomDir(dir);
        VectorMA(pos, 45.0f, dir, p->org);
        VectorScale(dir, 40.0f, p->vel);

        VectorClear(p->accel);
        p->alpha = 1.0f;

        p->alphavel = -0.8f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CG_TagTrail

===============
*/
void CG_TagTrail(centity_t *ent, const vec3_t end, int color)
{
    vec3_t      move;
    vec3_t      vec;
    int         i, j, count;
    cparticle_t *p;
    const int   dec = 5;

    VectorSubtract(end, ent->lerp_origin, vec);
    count = VectorNormalize(vec) / dec;
    if (!count)
        return;

    VectorCopy(ent->lerp_origin, move);
    VectorScale(vec, dec, vec);

    for (i = 0; i < count; i++) {
        p = CG_AllocParticle();
        if (!p)
            break;
        VectorClear(p->accel);

        p->time = cg.time;

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.8f + frand() * 0.2f);
        p->color = color;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand() * 16;
            p->vel[j] = crand() * 5;
        }

        VectorAdd(move, vec, move);
    }

    VectorCopy(move, ent->lerp_origin);
}

/*
===============
CG_ColorExplosionParticles
===============
*/
void CG_ColorExplosionParticles(const vec3_t org, int color, int run)
{
    int         i, j;
    cparticle_t *p;

    for (i = 0; i < 128; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = color + (Q_rand() % run);

        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((int)(Q_rand() % 32) - 16);
            p->vel[j] = (int)(Q_rand() % 256) - 128;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -0.4f / (0.6f + frand() * 0.2f);
    }
}

/*
===============
CG_ParticleSmokeEffect - like the steam effect, but unaffected by gravity
===============
*/
void CG_ParticleSmokeEffect(const vec3_t org, const vec3_t dir, int color, int count, int magnitude)
{
    int         i, j;
    cparticle_t *p;
    float       d;
    vec3_t      r, u;

    MakeNormalVectors(dir, r, u);

    for (i = 0; i < count; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = color + (Q_rand() & 7);

        for (j = 0; j < 3; j++)
            p->org[j] = org[j] + magnitude * 0.1f * crand();

        VectorScale(dir, magnitude, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, r, p->vel);
        d = crand() * magnitude / 3;
        VectorMA(p->vel, d, u, p->vel);

        VectorClear(p->accel);
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CG_BlasterParticles2

Wall impact puffs (Green)
===============
*/
void CG_BlasterParticles2(const vec3_t org, const vec3_t dir, unsigned int color)
{
    int         i, j;
    cparticle_t *p;
    float       d;
    int         count;

    count = 40;
    for (i = 0; i < count; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = color + (Q_rand() & 7);

        d = Q_rand() & 15;
        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((int)(Q_rand() & 7) - 4) + d * dir[j];
            p->vel[j] = dir[j] * 30 + crand() * 40;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = -PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CG_BlasterTrail2

Green!
===============
*/
void CG_BlasterTrail2(centity_t *ent, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    int         i, j, count;
    cparticle_t *p;
    const int   dec = 5;

    VectorSubtract(end, ent->lerp_origin, vec);
    count = VectorNormalize(vec) / dec;
    if (!count)
        return;

    VectorCopy(ent->lerp_origin, move);
    VectorScale(vec, dec, vec);

    for (i = 0; i < count; i++) {
        p = CG_AllocParticle();
        if (!p)
            break;
        VectorClear(p->accel);

        p->time = cg.time;

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.3f + frand() * 0.2f);
        p->color = 0xd0;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand();
            p->vel[j] = crand() * 5;
        }

        VectorAdd(move, vec, move);
    }

    VectorCopy(move, ent->lerp_origin);
}

/*
===============
CG_IonripperTrail
===============
*/
void CG_IonripperTrail(centity_t *ent, const vec3_t end)
{
    vec3_t      move;
    vec3_t      vec;
    cparticle_t *p;
    const int   dec = 5;
    int         i, count, sign;

    VectorSubtract(end, ent->lerp_origin, vec);
    count = VectorNormalize(vec) / dec;
    if (!count)
        return;

    VectorCopy(ent->lerp_origin, move);
    VectorScale(vec, dec, vec);

    sign = ent->trailcount;
    for (i = 0; i < count; i++) {
        p = CG_AllocParticle();
        if (!p)
            break;
        VectorClear(p->accel);

        p->time = cg.time;
        p->alpha = 0.5f;
        p->alphavel = -1.0f / (0.3f + frand() * 0.2f);
        p->color = 0xe4 + (Q_rand() & 3);

        VectorCopy(move, p->org);

        p->vel[0] = (sign & 1) ? 10 : -10;
        p->vel[1] = 0;
        p->vel[2] = 0;

        VectorAdd(move, vec, move);
        sign ^= 1;
    }

    ent->trailcount = sign;
    VectorCopy(move, ent->lerp_origin);
}

/*
===============
CG_TrapParticles
===============
*/
void CG_TrapParticles(centity_t *ent, const vec3_t origin)
{
    vec3_t      move;
    vec3_t      vec;
    vec3_t      start, end;
    float       len;
    int         j;
    cparticle_t *p;
    int         dec;

    if (cg.time - ent->fly_stoptime < 10)
        return;
    ent->fly_stoptime = cg.time;

    VectorCopy(origin, start);
    VectorCopy(origin, end);
    start[2] -= 14;
    end[2] += 50;

    VectorCopy(start, move);
    VectorSubtract(end, start, vec);
    len = VectorNormalize(vec);

    dec = 5;
    VectorScale(vec, 5, vec);

    // FIXME: this is a really silly way to have a loop
    while (len > 0) {
        len -= dec;

        p = CG_AllocParticle();
        if (!p)
            return;
        VectorClear(p->accel);

        p->time = cg.time;

        p->alpha = 1.0f;
        p->alphavel = -1.0f / (0.3f + frand() * 0.2f);
        p->color = 0xe0;
        for (j = 0; j < 3; j++) {
            p->org[j] = move[j] + crand();
            p->vel[j] = crand() * 15;
        }
        p->accel[2] = PARTICLE_GRAVITY;

        VectorAdd(move, vec, move);
    }

    {
        int         i, j, k;
        cparticle_t *p;
        float       vel;
        vec3_t      dir;
        vec3_t      org;

        VectorCopy(origin, org);

        for (i = -2; i <= 2; i += 4)
            for (j = -2; j <= 2; j += 4)
                for (k = -2; k <= 4; k += 4) {
                    p = CG_AllocParticle();
                    if (!p)
                        return;

                    p->time = cg.time;
                    p->color = 0xe0 + (Q_rand() & 3);

                    p->alpha = 1.0f;
                    p->alphavel = -1.0f / (0.3f + (Q_rand() & 7) * 0.02f);

                    p->org[0] = org[0] + i + ((Q_rand() & 23) * crand());
                    p->org[1] = org[1] + j + ((Q_rand() & 23) * crand());
                    p->org[2] = org[2] + k + ((Q_rand() & 23) * crand());

                    dir[0] = j * 8;
                    dir[1] = i * 8;
                    dir[2] = k * 8;

                    VectorNormalize(dir);
                    vel = 50 + (Q_rand() & 63);
                    VectorScale(dir, vel, p->vel);

                    p->accel[0] = p->accel[1] = 0;
                    p->accel[2] = -PARTICLE_GRAVITY;
                }
    }
}

/*
===============
CG_ParticleEffect3
===============
*/
void CG_ParticleEffect3(const vec3_t org, const vec3_t dir, int color, int count)
{
    int         i, j;
    cparticle_t *p;
    float       d;

    for (i = 0; i < count; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = color;

        d = Q_rand() & 7;
        for (j = 0; j < 3; j++) {
            p->org[j] = org[j] + ((int)(Q_rand() & 7) - 4) + d * dir[j];
            p->vel[j] = crand() * 20;
        }

        p->accel[0] = p->accel[1] = 0;
        p->accel[2] = PARTICLE_GRAVITY;
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CG_BerserkSlamParticles
===============
*/
void CG_BerserkSlamParticles(const vec3_t org, const vec3_t dir)
{
    static const byte   colortable[4] = {110, 112, 114, 116};
    int         i;
    cparticle_t *p;
    float       d;
    vec3_t      r, u;

    MakeNormalVectors(dir, r, u);

    for (i = 0; i < 700; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = colortable[Q_rand() & 3];

        VectorCopy(org, p->org);

        d = frand() * 192;
        VectorScale(dir, d, p->vel);
        d = crand() * 192;
        VectorMA(p->vel, d, r, p->vel);
        d = crand() * 192;
        VectorMA(p->vel, d, u, p->vel);

        VectorClear(p->accel);
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CG_PowerSplash

TODO: differentiate screen/shield
===============
*/
void CG_PowerSplash(const centity_t *cent)
{
    static const byte   colortable[4] = {208, 209, 210, 211};
    int         i;
    cparticle_t *p;
    vec3_t      org, dir, mid;

    VectorAvg(cent->mins, cent->maxs, mid);
    VectorAdd(cent->current.origin, mid, org);

    for (i = 0; i < 256; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = colortable[Q_rand() & 3];

        RandomDir(dir);
        VectorMA(org, cent->radius, dir, p->org);
        VectorScale(dir, 40.0f, p->vel);

        VectorClear(p->accel);
        p->alpha = 1.0f;

        p->alphavel = -1.0f / (0.5f + frand() * 0.3f);
    }
}

/*
===============
CG_TeleporterParticles
===============
*/
void CG_TeleporterParticles2(const vec3_t org)
{
    int         i;
    cparticle_t *p;
    vec3_t      dir;

    for (i = 0; i < 8; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = 0xdb;

        RandomDir(dir);
        VectorMA(org, 30.0f, dir, p->org);
        p->org[2] += 20.0f;
        VectorScale(dir, -25.0f, p->vel);

        VectorClear(p->accel);
        p->alpha = 1.0f;

        p->alphavel = -0.8f;
    }
}

/*
===============
CG_HologramParticles
===============
*/
void CG_HologramParticles(const vec3_t org)
{
    int         i;
    cparticle_t *p;
    vec3_t      dir;
    float       ltime;
    vec3_t      axis[3];

    ltime = cg.time * 0.03f;
    VectorSet(dir, ltime, ltime, 0);
    AnglesToAxis(dir, axis);

    for (i = 0; i < NUMVERTEXNORMALS; i++) {
        p = CG_AllocParticle();
        if (!p)
            return;

        p->time = cg.time;
        p->color = 0xd0;

        VectorRotate(bytedirs[i], axis, dir);
        VectorMA(org, 100.0f, dir, p->org);

        VectorClear(p->vel);
        VectorClear(p->accel);

        p->alpha = 1.0f;
        p->alphavel = INSTANT_PARTICLE;
    }
}

/*
===============
CG_BarrelExplodingParticles
===============
*/
void CG_BarrelExplodingParticles(const vec3_t org)
{
    static const vec3_t ofs[6] = {
        { -10, 0, 40 },
        { 10, 0, 40 },
        { 0, 16, 30 },
        { 16, 0, 25 },
        { 0, -16, 20 },
        { -16, 0, 15 },
    };

    static const vec3_t dir[6] = {
        { 0, 0, 1 },
        { 0, 0, 1 },
        { 0, 1, 0 },
        { 1, 0, 0 },
        { 0, -1, 0 },
        { -1, 0, 0 },
    };

    static const byte color[4] = { 52, 64, 96, 112 };

    for (int i = 0; i < 6; i++) {
        vec3_t p;
        VectorAdd(org, ofs[i], p);
        for (int j = 0; j < 4; j++)
            CG_ParticleSmokeEffect(p, dir[i], color[j], 5, 40);
    }
}

static particle_t   r_particles[MAX_PARTICLES];

/*
===============
CG_AddParticles
===============
*/
void CG_AddParticles(void)
{
    cparticle_t     *p, *next;
    float           alpha;
    float           time, time2;
    cparticle_t     *active, *tail;
    particle_t      *part;
    int r_numparticles = 0;

    active = NULL;
    tail = NULL;

    for (p = active_particles; p; p = next) {
        next = p->next;

        if (p->alphavel != INSTANT_PARTICLE) {
            time = (cg.time - p->time) * 0.001f;
            alpha = p->alpha + time * p->alphavel;
            if (alpha <= 0) {
                // faded out
                p->next = free_particles;
                free_particles = p;
                continue;
            }
        } else {
            time = 0.0f;
            alpha = p->alpha;
        }

        if (r_numparticles >= MAX_PARTICLES)
            break;
        part = &r_particles[r_numparticles++];

        p->next = NULL;
        if (!tail)
            active = tail = p;
        else {
            tail->next = p;
            tail = p;
        }

        time2 = time * time;

        part->origin[0] = p->org[0] + p->vel[0] * time + p->accel[0] * time2;
        part->origin[1] = p->org[1] + p->vel[1] * time + p->accel[1] * time2;
        part->origin[2] = p->org[2] + p->vel[2] * time + p->accel[2] * time2;

        part->rgba = p->rgba;
        part->color = p->color;
        part->alpha = min(alpha, 1.0f);
        part->scale = p->scale;

        if (p->alphavel == INSTANT_PARTICLE) {
            p->alphavel = 0.0f;
            p->alpha = 0.0f;
        }
    }

    trap_R_LocateParticles(r_particles, r_numparticles);

    active_particles = active;
}

/*
==============
CG_ClearEffects

==============
*/
void CG_ClearEffects(void)
{
    CG_ClearParticles();
    CG_ClearDlights();
}

void CG_InitEffects(void)
{
    CG_ClearParticles();

    for (int i = 0; i < NUMVERTEXNORMALS; i++) {
        uint32_t r = Q_rand();
        avelocities[i][0] = (r & 255) * 0.01f;
        avelocities[i][1] = (r >> 8 & 255) * 0.01f;
        avelocities[i][2] = (r >> 16 & 255) * 0.01f;
    }
}
