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

#pragma once

#define STEPSIZE    18.0f

#define STOP_EPSILON 0.1f

#define MIN_STEP_HEIGHT     4.0f

#define MIN_STEP_NORMAL     0.7f // can't step up onto very steep slopes

#define PSX_PHYSICS_SCALAR  0.875f

#define INFINITE_AMMO   MASK(AMMO_BITS)

//
// config strings are a general means of communication from
// the server to all connected clients.
// Each config string can be at most MAX_QPATH characters.
//
#define MAX_ITEMS           256
#define MAX_CLIENTWEAPONS   256     // PGM -- upped from 16 to fit the chainfist vwep
#define MAX_GENERAL         512     // general config strings
#define MAX_WHEEL_ITEMS     32

#define CS_NAME             0       // server and game both reference!!!
#define CS_CDTRACK          1
#define CS_SKY              2
#define CS_STATUSBAR        3       // display program string
#define CS_AIRACCEL         4
#define CS_MAXCLIENTS       5
#define CS_MODELS           6
#define CS_SOUNDS           (CS_MODELS + MAX_MODELS)
#define CS_IMAGES           (CS_SOUNDS + MAX_SOUNDS)
#define CS_LIGHTS           (CS_IMAGES + MAX_IMAGES)
#define CS_ITEMS            (CS_LIGHTS + MAX_LIGHTSTYLES)
#define CS_CLIENTWEAPONS    (CS_ITEMS + MAX_ITEMS)
#define CS_PLAYERSKINS      (CS_CLIENTWEAPONS + MAX_CLIENTWEAPONS)
#define CS_GENERAL          (CS_PLAYERSKINS + MAX_CLIENTS)
#define CS_WHEEL_WEAPONS    (CS_GENERAL + MAX_GENERAL)
#define CS_WHEEL_AMMO       (CS_WHEEL_WEAPONS + MAX_WHEEL_ITEMS)
#define CS_WHEEL_POWERUPS   (CS_WHEEL_AMMO + MAX_WHEEL_ITEMS)
#define CS_END              (CS_GENERAL + MAX_GENERAL)

#if CS_END > MAX_CONFIGSTRINGS
#error Too many configstrings
#endif

// STAT_LAYOUTS flags
#define LAYOUTS_LAYOUT          BIT(0)
#define LAYOUTS_INVENTORY       BIT(1)
#define LAYOUTS_HIDE_HUD        BIT(2)
#define LAYOUTS_INTERMISSION    BIT(3)
#define LAYOUTS_HELP            BIT(4)
#define LAYOUTS_HIDE_CROSSHAIR  BIT(5)

// uf flags
#define UF_AUTOSCREENSHOT   BIT(0)
#define UF_AUTORECORD       BIT(1)
#define UF_LOCALFOV         BIT(2)
#define UF_MUTE_PLAYERS     BIT(3)
#define UF_MUTE_OBSERVERS   BIT(4)
#define UF_MUTE_MISC        BIT(5)
#define UF_PLAYERFOV        BIT(6)

//==============================================

// entity_state_t->effects
// Effects are things handled on the client side (lights, particles, frame animations)
// that happen constantly on the given entity.
// An entity that has effects will be sent to the client
// even if it has a zero index model.
#define EF_NONE             0U
#define EF_ROTATE           BIT(0)      // rotate (bonus items)
#define EF_GIB              BIT(1)      // leave a trail
#define EF_BOB              BIT(2)      // used by KEX
#define EF_BLASTER          BIT(3)      // redlight + trail
#define EF_ROCKET           BIT(4)      // redlight + trail
#define EF_GRENADE          BIT(5)
#define EF_HYPERBLASTER     BIT(6)
#define EF_BFG              BIT(7)
#define EF_COLOR_SHELL      BIT(8)
#define EF_POWERSCREEN      BIT(9)
#define EF_ANIM01           BIT(10)     // automatically cycle between frames 0 and 1 at 2 hz
#define EF_ANIM23           BIT(11)     // automatically cycle between frames 2 and 3 at 2 hz
#define EF_ANIM_ALL         BIT(12)     // automatically cycle through all frames at 2hz
#define EF_ANIM_ALLFAST     BIT(13)     // automatically cycle through all frames at 10hz
#define EF_FLIES            BIT(14)
#define EF_QUAD             BIT(15)
#define EF_PENT             BIT(16)
#define EF_TELEPORTER       BIT(17)     // particle fountain
#define EF_FLAG1            BIT(18)
#define EF_FLAG2            BIT(19)

// RAFAEL
#define EF_IONRIPPER        BIT(20)
#define EF_GREENGIB         BIT(21)
#define EF_BLUEHYPERBLASTER BIT(22)
#define EF_SPINNINGLIGHTS   BIT(23)
#define EF_PLASMA           BIT(24)
#define EF_TRAP             BIT(25)

//ROGUE
#define EF_TRACKER          BIT(26)
#define EF_DOUBLE           BIT(27)
#define EF_SPHERETRANS      BIT(28)
#define EF_TAGTRAIL         BIT(29)
#define EF_HALF_DAMAGE      BIT(30)
#define EF_TRACKERTRAIL     BIT(31)
//ROGUE

// entity_state_t->morefx flags
//KEX
#define EFX_NONE                0U
#define EFX_DUALFIRE            BIT(0)
#define EFX_HOLOGRAM            BIT(1)
#define EFX_FLASHLIGHT          BIT(2)
#define EFX_BARREL_EXPLODING    BIT(3)
#define EFX_TELEPORTER2         BIT(4)
#define EFX_GRENADE_LIGHT       BIT(5)
//KEX
#define EFX_STEAM               BIT(6)

// entity_state_t->event values
// entity events are for effects that take place relative
// to an existing entities origin.  Very network efficient.
// All muzzle flashes really should be converted to events...
typedef enum {
    EV_NONE,
    EV_ITEM_RESPAWN,
    EV_FOOTSTEP,
    EV_FALL,
    EV_DEATH1,
    EV_DEATH2,
    EV_DEATH3,
    EV_DEATH4,
    EV_PAIN,
    EV_GURP,
    EV_DROWN,
    EV_JUMP,
    EV_PLAYER_TELEPORT,
    EV_OTHER_TELEPORT,
    EV_OTHER_FOOTSTEP,
    EV_LADDER_STEP,
    EV_STAIR_STEP,
    EV_MUZZLEFLASH,
    EV_MUZZLEFLASH2,
    EV_SOUND,
    EV_BERSERK_SLAM,
    EV_GUNCMDR_SLAM,
    EV_RAILTRAIL,
    EV_RAILTRAIL2,
    EV_BUBBLETRAIL,
    EV_BUBBLETRAIL2,
    EV_BFG_LASER,
    EV_BFG_ZAP,
    EV_EARTHQUAKE,
    EV_EARTHQUAKE2,

    EV_SPLASH_UNKNOWN,
    EV_SPLASH_SPARKS,
    EV_SPLASH_BLUE_WATER,
    EV_SPLASH_BROWN_WATER,
    EV_SPLASH_SLIME,
    EV_SPLASH_LAVA,
    EV_SPLASH_BLOOD,
    EV_SPLASH_ELECTRIC_N64,

    EV_BLOOD,
    EV_MORE_BLOOD,
    EV_GREEN_BLOOD,
    EV_GUNSHOT,
    EV_SHOTGUN,
    EV_SPARKS,
    EV_BULLET_SPARKS,
    EV_HEATBEAM_SPARKS,
    EV_HEATBEAM_STEAM,
    EV_SCREEN_SPARKS,
    EV_SHIELD_SPARKS,
    EV_ELECTRIC_SPARKS,
    EV_LASER_SPARKS,
    EV_WELDING_SPARKS,
    EV_TUNNEL_SPARKS,

    EV_EXPLOSION_PLAIN,
    EV_EXPLOSION1,
    EV_EXPLOSION1_NL,
    EV_EXPLOSION1_NP,
    EV_EXPLOSION1_BIG,
    EV_EXPLOSION2,
    EV_EXPLOSION2_NL,
    EV_BLASTER,
    EV_BLASTER2,
    EV_FLECHETTE,
    EV_BLUEHYPERBLASTER,
    EV_GRENADE_EXPLOSION,
    EV_GRENADE_EXPLOSION_WATER,
    EV_ROCKET_EXPLOSION,
    EV_ROCKET_EXPLOSION_WATER,
    EV_BFG_EXPLOSION,
    EV_BFG_EXPLOSION_BIG,
    EV_TRACKER_EXPLOSION,

    EV_POWER_SPLASH,
    EV_BOSSTPORT,
    EV_TELEPORT_EFFECT,
    EV_CHAINFIST_SMOKE,
    EV_NUKEBLAST,
    EV_WIDOWBEAMOUT,
    EV_WIDOWSPLASH,
} entity_event_t;

//
// muzzle flashes / player effects
//
typedef enum {
    MZ_NONE,
    MZ_BLASTER,
    MZ_HYPERBLASTER,
    MZ_MACHINEGUN,
    MZ_SHOTGUN,
    MZ_SSHOTGUN,
    MZ_CHAINGUN1,
    MZ_CHAINGUN2,
    MZ_CHAINGUN3,
    MZ_RAILGUN,
    MZ_ROCKET,
    MZ_GRENADE,
    MZ_BFG,
    MZ_BFG2,
    MZ_LOGIN,
    MZ_LOGOUT,

// RAFAEL
    MZ_IONRIPPER,
    MZ_BLUEHYPERBLASTER,
    MZ_PHALANX,
    MZ_PHALANX2,
// RAFAEL

//ROGUE
    MZ_PROX,
    MZ_ETF_RIFLE,
    MZ_ETF_RIFLE_2,
    MZ_HEATBEAM,
    MZ_BLASTER2,
    MZ_TRACKER,
    MZ_NUKE1,
    MZ_NUKE2,
    MZ_NUKE4,
    MZ_NUKE8,
//ROGUE

    MZ_SILENCED = BIT(7),  // bit flag ORed with one of the above numbers
} player_muzzle_t;

typedef enum {
    SPLASH_UNKNOWN,
    SPLASH_SPARKS,
    SPLASH_BLUE_WATER,
    SPLASH_BROWN_WATER,
    SPLASH_SLIME,
    SPLASH_LAVA,
    SPLASH_BLOOD,
    SPLASH_ELECTRIC_N64, // KEX
} splash_color_t;

// sound channels
// channel 0 never willingly overrides
// other channels (1-7) always override a playing sound on that channel
typedef enum {
    CHAN_AUTO,
    CHAN_WEAPON,
    CHAN_VOICE,
    CHAN_ITEM,
    CHAN_BODY,
    CHAN_AUX,
    CHAN_FOOTSTEP,

    // modifier flags
    CHAN_NO_STEREO      = BIT(3),   // don't use stereo panning
} soundchan_t;

// game print flags
typedef enum {
    PRINT_LOW,          // pickup messages
    PRINT_MEDIUM,       // death messages
    PRINT_HIGH,         // critical messages
    PRINT_CHAT,         // chat messages
    PRINT_TYPEWRITER,
    PRINT_CENTER,
} print_level_t;

// state for coop respawning; used to select which
// message to print for the player this is set on.
typedef enum {
    COOP_RESPAWN_NONE, // no messagee
    COOP_RESPAWN_IN_COMBAT, // player is in combat
    COOP_RESPAWN_BAD_AREA, // player not in a good spot
    COOP_RESPAWN_BLOCKED, // spawning was blocked by something
    COOP_RESPAWN_WAITING, // for players that are waiting to respawn
    COOP_RESPAWN_NO_LIVES, // out of lives, so need to wait until level switch
    COOP_RESPAWN_TOTAL
} coop_respawn_t;

// reserved general CS ranges
enum {
    CONFIG_CTF_MATCH = CS_GENERAL,
    CONFIG_CTF_TEAMINFO,
    CONFIG_CTF_PLAYER_NAME,
    CONFIG_CTF_PLAYER_NAME_END = CONFIG_CTF_PLAYER_NAME + MAX_CLIENTS,

    // nb: offset by 1 since NONE is zero
    CONFIG_COOP_RESPAWN_STRING,
    CONFIG_COOP_RESPAWN_STRING_END = CONFIG_COOP_RESPAWN_STRING + (COOP_RESPAWN_TOTAL - 1),

    // [Paril-KEX] see enum physics_flags_t
    CONFIG_PHYSICS_FLAGS,
    CONFIG_HEALTH_BAR_NAME, // active health bar name

    CONFIG_LAST
};

// player_state->stats[] indexes
typedef enum {
    STAT_HEALTH_ICON = 1,
    STAT_HEALTH,
    STAT_AMMO_ICON,
    STAT_AMMO,
    STAT_ARMOR_ICON,
    STAT_ARMOR,
    STAT_SELECTED_ICON,
    STAT_SELECTED_ITEM,
    STAT_SELECTED_ITEM_NAME,
    STAT_PICKUP_ICON,
    STAT_PICKUP_STRING,
    STAT_TIMER_ICON,
    STAT_TIMER,
    STAT_HELPICON,
    STAT_LAYOUTS,
    STAT_FLASHES,           // cleared each frame, 1 = health, 2 = armor
    STAT_CHASE,
    STAT_SPECTATOR,
    STAT_HITS,
    STAT_DAMAGE,

    // More stats for weapon wheel
    STAT_ACTIVE_WEAPON,
    STAT_ACTIVE_WHEEL_WEAPON,
    STAT_WEAPONS_OWNED,
    STAT_POWERUPS_OWNED,

    STAT_CTF_TEAM1_PIC = STAT_POWERUPS_OWNED + 1,
    STAT_CTF_TEAM1_CAPS,
    STAT_CTF_TEAM2_PIC,
    STAT_CTF_TEAM2_CAPS,
    STAT_CTF_FLAG_PIC,
    STAT_CTF_JOINED_TEAM1_PIC,
    STAT_CTF_JOINED_TEAM2_PIC,
    STAT_CTF_TEAM1_HEADER,
    STAT_CTF_TEAM2_HEADER,
    STAT_CTF_TECH,
    STAT_CTF_ID_VIEW,
    STAT_CTF_MATCH,
    STAT_CTF_ID_VIEW_COLOR,
    STAT_CTF_TEAMINFO,

    // [Paril-KEX] Key display
    STAT_KEY_A = STAT_POWERUPS_OWNED + 1,
    STAT_KEY_B,
    STAT_KEY_C,

    // [Paril-KEX] top of screen coop respawn state
    STAT_COOP_RESPAWN,

    // [Paril-KEX] respawns remaining
    STAT_LIVES,

    // [Paril-KEX]
    STAT_HEALTH_BARS, // two health bar values (0 - inactive, 1 - dead, 2-255 - alive)
} stat_index_t;

typedef struct {
    char        name[MAX_QPATH];
    float       rotate;
    bool        autorotate;
    vec3_t      axis;
} sky_params_t;

void BG_ParseSkyParams(const char *s, sky_params_t *sky);
const char *BG_FormatSkyParams(const sky_params_t *sky);

//==============================================

// pmove_state_t is the information necessary for client side movement
// prediction
typedef enum {
    // can accelerate and turn
    PM_NORMAL,
    PM_GRAPPLE, // [Paril-KEX] pull towards velocity, no gravity
    PM_NOCLIP,
    PM_SPECTATOR,
    // no acceleration or turning
    PM_DEAD,
    PM_GIB,     // different bounding box
    PM_FREEZE
} pmtype_t;

// pmove->pm_flags
#define PMF_NONE            0U
#define PMF_DUCKED          BIT(0)
#define PMF_JUMP_HELD       BIT(1)
#define PMF_ON_GROUND       BIT(2)
#define PMF_TIME_WATERJUMP  BIT(3)      // pm_time is waterjump
#define PMF_TIME_LAND       BIT(4)      // pm_time is time before rejump
#define PMF_TIME_TELEPORT   BIT(5)      // pm_time is non-moving time
#define PMF_NO_PREDICTION   BIT(6)      // temporarily disables prediction (used for grappling hook)

//KEX
#define PMF_ON_LADDER                   BIT(7)
#define PMF_NO_ANGULAR_PREDICTION       BIT(8)
#define PMF_IGNORE_PLAYER_COLLISION     BIT(9)
#define PMF_TIME_TRICK                  BIT(10)
#define PMF_NO_GROUND_SEEK              BIT(11)
//KEX

typedef enum {
    WATER_NONE,
    WATER_FEET,
    WATER_WAIST,
    WATER_UNDER
} water_level_t;

#define MAXTOUCH    32

typedef struct {
    int num;
    trace_t traces[MAXTOUCH];
} touch_list_t;

typedef void (*trace_func_t)(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, unsigned passent, contents_t contentmask);
typedef void (*clip_func_t)(trace_t *tr, const vec3_t start, const vec3_t mins, const vec3_t maxs, const vec3_t end, unsigned clipent, contents_t contentmask);

typedef struct {
    // state (in / out)
    player_state_t  *s;

    // command (in)
    usercmd_t       cmd;

    // results (out)
    touch_list_t    touch;
    vec3_t          mins, maxs;         // bounding box size
    int             groundentitynum;
    contents_t      watertype;
    water_level_t   waterlevel;

    // callbacks to test the world
    trace_func_t    trace;
    clip_func_t     clip;
    contents_t      (*pointcontents)(const vec3_t point);

    // [KEX] results (out)
    bool        jump_sound;
    bool        step_sound;
    float       step_height;
    float       impact_delta;
} pmove_t;

typedef enum {
    GOOD_POSITION,
    STUCK_FIXED,
    NO_GOOD_POSITION
} stuck_result_t;

stuck_result_t G_FixStuckObject_Generic(vec3_t origin, const vec3_t own_mins, const vec3_t own_maxs,
                                        int ignore, contents_t mask, trace_func_t trace_func);

typedef enum {
    PHYSICS_PC = 0,
    PHYSICS_N64_MOVEMENT = BIT(0),
    PHYSICS_PSX_MOVEMENT = BIT(1),
    PHYSICS_PSX_SCALE    = BIT(2),
    PHYSICS_DEATHMATCH   = BIT(3),
} physics_flags_t;

typedef struct {
    float airaccel;
    physics_flags_t physics_flags;
} pm_config_t;

extern pm_config_t pm_config;

// In PSX SP, step-ups aren't allowed
static inline bool PM_AllowStepUp(void)
{
    return !(pm_config.physics_flags & PHYSICS_PSX_MOVEMENT) || (pm_config.physics_flags & PHYSICS_DEATHMATCH);
}

// PSX / N64 can't trick-jump except in DM
static inline bool PM_AllowTrickJump(void)
{
    return !(pm_config.physics_flags & (PHYSICS_N64_MOVEMENT | PHYSICS_PSX_MOVEMENT)) || (pm_config.physics_flags & PHYSICS_DEATHMATCH);
}

// PSX / N64 (single player) require landing before a next jump
static inline bool PM_NeedsLandTime(void)
{
    return (pm_config.physics_flags & (PHYSICS_N64_MOVEMENT | PHYSICS_PSX_MOVEMENT)) && !(pm_config.physics_flags & PHYSICS_DEATHMATCH);
}

// can't crouch in single player N64
static inline bool PM_CrouchingDisabled(void)
{
    return (pm_config.physics_flags & PHYSICS_N64_MOVEMENT) && !(pm_config.physics_flags & PHYSICS_DEATHMATCH);
}

void PM_ClipVelocity(const vec3_t in, const vec3_t normal, vec3_t out, float overbounce);

void PM_RecordTrace(touch_list_t *touch, const trace_t *tr);

void PM_StepSlideMove_Generic(vec3_t origin, vec3_t velocity, float frametime, const vec3_t mins, const vec3_t maxs,
                              int passent, contents_t mask, touch_list_t *touch, bool has_time, trace_func_t trace_func);

void BG_Pmove(pmove_t *pmove);

void G_AddBlend(float r, float g, float b, float a, vec4_t v_blend);

void vectoangles(const vec3_t value1, vec3_t angles);

const char *BG_EventName(entity_event_t event);
