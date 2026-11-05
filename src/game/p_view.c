// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"
#include "m_player.h"

/*
===============
SV_CalcRoll

===============
*/
static float SV_CalcRoll(float side)
{
    float sign;
    float value;

    sign = side < 0 ? -1 : 1;
    side = fabsf(side);

    value = sv_rollangle.value;

    if (side < sv_rollspeed.value)
        side = side * value / sv_rollspeed.value;
    else
        side = value;

    return side * sign;
}

/*
===============
P_DamageFeedback

Handles color blends and view kicks
===============
*/
static void P_DamageFeedback(edict_t *player)
{
    gclient_t   *client;
    float        realcount, count;
    vec3_t       v;
    static const vec3_t armor_color = { 1.0f, 1.0f, 1.0f };
    static const vec3_t power_color = { 0.0f, 1.0f, 0.0f };
    static const vec3_t bcolor = { 1.0f, 0.0f, 0.0f };

    client = player->client;

    // flash the backgrounds behind the status numbers
    int want_flashes = 0;

    if (client->damage_blood)
        want_flashes |= 1;
    if (client->damage_armor && !(player->flags & FL_GODMODE) && (client->invincible_time <= level.time))
        want_flashes |= 2;

    if (want_flashes) {
        client->flash_time = level.time + BASE_FRAMETIME;
        client->ps.stats[STAT_FLASHES] = want_flashes;
    } else if (client->flash_time < level.time)
        client->ps.stats[STAT_FLASHES] = 0;

    client->ps.damage_kick = 0;
    memset(client->ps.damage_indicators, 0, sizeof(client->ps.damage_indicators));

    // total points of damage shot at the player this frame
    count = client->damage_blood + client->damage_armor + client->damage_parmor;
    if (count == 0)
        return; // didn't take any damage

    // start a pain animation if still in the player model
    if (client->anim_priority < ANIM_PAIN && player->s.modelindex == MODELINDEX_PLAYER) {
        static int i;

        client->anim_priority = ANIM_PAIN;
        if (client->ps.pm_flags & PMF_DUCKED) {
            player->s.frame = FRAME_crpain1 - 1;
            client->anim_end = FRAME_crpain4;
        } else {
            i = (i + 1) % 3;
            switch (i) {
            case 0:
                player->s.frame = FRAME_pain101 - 1;
                client->anim_end = FRAME_pain104;
                break;
            case 1:
                player->s.frame = FRAME_pain201 - 1;
                client->anim_end = FRAME_pain204;
                break;
            case 2:
                player->s.frame = FRAME_pain301 - 1;
                client->anim_end = FRAME_pain304;
                break;
            }
        }

        client->anim_time = 0;
    }

    realcount = count;

    // if we took health damage, do a minimum clamp
    if (client->damage_blood) {
        if (count < 10)
            count = 10; // always make a visible effect
    } else {
        if (count > 2)
            count = 2; // don't go too deep
    }

    // play an appropriate pain sound
    if ((level.time > player->pain_debounce_time) && !(player->flags & FL_GODMODE) && (client->invincible_time <= level.time)) {
        player->pain_debounce_time = level.time + Time_FromSec(0.7f);

        G_AddEvent(player, EV_PAIN, player->health);
        // Paril: pain noises alert monsters
        PlayerNoise(player, player->s.origin, PNOISE_SELF);
    }

    // the total alpha of the blend is always proportional to count
    if (client->damage_alpha < 0)
        client->damage_alpha = 0;

    // [Paril-KEX] tweak the values to rely less on this
    // and more on damage indicators
    if (client->damage_blood || (client->damage_alpha + count * 0.06f) < 0.15f) {
        client->damage_alpha += count * 0.06f;
        client->damage_alpha = Q_clipf(client->damage_alpha, 0.06f, 0.4f); // don't go too saturated
    }

    // mix in colors
    v = vec3_origin;

    if (client->damage_parmor)
        v = Vec3_MA(v, client->damage_parmor / realcount, power_color);
    if (client->damage_armor)
        v = Vec3_MA(v, client->damage_armor / realcount, armor_color);
    if (client->damage_blood)
        v = Vec3_MA(v, max(15.0f, client->damage_blood / realcount), bcolor);
    client->damage_blend = Vec3_Normalize(v);

    //
    // calculate view angle kicks
    //
    int kick = abs(client->damage_knockback);
    if (kick && player->health > 0) { // kick of 0 means no view adjust at all
        kick = kick * 100 / player->health;

        if (kick < count * 0.5f)
            kick = count * 0.5f;
        if (kick > 50)
            kick = 50;

        v = Vec3_Direction(client->damage_from, player->s.origin);
        client->ps.damage_dir = DirToByte(v);
        client->ps.damage_kick = kick;
    }

    //
    // encode damage indicators
    //
    for (int i = 0; i < client->num_damage_indicators; i++) {
        const damage_indicator_t *indicator = &client->damage_indicators[i];

        // encode total damage into 5 bits
        int damage = Q_clip((indicator->health + indicator->power + indicator->armor) / 3, 1, 0x1F);

        // encode types in the latter 3 bits
        if (indicator->health)
            damage |= DAMAGE_INDICATOR_HEALTH;
        if (indicator->armor)
            damage |= DAMAGE_INDICATOR_ARMOR;
        if (indicator->power)
            damage |= DAMAGE_INDICATOR_POWER;

        v = Vec3_Direction(indicator->from, player->s.origin);
        client->ps.damage_indicators[i] = DirToByte(v) << 8 | damage;
    }

    //
    // clear totals
    //
    client->damage_blood = 0;
    client->damage_armor = 0;
    client->damage_parmor = 0;
    client->damage_knockback = 0;
    client->num_damage_indicators = 0;
}

/*
===============
SV_CalcViewOffset
===============
*/
static void SV_CalcViewOffset(edict_t *ent)
{
    // if dead, fix the angle and don't add any kick
    if (ent->deadflag && !ent->client->resp.spectator) {
        if (ent->flags & FL_SAM_RAIMI) {
            ent->client->ps.viewangles.roll = 0;
            ent->client->ps.viewangles.pitch = 0;
        } else {
            ent->client->ps.viewangles.roll = 40;
            ent->client->ps.viewangles.pitch = -15;
        }
        ent->client->ps.viewangles.yaw = ent->client->killer_yaw;
    }
}

/*
==============
SV_CalcGunOffset
==============
*/
static void SV_CalcGunOffset(edict_t *ent)
{
    // ROGUE
    // ROGUE - heatbeam shouldn't bob so the beam looks right
    if (ent->client->pers.weapon &&
        (ent->client->pers.weapon->id == IT_WEAPON_PLASMABEAM ||
         ent->client->pers.weapon->id == IT_WEAPON_GRAPPLE) && ent->client->weaponstate == WEAPON_FIRING)
        ent->client->ps.rdflags |= RDF_NO_WEAPON_BOB;
    else
        ent->client->ps.rdflags &= ~RDF_NO_WEAPON_BOB;
    // ROGUE
}

typedef struct {
    int ofs;
    const char *sound;
    vec4_t blend;
} powerup_info_t;

static const powerup_info_t powerup_table[] = {
    { CLOFS(quad_time),       "items/damage2.wav",   Vec4(0.0f, 0.0f, 1.0f, 0.08f) },
    { CLOFS(quadfire_time),   "items/quadfire2.wav", Vec4(1.0f, 0.2f, 0.5f, 0.08f) },
    { CLOFS(double_time),     "misc/ddamage2.wav",   Vec4(0.9f, 0.7f, 0.0f, 0.08f) },
    { CLOFS(invincible_time), "items/protect2.wav",  Vec4(1.0f, 1.0f, 0.0f, 0.08f) },
    { CLOFS(invisible_time),  "items/protect2.wav",  Vec4(0.8f, 0.8f, 0.8f, 0.08f) },
    { CLOFS(enviro_time),     "items/airout.wav",    Vec4(0.0f, 1.0f, 0.0f, 0.08f) },
    { CLOFS(breather_time),   "items/airout.wav",    Vec4(0.4f, 1.0f, 0.4f, 0.04f) },
};

/*
=============
SV_CalcBlend
=============
*/
static void SV_CalcBlend(edict_t *ent)
{
    gclient_t *client = ent->client;
    vec4_t screen_blend = vec4_origin;
    vec4_t damage_blend = vec4_origin;

    // add for powerups
    for (int i = 0; i < q_countof(powerup_table); i++) {
        const powerup_info_t *powerup = &powerup_table[i];
        gtime_t powerup_time = *(gtime_t *)((byte *)client + powerup->ofs);
        if (powerup_time > level.time) {
            gtime_t remaining = powerup_time - level.time;
            if (remaining == Time_FromSec(3)) // beginning to fade
                G_StartSound(ent, CHAN_ITEM, G_SoundIndex(powerup->sound), 1, ATTN_NORM);
            if (G_PowerUpExpiringRelative(remaining))
                BG_AddBlend(&screen_blend, powerup->blend);
            break;
        }
    }

    // PGM
    if (client->nuke_time > level.time) {
        float brightness = Time_ToSec(client->nuke_time - level.time) / 2.0f;
        BG_AddBlend(&screen_blend, Vec4(1, 1, 1, brightness));
    }
    if (client->ir_time > level.time) {
        gtime_t remaining = client->ir_time - level.time;
        if (G_PowerUpExpiringRelative(remaining)) {
            client->ps.rdflags |= RDF_IRGOGGLES;
            BG_AddBlend(&screen_blend, Vec4(1, 0, 0, 0.2f));
        } else
            client->ps.rdflags &= ~RDF_IRGOGGLES;
    } else {
        client->ps.rdflags &= ~RDF_IRGOGGLES;
    }
    // PGM

    // add for damage
    if (client->damage_alpha > 0)
        BG_AddBlend(&damage_blend, Vec4_FromVec3(client->damage_blend, client->damage_alpha));

    // [Paril-KEX] drowning visual indicator
    if (ent->air_finished < level.time + Time_FromSec(9)) {
        float alpha = ent->air_finished > level.time ?
            1.0f - Time_ToSec(ent->air_finished - level.time) / 9.0f : 1.0f;
        BG_AddBlend(&damage_blend, Vec4(0.1f, 0.1f, 0.2f, alpha * 0.75f));
    }

    if (client->bonus_alpha > 0)
        BG_AddBlend(&screen_blend, Vec4(0.85f, 0.7f, 0.3f, client->bonus_alpha));

    client->ps.screen_blend = screen_blend;
    client->ps.damage_blend = damage_blend;

    // drop the damage value
    client->damage_alpha -= FRAME_TIME_SEC * 0.6f;
    if (client->damage_alpha < 0)
        client->damage_alpha = 0;

    // drop the bonus value
    client->bonus_alpha -= FRAME_TIME_SEC;
    if (client->bonus_alpha < 0)
        client->bonus_alpha = 0;
}

/*
=============
P_WorldEffects
=============
*/
static void P_WorldEffects(edict_t *ent)
{
    bool          breather;
    bool          envirosuit;
    water_level_t waterlevel, old_waterlevel;

    if (ent->movetype == MOVETYPE_NOCLIP) {
        ent->air_finished = level.time + Time_FromSec(12); // don't need air
        return;
    }

    waterlevel = ent->waterlevel;
    old_waterlevel = ent->client->old_waterlevel;
    ent->client->old_waterlevel = waterlevel;

    breather = ent->client->breather_time > level.time;
    envirosuit = ent->client->enviro_time > level.time;

    //
    // if just entered a water volume, play a sound
    //
    if (!old_waterlevel && waterlevel) {
        PlayerNoise(ent, ent->s.origin, PNOISE_SELF);
        if (ent->watertype & CONTENTS_LAVA)
            G_StartSound(ent, CHAN_BODY, G_SoundIndex("player/lava_in.wav"), 1, ATTN_NORM);
        else if (ent->watertype & CONTENTS_SLIME)
            G_StartSound(ent, CHAN_BODY, G_SoundIndex("player/watr_in.wav"), 1, ATTN_NORM);
        else if (ent->watertype & CONTENTS_WATER)
            G_StartSound(ent, CHAN_BODY, G_SoundIndex("player/watr_in.wav"), 1, ATTN_NORM);
        ent->flags |= FL_INWATER;

        // clear damage_debounce, so the pain sound will play immediately
        ent->damage_debounce_time = level.time - Time_FromSec(1);
    }

    //
    // if just completely exited a water volume, play a sound
    //
    if (old_waterlevel && !waterlevel) {
        PlayerNoise(ent, ent->s.origin, PNOISE_SELF);
        G_StartSound(ent, CHAN_BODY, G_SoundIndex("player/watr_out.wav"), 1, ATTN_NORM);
        ent->flags &= ~FL_INWATER;
    }

    //
    // check for head just going under water
    //
    if (old_waterlevel != WATER_UNDER && waterlevel == WATER_UNDER) {
        G_StartSound(ent, CHAN_BODY, G_SoundIndex("player/watr_un.wav"), 1, ATTN_NORM);
    }

    //
    // check for head just coming out of water
    //
    if (ent->health > 0 && old_waterlevel == WATER_UNDER && waterlevel != WATER_UNDER) {
        if (ent->air_finished < level.time) {
            // gasp for air
            G_StartSound(ent, CHAN_VOICE, G_SoundIndex("player/gasp1.wav"), 1, ATTN_NORM);
            PlayerNoise(ent, ent->s.origin, PNOISE_SELF);
        } else if (ent->air_finished < level.time + Time_FromSec(11)) {
            // just break surface
            G_StartSound(ent, CHAN_VOICE, G_SoundIndex("player/gasp2.wav"), 1, ATTN_NORM);
        }
    }

    //
    // check for drowning
    //
    if (waterlevel == WATER_UNDER && !(ent->flags & FL_DEEPONE)) {
        // breather or envirosuit give air
        if (breather || envirosuit) {
            ent->air_finished = level.time + Time_FromSec(10);

            if (((ent->client->breather_time - level.time) % Time_FromSec(2.5f)) == 0) {
                if (!ent->client->breather_sound)
                    G_StartSound(ent, CHAN_AUTO, G_SoundIndex("player/u_breath1.wav"), 1, ATTN_NORM);
                else
                    G_StartSound(ent, CHAN_AUTO, G_SoundIndex("player/u_breath2.wav"), 1, ATTN_NORM);
                ent->client->breather_sound ^= 1;
                PlayerNoise(ent, ent->s.origin, PNOISE_SELF);
                // FIXME: release a bubble?
            }
        }

        // if out of air, start drowning
        if (ent->air_finished < level.time) {
            // drown!
            if (ent->client->next_drown_time < level.time && ent->health > 0) {
                ent->client->next_drown_time = level.time + Time_FromSec(1);

                // take more damage the longer underwater
                ent->dmg += 2;
                if (ent->dmg > 15)
                    ent->dmg = 15;

                // play a gurp sound instead of a normal pain sound
                if (ent->health <= ent->dmg)
                    G_AddEvent(ent, EV_DROWN, 0); // [Paril-KEX]
                else
                    G_AddEvent(ent, EV_GURP, 0);

                ent->pain_debounce_time = level.time;

                T_Damage(ent, world, world, vec3_origin, ent->s.origin,
                         BYTEDIR_NONE, ent->dmg, 0, DAMAGE_NO_ARMOR, MOD_WATER);
            }
        // Paril: almost-drowning sounds
        } else if (ent->air_finished <= level.time + Time_FromSec(3)) {
            if (ent->client->next_drown_time < level.time) {
                const char *fmt = (game.dirtype == GAMEDIR_PSX) ? "player/breathout%d.wav" : "player/wade%d.wav";
                G_StartSound(ent, CHAN_VOICE, G_SoundIndex(va(fmt, 1 + ((int)Time_ToSec(level.time) % 3))), 1, ATTN_NORM);
                ent->client->next_drown_time = level.time + Time_FromSec(1);
            }
        }
    } else {
        ent->air_finished = level.time + Time_FromSec(12);
        ent->dmg = 2;
    }

    //
    // check for sizzle damage
    //
    if (waterlevel && (ent->watertype & (CONTENTS_LAVA | CONTENTS_SLIME)) && ent->slime_debounce_time <= level.time) {
        if (ent->watertype & CONTENTS_LAVA) {
            if (ent->health > 0 && ent->pain_debounce_time <= level.time && ent->client->invincible_time < level.time) {
                if (brandom())
                    G_StartSound(ent, CHAN_VOICE, G_SoundIndex("player/burn1.wav"), 1, ATTN_NORM);
                else
                    G_StartSound(ent, CHAN_VOICE, G_SoundIndex("player/burn2.wav"), 1, ATTN_NORM);
                ent->pain_debounce_time = level.time + Time_FromSec(1);
            }

            int dmg = (envirosuit ? 1 : 3) * waterlevel; // take 1/3 damage with envirosuit

            T_Damage(ent, world, world, vec3_origin, ent->s.origin, BYTEDIR_NONE, dmg, 0, DAMAGE_NONE, MOD_LAVA);
            ent->slime_debounce_time = level.time + BASE_FRAMETIME;
        }

        if (ent->watertype & CONTENTS_SLIME) {
            if (!envirosuit) {
                // no damage from slime with envirosuit
                T_Damage(ent, world, world, vec3_origin, ent->s.origin, BYTEDIR_NONE, 1 * waterlevel, 0, DAMAGE_NONE, MOD_SLIME);
                ent->slime_debounce_time = level.time + BASE_FRAMETIME;
            }
        }
    }
}

/*
===============
G_SetClientEffects
===============
*/
static void G_SetClientEffects(edict_t *ent)
{
    int pa_type;

    ent->s.effects = EF_NONE;
    ent->s.morefx = EFX_NONE;
    ent->s.renderfx = RF_IR_VISIBLE;
    ent->s.alpha = 0.0f;

    if (ent->health <= 0 || level.intermissiontime)
        return;

    if (ent->flags & FL_FLASHLIGHT)
        ent->s.morefx |= EFX_FLASHLIGHT;

    //=========
    // PGM
    if (ent->flags & FL_DISGUISED)
        ent->s.renderfx |= RF_USE_DISGUISE;

    if (gamerules.integer) {
        if (DMGame.PlayerEffects)
            DMGame.PlayerEffects(ent);
    }
    // PGM
    //=========

    if (ent->powerarmor_time > level.time) {
        pa_type = PowerArmorType(ent);
        if (pa_type == IT_ITEM_POWER_SCREEN) {
            ent->s.effects |= EF_POWERSCREEN;
        } else if (pa_type == IT_ITEM_POWER_SHIELD) {
            ent->s.effects |= EF_COLOR_SHELL;
            ent->s.renderfx |= RF_SHELL_GREEN;
        }
    }

    // ZOID
    CTFEffects(ent);
    // ZOID

    if (ent->client->quad_time > level.time) {
        if (G_PowerUpExpiring(ent->client->quad_time))
            CTFSetPowerUpEffect(ent, EF_QUAD);
    }

    // RAFAEL
    if (ent->client->quadfire_time > level.time) {
        if (G_PowerUpExpiring(ent->client->quadfire_time))
            //CTFSetPowerUpEffect(ent, EF_DUALFIRE);
            ent->s.morefx |= EFX_DUALFIRE;
    }
    // RAFAEL

    //=======
    // ROGUE
    if (ent->client->double_time > level.time) {
        if (G_PowerUpExpiring(ent->client->double_time))
            CTFSetPowerUpEffect(ent, EF_DOUBLE);
    }
    if ((ent->client->owned_sphere) && (ent->client->owned_sphere->spawnflags == SPHERE_DEFENDER))
        CTFSetPowerUpEffect(ent, EF_HALF_DAMAGE);

    if (ent->client->tracker_pain_time > level.time)
        ent->s.effects |= EF_TRACKERTRAIL;

    if (ent->client->invisible_time > level.time) {
        if (ent->client->invisibility_fade_time <= level.time)
            ent->s.alpha = 0.1f;
        else
            ent->s.alpha = Q_clipf((float)(ent->client->invisibility_fade_time - level.time) / INVISIBILITY_TIME, 0.1f, 1.0f);
    }
    // ROGUE
    //=======

    if (ent->client->invincible_time > level.time) {
        if (G_PowerUpExpiring(ent->client->invincible_time))
            CTFSetPowerUpEffect(ent, EF_PENT);
    }

    // show cheaters!!!
    if (ent->flags & FL_GODMODE) {
        ent->s.effects |= EF_COLOR_SHELL;
        ent->s.renderfx |= (RF_SHELL_RED | RF_SHELL_GREEN | RF_SHELL_BLUE);
    }
}

/*
===============
G_SetClientSound
===============
*/
static void G_SetClientSound(edict_t *ent)
{
    // reset defaults
    ent->s.sound = 0;

    if (ent->waterlevel && (ent->watertype & (CONTENTS_LAVA | CONTENTS_SLIME))) {
        ent->s.sound = G_EncodeSound(CHAN_AUTO, level.snd_fry, 1, ATTN_STATIC);
        return;
    }

    if (ent->deadflag || ent->client->resp.spectator)
        return;

    if (ent->client->weapon_sound)
        ent->s.sound = ent->client->weapon_sound;
    else if (ent->client->pers.weapon) {
        if (ent->client->pers.weapon->id == IT_WEAPON_RAILGUN)
            ent->s.sound = G_SoundIndex("weapons/rg_hum.wav");
        else if (ent->client->pers.weapon->id == IT_WEAPON_BFG)
            ent->s.sound = G_SoundIndex("weapons/bfg_hum.wav");
        // RAFAEL
        else if (ent->client->pers.weapon->id == IT_WEAPON_PHALANX)
            ent->s.sound = G_SoundIndex("weapons/phaloop.wav");
        // RAFAEL
    }

    // [Paril-KEX] if no other sound is playing, play appropriate grapple sounds
    if (!ent->s.sound && ent->client->ctf_grapple) {
        if (ent->client->ctf_grapplestate == CTF_GRAPPLE_STATE_PULL)
            ent->s.sound = G_SoundIndex("weapons/grapple/grpull.wav");
        else if (ent->client->ctf_grapplestate == CTF_GRAPPLE_STATE_FLY)
            ent->s.sound = G_SoundIndex("weapons/grapple/grfly.wav");
        else if (ent->client->ctf_grapplestate == CTF_GRAPPLE_STATE_HANG)
            ent->s.sound = G_SoundIndex("weapons/grapple/grhang.wav");
    }

    // weapon sounds play at a higher attn
    ent->s.sound = G_EncodeSound(CHAN_AUTO, ent->s.sound, 1, ATTN_NORM);
}

/*
===============
G_SetClientFrame
===============
*/
void G_SetClientFrame(edict_t *ent, gclient_t *client)
{
    bool       duck, run;

    if (ent->s.modelindex != MODELINDEX_PLAYER)
        return; // not in the player model

    duck = (client->ps.pm_flags & PMF_DUCKED);
    run  = (ent->velocity.x || ent->velocity.y);

    // check for stand/duck and stop/go transitions
    if (duck != client->anim_duck && client->anim_priority < ANIM_DEATH)
        goto newanim;
    if (run != client->anim_run && client->anim_priority == ANIM_BASIC)
        goto newanim;
    if (!ent->groundentity && client->anim_priority <= ANIM_WAVE)
        goto newanim;

    if (client->anim_time > level.time)
        return;
    if ((client->anim_priority & ANIM_REVERSED) && (ent->s.frame > client->anim_end)) {
        if (client->anim_time <= level.time) {
            ent->s.frame--;
            client->anim_time = level.time + BASE_FRAMETIME;
        }
        return;
    }
    if (!(client->anim_priority & ANIM_REVERSED) && (ent->s.frame < client->anim_end)) {
        // continue an animation
        if (client->anim_time <= level.time) {
            ent->s.frame++;
            client->anim_time = level.time + BASE_FRAMETIME;
        }
        return;
    }

    if (client->anim_priority == ANIM_DEATH)
        return; // stay there
    if (client->anim_priority == ANIM_JUMP) {
        if (!ent->groundentity)
            return; // stay there
        ent->client->anim_priority = ANIM_WAVE;

        if (duck) {
            ent->s.frame = FRAME_jump6;
            ent->client->anim_end = FRAME_jump4;
            ent->client->anim_priority |= ANIM_REVERSED;
        } else {
            ent->s.frame = FRAME_jump3;
            ent->client->anim_end = FRAME_jump6;
        }
        ent->client->anim_time = level.time + BASE_FRAMETIME;
        return;
    }

newanim:
    // return to either a running or standing frame
    client->anim_priority = ANIM_BASIC;
    client->anim_duck = duck;
    client->anim_run = run;
    client->anim_time = level.time + BASE_FRAMETIME;

    if (!ent->groundentity) {
        // ZOID: if on grapple, don't go into jump frame, go into standing
        // frame
        if (client->ctf_grapple) {
            if (duck) {
                ent->s.frame = FRAME_crstnd01;
                client->anim_end = FRAME_crstnd19;
            } else {
                ent->s.frame = FRAME_stand01;
                client->anim_end = FRAME_stand40;
            }
        } else {
            // ZOID
            client->anim_priority = ANIM_JUMP;

            if (duck) {
                if (ent->s.frame != FRAME_crwalk2)
                    ent->s.frame = FRAME_crwalk1;
                client->anim_end = FRAME_crwalk2;
            } else {
                if (ent->s.frame != FRAME_jump2)
                    ent->s.frame = FRAME_jump1;
                client->anim_end = FRAME_jump2;
            }
        }
    } else if (run) {
        // running
        if (duck) {
            ent->s.frame = FRAME_crwalk1;
            client->anim_end = FRAME_crwalk6;
        } else {
            ent->s.frame = FRAME_run1;
            client->anim_end = FRAME_run6;
        }
    } else {
        // standing
        if (duck) {
            ent->s.frame = FRAME_crstnd01;
            client->anim_end = FRAME_crstnd19;
        } else {
            ent->s.frame = FRAME_stand01;
            client->anim_end = FRAME_stand40;
        }
    }
}

static void P_SetClientFog(edict_t *ent)
{
    if (ent->client->fog_transition_end <= level.time) {
        ent->client->ps.fog = ent->client->wanted_fog;
        ent->client->ps.heightfog = ent->client->wanted_heightfog;
        return;
    }

    float frac = (float)(level.time - ent->client->fog_transition_start) /
        (ent->client->fog_transition_end - ent->client->fog_transition_start);

    ent->client->ps.fog = BG_LerpFog(ent->client->start_fog, ent->client->wanted_fog, frac);
    ent->client->ps.heightfog = BG_LerpHeightFog(ent->client->start_heightfog, ent->client->wanted_heightfog, frac);
}

// [Paril-KEX]
static void P_RunMegaHealth(edict_t *ent)
{
    if (!ent->client->pers.megahealth_time)
        return;
    if (ent->health <= ent->max_health) {
        ent->client->pers.megahealth_time = 0;
        return;
    }

    ent->client->pers.megahealth_time -= FRAME_TIME;

    if (ent->client->pers.megahealth_time <= 0) {
        ent->health--;

        if (ent->health > ent->max_health)
            ent->client->pers.megahealth_time = Time_FromSec(1);
        else
            ent->client->pers.megahealth_time = 0;
    }
}

static void P_CheckEnableCollide(edict_t *ent)
{
    if ((ent->clipmask & CONTENTS_PLAYER) || !ent->takedamage || !G_ShouldPlayersCollide(false))
        return;

    FOR_EACH_PLAYER(player) {
        if (player == ent)
            continue;
        if (Box3_Intersects(ent->r.absbox, player->r.absbox))
            return;
    }

    // safe!
    ent->clipmask |= CONTENTS_PLAYER;
}

/*
=================
ClientEndServerFrame

Called for each player at the end of the server frame
and right after spawning
=================
*/
void ClientEndServerFrame(edict_t *ent)
{
    // no player exists yet (load game)
    if (!ent->client->pers.spawned)
        return;

    // check goals
    G_PlayerNotifyGoal(ent);

    // mega health
    P_RunMegaHealth(ent);

    //
    // If the origin or velocity have changed since ClientThink(),
    // update the pmove values.  This will happen when the client
    // is pushed by a bmodel or kicked by an explosion.
    //
    // If it wasn't updated here, the view position would lag a frame
    // behind the body position when pushed -- "sinking into plats"
    //
    ent->client->ps.origin = ent->s.origin;
    ent->client->ps.velocity = ent->velocity;

    //
    // If the end of unit layout is displayed, don't give
    // the player any normal movement attributes
    //
    if (level.intermissiontime || ent->client->awaiting_respawn) {
        if (ent->client->awaiting_respawn || level.intermission_eou || level.is_n64 || (deathmatch.integer && level.intermissiontime)) {
            ent->client->ps.screen_blend.a = 0;
            ent->client->ps.damage_blend.a = 0;
            ent->client->ps.fov = 90;
            ent->client->ps.gunindex = 0;
            ent->client->ps.gunskin = 0;
        }
        G_SetStats(ent);
        G_SetCoopStats(ent);

        // if the scoreboard is up, update it if a client leaves
        if (deathmatch.integer && ent->client->showscores && ent->client->menutime) {
            DeathmatchScoreboardMessage(ent, ent->enemy, false);
            ent->client->menutime = 0;
        }

        return;
    }

    // ZOID
    // regen tech
    CTFApplyRegeneration(ent);
    // ZOID

    // burn from lava, etc
    P_WorldEffects(ent);

    //
    // set model angles from view angles so other things in
    // the world can tell which direction you are looking
    //
    vec3_t right;
    AngleVectors(ent->client->v_angle, NULL, &right, NULL);

    if (ent->client->v_angle.pitch > 180)
        ent->s.angles.pitch = (-360 + ent->client->v_angle.pitch) / 3;
    else
        ent->s.angles.pitch = ent->client->v_angle.pitch / 3;

    ent->s.angles.yaw = ent->client->v_angle.yaw;
    ent->s.angles.roll = SV_CalcRoll(Vec3_Dot(ent->velocity, right)) * 4;

    // apply all the damage taken this frame
    P_DamageFeedback(ent);

    // determine the view offsets
    SV_CalcViewOffset(ent);

    // determine the gun offsets
    SV_CalcGunOffset(ent);

    // determine the full screen color blend
    // must be after viewoffset, so eye contents can be
    // accurately determined
    SV_CalcBlend(ent);

    // chase cam stuff
    if (ent->client->resp.spectator)
        G_SetSpectatorStats(ent);
    else
        G_SetStats(ent);

    G_SetCoopStats(ent);

    G_SetClientEffects(ent);

    G_SetClientSound(ent);

    G_SetClientFrame(ent, ent->client);

    P_SetClientFog(ent);

    ent->client->oldvelocity = ent->velocity;
    ent->client->oldviewangles = ent->client->ps.viewangles;
    ent->client->oldgroundentity = ent->groundentity;

    // ZOID
    if (ent->client->menudirty && ent->client->menutime <= level.time) {
        if (ent->client->menu)
            PMenu_Do_Update(ent, true);
        ent->client->menutime = level.time;
        ent->client->menudirty = false;
    }
    // ZOID

    // if the scoreboard is up, update it
    if (ent->client->showscores && ent->client->menutime <= level.time) {
        // ZOID
        if (ent->client->menu) {
            PMenu_Do_Update(ent, false);
            ent->client->menudirty = false;
        } else
        // ZOID
            DeathmatchScoreboardMessage(ent, ent->enemy, false);
        ent->client->menutime = level.time + Time_FromSec(3);
    }

    P_AssignClientSkinnum(ent);

    Compass_Update(ent, false);

    // [Paril-KEX] in coop, if player collision is enabled and
    // we are currently in no-player-collision mode, check if
    // it's safe.
    P_CheckEnableCollide(ent);
}
