/*
Copyright (C) 2025 Andrey Nazarov

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

#pragma once

typedef enum {
    REVERB_PRESET_NONE,
    REVERB_PRESET_GENERIC,
    REVERB_PRESET_PADDEDCELL,
    REVERB_PRESET_ROOM,
    REVERB_PRESET_BATHROOM,
    REVERB_PRESET_LIVINGROOM,
    REVERB_PRESET_STONEROOM,
    REVERB_PRESET_AUDITORIUM,
    REVERB_PRESET_CONCERTHALL,
    REVERB_PRESET_CAVE,
    REVERB_PRESET_ARENA,
    REVERB_PRESET_HANGAR,
    REVERB_PRESET_CARPETEDHALLWAY,
    REVERB_PRESET_HALLWAY,
    REVERB_PRESET_STONECORRIDOR,
    REVERB_PRESET_ALLEY,
    REVERB_PRESET_FOREST,
    REVERB_PRESET_CITY,
    REVERB_PRESET_MOUNTAINS,
    REVERB_PRESET_QUARRY,
    REVERB_PRESET_PLAIN,
    REVERB_PRESET_PARKINGLOT,
    REVERB_PRESET_SEWERPIPE,
    REVERB_PRESET_UNDERWATER,
    REVERB_PRESET_DRUGGED,
    REVERB_PRESET_DIZZY,
    REVERB_PRESET_PSYCHOTIC,
    REVERB_PRESET_MAX
} reverb_preset_t;

typedef struct {
    unsigned entnum;
    vec3_t origin;
    vec3_t velocity;
    vec3_t v_forward;
    vec3_t v_up;
    bool underwater;
    struct {
        reverb_preset_t preset;
        float gain;
    } reverb[2];
} listener_t;
