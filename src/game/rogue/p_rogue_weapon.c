// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"
#include "m_player.h"

static void weapon_prox_fire(edict_t *ent)
{
    // Paril: kill sideways angle on grenades
    // limit upwards angle so you don't fire behind you
    vec3_t angles;
    P_GetThrowAngles(ent, angles);

    vec3_t start, dir;
    P_ProjectSource(ent, angles, (const vec3_t) { 8, 0, -8 }, start, dir, false);

    fire_prox(ent, start, dir, damage_multiplier, 600);

    G_AddEvent(ent, EV_MUZZLEFLASH, MZ_PROX | is_silenced);

    PlayerNoise(ent, start, PNOISE_WEAPON);

    G_RemoveAmmo(ent);
}

void Weapon_ProxLauncher(edict_t *ent)
{
    static const int pause_frames[] = { 34, 51, 59, 0 };
    static const int fire_frames[] = { 6, 0 };

    Weapon_Generic(ent, 5, 16, 59, 64, pause_frames, fire_frames, weapon_prox_fire);
}

static void weapon_tesla_fire(edict_t *ent, bool held)
{
    int speed;

    // Paril: kill sideways angle on grenades
    // limit upwards angle so you don't throw behind you
    vec3_t angles;
    P_GetThrowAngles(ent, angles);

    vec3_t start, dir;
    P_ProjectSource(ent, angles, (const vec3_t) { 0, 0, -22 }, start, dir, false);

    if (ent->health > 0) {
        float frac = 1.0f - TO_SEC(ent->client->grenade_time - level.time) / GRENADE_TIMER_SEC;
        speed = lerp(GRENADE_MINSPEED, GRENADE_MAXSPEED, min(frac, 1.0f));
    } else
        speed = GRENADE_MINSPEED;

    ent->client->grenade_time = 0;

    fire_tesla(ent, start, dir, damage_multiplier, speed);

    G_RemoveAmmoEx(ent, 1);
}

void Weapon_Tesla(edict_t *ent)
{
    static const int pause_frames[] = { 21, 0 };

    Throw_Generic(ent, 8, 32, -1, NULL, 1, 2, pause_frames, false, NULL, weapon_tesla_fire, false);
}

//======================================================================
// ROGUE MODS BELOW
//======================================================================

//
// CHAINFIST
//
#define CHAINFIST_REACH 24

static void weapon_chainfist_fire(edict_t *ent)
{
    if (!(ent->client->buttons & BUTTON_ATTACK)) {
        if (ent->client->ps.gunframe == 13 ||
            ent->client->ps.gunframe == 23 ||
            ent->client->ps.gunframe >= 32) {
            ent->client->ps.gunframe = 33;
            return;
        }
    }

    int damage = 7;

    if (deathmatch.integer)
        damage = 15;

    if (is_quad)
        damage *= damage_multiplier;

    // set start point
    vec3_t start, dir;

    P_ProjectSource(ent, ent->client->v_angle, (const vec3_t) { 0, 0, -4 }, start, dir, false);

    if (fire_player_melee(ent, start, dir, CHAINFIST_REACH, damage, 100, (mod_t) { MOD_CHAINFIST })) {
        if (ent->client->empty_click_sound < level.time) {
            ent->client->empty_click_sound = level.time + SEC(0.5f);
            G_StartSound(ent, CHAN_WEAPON, G_SoundIndex("weapons/sawslice.wav"), 1, ATTN_NORM);
        }
    }

    PlayerNoise(ent, start, PNOISE_WEAPON);

    ent->client->ps.gunframe++;

    if (ent->client->buttons & BUTTON_ATTACK) {
        if (ent->client->ps.gunframe == 12)
            ent->client->ps.gunframe = 14;
        else if (ent->client->ps.gunframe == 22)
            ent->client->ps.gunframe = 24;
        else if (ent->client->ps.gunframe >= 32)
            ent->client->ps.gunframe = 7;
    }

    // start the animation
    if (ent->client->anim_priority != ANIM_ATTACK || frandom() < 0.25f) {
        ent->client->anim_priority = ANIM_ATTACK;
        if (ent->client->ps.pm_flags & PMF_DUCKED) {
            ent->s.frame = FRAME_crattak1 - 1;
            ent->client->anim_end = FRAME_crattak9;
        } else {
            ent->s.frame = FRAME_attack1 - 1;
            ent->client->anim_end = FRAME_attack8;
        }
        ent->client->anim_time = 0;
    }
}

// this spits out some smoke from the motor. it's a two-stroke, you know.
static void chainfist_smoke(edict_t *ent)
{
    vec3_t start, dir;
    P_ProjectSource(ent, ent->client->v_angle, (const vec3_t) { 8, 8, -4 }, start, dir, false);

    edict_t *te = G_TempEntity(start, EV_CHAINFIST_SMOKE, 0);
    te->r.svflags |= SVF_CLIENTMASK;
    memset(te->r.clientmask, 255, sizeof(te->r.clientmask));
    Q_ClearBit(te->r.clientmask, ent->s.number);
}

void Weapon_ChainFist(edict_t *ent)
{
    static const int pause_frames[] = { 0 };

    Weapon_Repeating(ent, 4, 32, 57, 60, pause_frames, weapon_chainfist_fire);

    // smoke on idle sequence
    if (ent->client->ps.gunframe == 42 && irandom1(8)) {
        if ((ent->client->pers.hand != CENTER_HANDED) && frandom() < 0.4f)
            chainfist_smoke(ent);
    } else if (ent->client->ps.gunframe == 51 && irandom1(8)) {
        if ((ent->client->pers.hand != CENTER_HANDED) && frandom() < 0.4f)
            chainfist_smoke(ent);
    }

    // set the appropriate weapon sound.
    if (ent->client->weaponstate == WEAPON_FIRING)
        ent->client->weapon_sound = G_SoundIndex("weapons/sawhit.wav");
    else if (ent->client->weaponstate == WEAPON_DROPPING)
        ent->client->weapon_sound = 0;
    else if (ent->client->pers.weapon->id == IT_WEAPON_CHAINFIST)
        ent->client->weapon_sound = G_SoundIndex("weapons/sawidle.wav");
}

//
// Disintegrator
//

static void weapon_tracker_fire(edict_t *self)
{
    vec3_t   end;
    edict_t *enemy = NULL, *hit;
    trace_t  tr;
    int      damage;
    contents_t mask = MASK_PROJECTILE;

    // [Paril-KEX]
    if (!G_ShouldPlayersCollide(true))
        mask &= ~CONTENTS_PLAYER;

    // PMM - felt a little high at 25
    if (deathmatch.integer)
        damage = 45;
    else
        damage = 135;

    if (is_quad)
        damage *= damage_multiplier; // pgm

    vec3_t start, dir;
    P_ProjectSource(self, self->client->v_angle, (const vec3_t) { 24, 8, -8 }, start, dir, false);

    VectorMA(start, 8192, dir, end);

    // PMM - doing two traces .. one point and one box.
    trap_Trace(&tr, start, NULL, NULL, end, self->s.number, mask);
    if (tr.entnum == ENTITYNUM_WORLD)
        trap_Trace(&tr, start, (const vec3_t) { -16, -16, -16 }, (const vec3_t) { 16, 16, 16 }, end, self->s.number, mask);

    hit = &g_edicts[tr.entnum];
    if (hit != world && ((hit->r.svflags & SVF_MONSTER) || hit->client || (hit->flags & FL_DAMAGEABLE)) && hit->health > 0)
        enemy = hit;

    fire_tracker(self, start, dir, damage, 1000, enemy);

    // send muzzle flash
    G_AddEvent(self, EV_MUZZLEFLASH, MZ_TRACKER | is_silenced);

    PlayerNoise(self, start, PNOISE_WEAPON);

    G_RemoveAmmo(self);
}

void Weapon_Disintegrator(edict_t *ent)
{
    static const int pause_frames[] = { 14, 19, 23, 0 };
    static const int fire_frames[] = { 5, 0 };

    Weapon_Generic(ent, 4, 9, 29, 34, pause_frames, fire_frames, weapon_tracker_fire);
}

/*
======================================================================

ETF RIFLE

======================================================================
*/
static void weapon_etf_rifle_fire(edict_t *ent)
{
    int    damage;
    int    kick = 3;
    int    i;
    vec3_t offset;

    if (deathmatch.integer)
        damage = 10;
    else
        damage = 10;

    if (!(ent->client->buttons & BUTTON_ATTACK)) {
        ent->client->ps.gunframe = 8;
        return;
    }

    if (ent->client->ps.gunframe == 6)
        ent->client->ps.gunframe = 7;
    else
        ent->client->ps.gunframe = 6;

    // PGM - adjusted to use the quantity entry in the weapon structure.
    if (ent->client->pers.inventory[ent->client->pers.weapon->ammo] < ent->client->pers.weapon->quantity) {
        ent->client->ps.gunframe = 8;
        NoAmmoWeaponChange(ent, true);
        return;
    }

    if (is_quad) {
        damage *= damage_multiplier;
        kick *= damage_multiplier;
    }

    // get start / end positions
    if (ent->client->ps.gunframe == 6)
        VectorSet(offset, 15, 8, -8);
    else
        VectorSet(offset, 15, 6, -8);

    vec3_t start, dir, angles;
    for (i = 0; i < 3; i++)
        angles[i] = ent->client->v_angle[i] + crandom() * 0.85f;
    P_ProjectSource(ent, angles, offset, start, dir, false);
    fire_flechette(ent, start, dir, damage, 1150, kick);
    Weapon_PowerupSound(ent);

    // send muzzle flash
    G_AddEvent(ent, EV_MUZZLEFLASH, (ent->client->ps.gunframe == 6 ? MZ_ETF_RIFLE : MZ_ETF_RIFLE_2) | is_silenced);

    PlayerNoise(ent, start, PNOISE_WEAPON);

    G_RemoveAmmo(ent);

    ent->client->anim_priority = ANIM_ATTACK;
    if (ent->client->ps.pm_flags & PMF_DUCKED) {
        ent->s.frame = FRAME_crattak1 - (int)(frandom() + 0.25f);
        ent->client->anim_end = FRAME_crattak9;
    } else {
        ent->s.frame = FRAME_attack1 - (int)(frandom() + 0.25f);
        ent->client->anim_end = FRAME_attack8;
    }
    ent->client->anim_time = 0;
}

void Weapon_ETF_Rifle(edict_t *ent)
{
    static const int pause_frames[] = { 18, 28, 0 };

    Weapon_Repeating(ent, 4, 7, 37, 41, pause_frames, weapon_etf_rifle_fire);
}

#define HEATBEAM_DM_DMG 15
#define HEATBEAM_SP_DMG 15

static void Heatbeam_Fire(edict_t *ent)
{
    bool firing = (ent->client->buttons & BUTTON_ATTACK);
    bool has_ammo = ent->client->pers.inventory[ent->client->pers.weapon->ammo] >= ent->client->pers.weapon->quantity;

    if (!firing || !has_ammo) {
        ent->client->ps.gunframe = 13;
        ent->client->weapon_sound = 0;
        ent->client->ps.gunskin = 0; // normal skin

        if (firing && !has_ammo)
            NoAmmoWeaponChange(ent, true);
        return;
    }

    // start on frame 8
    if (ent->client->ps.gunframe > 12)
        ent->client->ps.gunframe = 8;
    else
        ent->client->ps.gunframe++;

    if (ent->client->ps.gunframe == 12)
        ent->client->ps.gunframe = 8;

    // play weapon sound for firing
    ent->client->weapon_sound = G_SoundIndex("weapons/bfg__l1a.wav");
    ent->client->ps.gunskin = 1; // alternate skin

    int damage;
    int kick;

    // for comparison, the hyperblaster is 15/20
    // jim requested more damage, so try 15/15 --- PGM 07/23/98
    if (deathmatch.integer)
        damage = HEATBEAM_DM_DMG;
    else
        damage = HEATBEAM_SP_DMG;

    if (deathmatch.integer) // really knock 'em around in deathmatch
        kick = 75;
    else
        kick = 30;

    if (is_quad) {
        damage *= damage_multiplier;
        kick *= damage_multiplier;
    }

    // This offset is the "view" offset for the beam start (used by trace)
    vec3_t start, dir;
    P_ProjectSource(ent, ent->client->v_angle, (const vec3_t) { 7, 2, -3 }, start, dir, false);

    // This offset is the entity offset
    fire_heatbeam(ent, start, dir, (const vec3_t) { 2, 7, -3 }, damage, kick, false);
    Weapon_PowerupSound(ent);

    // send muzzle flash
    G_AddEvent(ent, EV_MUZZLEFLASH, MZ_HEATBEAM | is_silenced);

    PlayerNoise(ent, start, PNOISE_WEAPON);

    G_RemoveAmmo(ent);

    ent->client->anim_priority = ANIM_ATTACK;
    if (ent->client->ps.pm_flags & PMF_DUCKED) {
        ent->s.frame = FRAME_crattak1 - (int)(frandom() + 0.25f);
        ent->client->anim_end = FRAME_crattak9;
    } else {
        ent->s.frame = FRAME_attack1 - (int)(frandom() + 0.25f);
        ent->client->anim_end = FRAME_attack8;
    }
    ent->client->anim_time = 0;
}

void Weapon_Heatbeam(edict_t *ent)
{
    static const int pause_frames[] = { 35, 0 };

    Weapon_Repeating(ent, 8, 12, 42, 47, pause_frames, Heatbeam_Fire);
}
