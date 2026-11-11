/*
Copyright (C) 1997-2001 Id Software, Inc.
Copyright (c) ZeniMax Media Inc.

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

#include "shared/shared.h"
#include "bgame/bg_local.h"

#define EV(n)   [EV_##n] = #n

static const char *const event_names[] = {
    EV(NONE),
    EV(ITEM_RESPAWN),
    EV(FOOTSTEP),
    EV(FALL),
    EV(DEATH1),
    EV(DEATH2),
    EV(DEATH3),
    EV(DEATH4),
    EV(PAIN),
    EV(GURP),
    EV(DROWN),
    EV(JUMP),
    EV(OBITUARY),
    EV(PLAYER_TELEPORT),
    EV(OTHER_TELEPORT),
    EV(OTHER_FOOTSTEP),
    EV(LADDER_STEP),
    EV(STAIR_STEP),
    EV(MUZZLEFLASH),
    EV(MUZZLEFLASH2),
    EV(SOUND),
    EV(POSITIONED_SOUND),
    EV(RAILTRAIL),
    EV(RAILTRAIL2),
    EV(BUBBLETRAIL),
    EV(BUBBLETRAIL2),
    EV(BFG_LASER),
    EV(BFG_ZAP),
    EV(EARTHQUAKE),
    EV(EARTHQUAKE2),

    EV(SPLASH_UNKNOWN),
    EV(SPLASH_SPARKS),
    EV(SPLASH_BLUE_WATER),
    EV(SPLASH_BROWN_WATER),
    EV(SPLASH_SLIME),
    EV(SPLASH_LAVA),
    EV(SPLASH_BLOOD),
    EV(SPLASH_ELECTRIC_N64),

    EV(BLOOD),
    EV(MORE_BLOOD),
    EV(GREEN_BLOOD),
    EV(GUNSHOT),
    EV(SHOTGUN),
    EV(NAILS),
    EV(SPARKS),
    EV(BULLET_SPARKS),
    EV(HEATBEAM_SPARKS),
    EV(HEATBEAM_STEAM),
    EV(SCREEN_SPARKS),
    EV(SHIELD_SPARKS),
    EV(ELECTRIC_SPARKS),
    EV(LASER_SPARKS),
    EV(WELDING_SPARKS),
    EV(TUNNEL_SPARKS),

    EV(EXPLOSION_PLAIN),
    EV(EXPLOSION1),
    EV(EXPLOSION1_NL),
    EV(EXPLOSION1_NP),
    EV(EXPLOSION1_BIG),
    EV(EXPLOSION2),
    EV(EXPLOSION2_NL),
    EV(BLASTER),
    EV(BLASTER2),
    EV(FLECHETTE),
    EV(BLUEHYPERBLASTER),
    EV(GRENADE_EXPLOSION),
    EV(GRENADE_EXPLOSION_WATER),
    EV(ROCKET_EXPLOSION),
    EV(ROCKET_EXPLOSION_WATER),
    EV(BFG_EXPLOSION),
    EV(BFG_EXPLOSION_BIG),
    EV(TRACKER_EXPLOSION),
    EV(ENFORCER_BOLT),
    EV(HELLKNIGHT_MAGIC),
    EV(WIZARD_SPIT),

    EV(POWER_SPLASH),
    EV(BOSSTPORT),
    EV(TELEPORT_EFFECT),
    EV(CHAINFIST_SMOKE),
    EV(NUKEBLAST),
    EV(WIDOWBEAMOUT),
    EV(WIDOWSPLASH),
};

const char *BG_EventName(entity_event_t event)
{
    if (event < q_countof(event_names) && event_names[event])
        return event_names[event];
    return va("UNKNOWN_%d", event);
}

const char *BG_ObituaryString(mod_t mod, player_gender_t gender, uint32_t targ, uint32_t attacker)
{
    // killed self
    if (attacker == targ) {
        switch (mod) {
        case MOD_SUICIDE:
        case MOD_SUICIDE_NP:
            return "%s suicides.\n";
        case MOD_HELD_GRENADE:
            return "%s tried to put the pin back in.\n";
        case MOD_HG_SPLASH:
        case MOD_G_SPLASH:
            switch (gender) {
            case GENDER_MALE:
                return "%s tripped on his own grenade.\n";
            case GENDER_FEMALE:
                return "%s tripped on her own grenade.\n";
            default:
                return "%s tripped on their own grenade.\n";
            }
        case MOD_R_SPLASH:
            switch (gender) {
            case GENDER_MALE:
                return "%s blew himself up.\n";
            case GENDER_FEMALE:
                return "%s blew herself up.\n";
            default:
                return "%s blew themselves up.\n";
            }
        case MOD_BFG_BLAST:
            return "%s should have used a smaller gun.\n";
        // RAFAEL 03-MAY-98
        case MOD_TRAP:
            switch (gender) {
            case GENDER_MALE:
                return "%s was sucked into his own trap.\n";
            case GENDER_FEMALE:
                return "%s was sucked into her own trap.\n";
            default:
                return "%s was sucked into their own trap.\n";
            }
        // RAFAEL
        // ROGUE
        case MOD_DOPPLE_EXPLODE:
            switch (gender) {
            case GENDER_MALE:
                return "%s was fooled by his own doppelganger.\n";
            case GENDER_FEMALE:
                return "%s was fooled by her own doppelganger.\n";
            default:
                return "%s was fooled by their own doppelganger.\n";
            }
        // ROGUE
        default:
            switch (gender) {
            case GENDER_MALE:
                return "%s killed himself.\n";
            case GENDER_FEMALE:
                return "%s killed herself.\n";
            default:
                return "%s killed themselves.\n";
            }
        }
    }

    // has a killer
    if (attacker < MAX_CLIENTS) {
        switch (mod) {
        case MOD_BLASTER:
            return "%s was blasted by %s.\n";
        case MOD_SHOTGUN:
            return "%s was gunned down by %s.\n";
        case MOD_SSHOTGUN:
            return "%s was blown away by %s's Super Shotgun.\n";
        case MOD_MACHINEGUN:
            return "%s was machinegunned by %s.\n";
        case MOD_CHAINGUN:
            return "%s was cut in half by %s's Chaingun.\n";
        case MOD_GRENADE:
            return "%s was popped by %s's grenade.\n";
        case MOD_G_SPLASH:
            return "%s was shredded by %s's shrapnel.\n";
        case MOD_ROCKET:
            return "%s ate %s's rocket.\n";
        case MOD_R_SPLASH:
            return "%s almost dodged %s's rocket.\n";
        case MOD_HYPERBLASTER:
            return "%s was melted by %s's HyperBlaster.\n";
        case MOD_RAILGUN:
            return "%s was railed by %s.\n";
        case MOD_BFG_LASER:
            return "%s saw the pretty lights from %s's BFG.\n";
        case MOD_BFG_BLAST:
            return "%s was disintegrated by %s's BFG blast.\n";
        case MOD_BFG_EFFECT:
            return "%s couldn't hide from %s's BFG.\n";
        case MOD_HANDGRENADE:
            return "%s caught %s's handgrenade.\n";
        case MOD_HG_SPLASH:
            return "%s didn't see %s's handgrenade.\n";
        case MOD_HELD_GRENADE:
            return "%s feels %s's pain.\n";
        case MOD_TELEFRAG:
        case MOD_TELEFRAG_SPAWN:
            return "%s tried to invade %s's personal space.\n";
        // RAFAEL 14-APR-98
        case MOD_RIPPER:
            return "%s ripped to shreds by %s's ripper gun.\n";
        case MOD_PHALANX:
            return "%s was evaporated by %s.\n";
        case MOD_TRAP:
            return "%s was caught in %s's trap.\n";
        // RAFAEL
        //===============
        // ROGUE
        case MOD_CHAINFIST:
            return "%s was shredded by %s's ripsaw.\n";
        case MOD_DISINTEGRATOR:
            return "%s lost his grip courtesy of %s's Disintegrator.\n";
        case MOD_ETF_RIFLE:
            return "%s was perforated by %s.\n";
        case MOD_HEATBEAM:
            return "%s was scorched by %s's Plasma Beam.\n";
        case MOD_TESLA:
            return "%s was enlightened by %s's tesla mine.\n";
        case MOD_PROX:
            return "%s got too close to %s's proximity mine.\n";
        case MOD_NUKE:
            return "%s was nuked by %s's antimatter bomb.\n";
        case MOD_VENGEANCE_SPHERE:
            return "%s was purged by %s's Vengeance Sphere.\n";
        case MOD_DEFENDER_SPHERE:
            return "%s had a blast with %s's Defender Sphere.\n";
        case MOD_HUNTER_SPHERE:
            return "%s was hunted down by %s's Hunter Sphere.\n";
        case MOD_TRACKER:
            return "%s was annihilated by %s's Disruptor.\n";
        case MOD_DOPPLE_EXPLODE:
            return "%s was tricked by %s's Doppelganger.\n";
        case MOD_DOPPLE_VENGEANCE:
            return "%s was purged by %s's Doppelganger.\n";
        case MOD_DOPPLE_HUNTER:
            return "%s was hunted down by %s's Doppelganger.\n";
        // ROGUE
        //===============
        // ZOID
        case MOD_GRAPPLE:
            return "%s was caught by %s's grapple.\n";
        // ZOID
        default:
            return "%s was killed by %s.\n";
        }
    }

    // generic
    switch (mod) {
    case MOD_FALLING:
        return "%s cratered.\n";
    case MOD_CRUSH:
        return "%s was squished.\n";
    case MOD_WATER:
        return "%s sank like a rock.\n";
    case MOD_SLIME:
        return "%s melted.\n";
    case MOD_LAVA:
        return "%s does a back flip into the lava.\n";
    case MOD_EXPLOSIVE:
    case MOD_BARREL:
        return "%s blew up.\n";
    case MOD_EXIT:
        return "%s found a way out.\n";
    case MOD_TARGET_LASER:
        return "%s saw the light.\n";
    case MOD_TARGET_BLASTER:
        return "%s got blasted.\n";
    case MOD_BOMB:
    case MOD_SPLASH:
    case MOD_TRIGGER_HURT:
        return "%s was in the wrong place.\n";
    // RAFAEL
    case MOD_GEKK:
    case MOD_BRAINTENTACLE:
        return "%s... that's gotta hurt!\n";
    // RAFAEL
    default:
        return "%s died.\n";
    }
}

void BG_ParseSkyParams(sky_params_t *sky, const char *s)
{
    COM_ParseToken(&s, sky->name, sizeof(sky->name));
    sky->rotate     = Q_atof(COM_Parse(&s));
    sky->autorotate = Q_atoi(COM_Parse(&s));
    sky->axis.x     = Q_atof(COM_Parse(&s));
    sky->axis.y     = Q_atof(COM_Parse(&s));
    sky->axis.z     = Q_atof(COM_Parse(&s));
}

const char *BG_FormatSkyParams(const sky_params_t *sky)
{
    return va("\"%s\" %f %d %f %f %f",
              sky->name, sky->rotate, sky->autorotate,
              sky->axis.x, sky->axis.y, sky->axis.z);
}

void BG_ParseLevelEntry(level_entry_t *entry, const char *s)
{
    COM_ParseToken(&s, entry->map_name, sizeof(entry->map_name));
    COM_ParseToken(&s, entry->pretty_name, sizeof(entry->pretty_name));
    entry->total_secrets = Q_atoi(COM_Parse(&s));
    entry->found_secrets = Q_atoi(COM_Parse(&s));
    entry->total_monsters = Q_atoi(COM_Parse(&s));
    entry->killed_monsters = Q_atoi(COM_Parse(&s));
    entry->time = strtoll(COM_Parse(&s), NULL, 10);
    entry->visit_order = Q_atoi(COM_Parse(&s));
}

const char *BG_FormatLevelEntry(const level_entry_t *entry)
{
    return va("\"%s\" \"%s\" %d %d %d %d %"PRId64" %d",
              entry->map_name, entry->pretty_name,
              entry->total_secrets, entry->found_secrets,
              entry->total_monsters, entry->killed_monsters,
              entry->time, entry->visit_order);
}
