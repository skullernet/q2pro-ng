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
#include "shared/bg_local.h"

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
    EV(PLAYER_TELEPORT),
    EV(OTHER_TELEPORT),
    EV(OTHER_FOOTSTEP),
    EV(LADDER_STEP),
    EV(MUZZLEFLASH),
    EV(MUZZLEFLASH2),
    EV(SOUND),
    EV(BERSERK_SLAM),
    EV(GUNCMDR_SLAM),
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
    if (event < q_countof(event_names))
        return event_names[event];
    return "unknown";
}

void G_AddBlend(float r, float g, float b, float a, vec4_t v_blend)
{
    if (a <= 0)
        return;

    float a2 = v_blend[3] + (1 - v_blend[3]) * a; // new total alpha
    float a3 = v_blend[3] / a2; // fraction of color from old

    v_blend[0] = v_blend[0] * a3 + r * (1 - a3);
    v_blend[1] = v_blend[1] * a3 + g * (1 - a3);
    v_blend[2] = v_blend[2] * a3 + b * (1 - a3);
    v_blend[3] = a2;
}

void vectoangles(const vec3_t value1, vec3_t angles)
{
    float   forward;
    float   yaw, pitch;

    if (value1[1] == 0 && value1[0] == 0) {
        yaw = 0;
        if (value1[2] > 0)
            pitch = 90;
        else
            pitch = 270;
    } else {
        if (value1[0])
            yaw = RAD2DEG(atan2f(value1[1], value1[0]));
        else if (value1[1] > 0)
            yaw = 90;
        else
            yaw = 270;
        if (yaw < 0)
            yaw += 360;

        forward = sqrtf(value1[0] * value1[0] + value1[1] * value1[1]);
        pitch = RAD2DEG(atan2f(value1[2], forward));
        if (pitch < 0)
            pitch += 360;
    }

    angles[PITCH] = -pitch;
    angles[YAW] = yaw;
    angles[ROLL] = 0;
}
