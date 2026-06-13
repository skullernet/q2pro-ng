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
// Config strings are a general means of communication from the server to all
// connected clients. All config strings except the very first one (CS_NAME)
// are private to bgame and not interpreted by engine. Each config string can
// be at most MAX_NET_STRING characters.
//

#define MAX_ITEMS           256
#define MAX_CLIENTWEAPONS   256     // PGM -- upped from 16 to fit the chainfist vwep
#define MAX_WHEEL_ITEMS     32

typedef enum {
// generic parameters
    CS_CDTRACK = 1,
    CS_SKY,
    CS_STATUSBAR,
    CS_AIRACCEL,
    CS_PHYSICS_FLAGS,
    CS_MAXCLIENTS,

// generic configstring ranges
    CS_MODELS               = 32,
    CS_SOUNDS               = CS_MODELS + MAX_MODELS,
    CS_IMAGES               = CS_SOUNDS + MAX_SOUNDS,
    CS_LIGHTS               = CS_IMAGES + MAX_IMAGES,
    CS_ITEMS                = CS_LIGHTS + MAX_LIGHTSTYLES,
    CS_CLIENTWEAPONS        = CS_ITEMS + MAX_ITEMS,
    CS_PLAYERSKINS          = CS_CLIENTWEAPONS + MAX_CLIENTWEAPONS,
    CS_WHEEL_WEAPONS        = CS_PLAYERSKINS + MAX_CLIENTS,
    CS_WHEEL_AMMO           = CS_WHEEL_WEAPONS + MAX_WHEEL_ITEMS,
    CS_WHEEL_POWERUPS       = CS_WHEEL_AMMO + MAX_WHEEL_ITEMS,
    CS_WHEEL_POWERUPS_LAST  = CS_WHEEL_POWERUPS + MAX_WHEEL_ITEMS - 1,

// CTF stats
    CS_CTF_MATCH,
    CS_CTF_TEAMINFO,
    CS_CTF_PLAYER_NAME,
    CS_CTF_PLAYER_NAME_LAST = CS_CTF_PLAYER_NAME + MAX_CLIENTS - 1,

// coop respawn strings
    CS_COOP_RESPAWN_IN_COMBAT,  // player is in combat
    CS_COOP_RESPAWN_BAD_AREA,   // player not in a good spot
    CS_COOP_RESPAWN_BLOCKED,    // spawning was blocked by something
    CS_COOP_RESPAWN_WAITING,    // for players that are waiting to respawn
    CS_COOP_RESPAWN_NO_LIVES,   // out of lives, so need to wait until level switch

// misc
    CS_HEALTH_BAR_NAME, // active health bar name

    CS_END
} cs_index_t;

_Static_assert(CS_END <= MAX_CONFIGSTRINGS, "Too many configstrings");

// STAT_LAYOUTS flags
typedef enum : uint32_t {
    LAYOUTS_NONE            = 0U,
    LAYOUTS_LAYOUT          = BIT(0),
    LAYOUTS_INVENTORY       = BIT(1),
    LAYOUTS_HIDE_HUD        = BIT(2),
    LAYOUTS_INTERMISSION    = BIT(3),
    LAYOUTS_HELP            = BIT(4),
    LAYOUTS_HIDE_CROSSHAIR  = BIT(5),
} layout_flags_t;

// uf flags
typedef enum : uint32_t {
    UF_NONE             = 0U,
    UF_AUTOSCREENSHOT   = BIT(0),
    UF_AUTORECORD       = BIT(1),
    UF_LOCALFOV         = BIT(2),
    UF_MUTE_PLAYERS     = BIT(3),
    UF_MUTE_OBSERVERS   = BIT(4),
    UF_MUTE_MISC        = BIT(5),
    UF_PLAYERFOV        = BIT(6),
} user_flags_t;

//==============================================

// entity_state_t->effects
// Effects are things handled on the client side (lights, particles, frame animations)
// that happen constantly on the given entity.
// An entity that has effects will be sent to the client
// even if it has a zero index model.
typedef enum : uint32_t {
    EF_NONE             = 0U,
    EF_ROTATE           = BIT(0),   // rotate (bonus items)
    EF_GIB              = BIT(1),   // leave a trail
    EF_BOB              = BIT(2),   // used by KEX
    EF_BLASTER          = BIT(3),   // redlight + trail
    EF_ROCKET           = BIT(4),   // redlight + trail
    EF_GRENADE          = BIT(5),
    EF_HYPERBLASTER     = BIT(6),
    EF_BFG              = BIT(7),
    EF_COLOR_SHELL      = BIT(8),
    EF_POWERSCREEN      = BIT(9),
    EF_ANIM01           = BIT(10),  // automatically cycle between frames 0 and 1 at 2 hz
    EF_ANIM23           = BIT(11),  // automatically cycle between frames 2 and 3 at 2 hz
    EF_ANIM_ALL         = BIT(12),  // automatically cycle through all frames at 2hz
    EF_ANIM_ALLFAST     = BIT(13),  // automatically cycle through all frames at 10hz
    EF_FLIES            = BIT(14),
    EF_QUAD             = BIT(15),
    EF_PENT             = BIT(16),
    EF_TELEPORTER       = BIT(17),  // particle fountain
    EF_FLAG1            = BIT(18),
    EF_FLAG2            = BIT(19),

//RAFAEL
    EF_IONRIPPER        = BIT(20),
    EF_GREENGIB         = BIT(21),
    EF_BLUEHYPERBLASTER = BIT(22),
    EF_SPINNINGLIGHTS   = BIT(23),
    EF_PLASMA           = BIT(24),
    EF_TRAP             = BIT(25),
//RAFAEL

//ROGUE
    EF_TRACKER          = BIT(26),
    EF_DOUBLE           = BIT(27),
    EF_SPHERETRANS      = BIT(28),
    EF_TAGTRAIL         = BIT(29),
    EF_HALF_DAMAGE      = BIT(30),
    EF_TRACKERTRAIL     = BIT(31),
//ROGUE

    EF_TRAIL_MASK       = EF_ROCKET | EF_BLASTER | EF_HYPERBLASTER | EF_GIB | EF_GRENADE |
                          EF_FLIES | EF_BFG | EF_TRAP | EF_FLAG1 | EF_FLAG2 | EF_TAGTRAIL |
                          EF_TRACKERTRAIL | EF_TRACKER | EF_GREENGIB | EF_IONRIPPER |
                          EF_BLUEHYPERBLASTER | EF_PLASMA,

    EF_SHELL_MASK       = EF_COLOR_SHELL | EF_PENT | EF_QUAD | EF_DOUBLE | EF_HALF_DAMAGE,
} effects_t;

// entity_state_t->morefx flags
typedef enum : uint32_t {
//KEX
    EFX_NONE                = 0U,
    EFX_DUALFIRE            = BIT(0),
    EFX_HOLOGRAM            = BIT(1),
    EFX_FLASHLIGHT          = BIT(2),
    EFX_BARREL_EXPLODING    = BIT(3),
    EFX_TELEPORTER2         = BIT(4),
    EFX_GRENADE_LIGHT       = BIT(5),
//KEX
    EFX_STEAM               = BIT(6),
} morefx_t;

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
    EV_POSITIONED_SOUND,
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
    EV_NAILS,
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
    EV_ENFORCER_BOLT,
    EV_HELLKNIGHT_MAGIC,
    EV_WIZARD_SPIT,

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

    STAT_END
} stat_index_t;

_Static_assert(STAT_END <= MAX_STATS, "Too many stats");

typedef struct {
    char        name[MAX_QPATH];
    float       rotate;
    bool        autorotate;
    vec3_t      axis;
} sky_params_t;

void BG_ParseSkyParams(const char *s, sky_params_t *sky);
const char *BG_FormatSkyParams(const sky_params_t *sky);

static inline player_fog_t BG_LerpFog(player_fog_t a, player_fog_t b, float t)
{
    return (player_fog_t) {
        .color      = Vec3_Lerp(a.color, b.color, t),
        .density    = Q_lerpf(a.density, b.density, t),
        .sky_factor = Q_lerpf(a.sky_factor, b.sky_factor, t)
    };
}

static inline player_heightfog_t BG_LerpHeightFog(player_heightfog_t a, player_heightfog_t b, float t)
{
    return (player_heightfog_t) {
        .start = {
            .color = Vec3_Lerp(a.start.color, b.start.color, t),
            .dist = Q_lerpf(a.start.dist, b.start.dist, t)
        },
        .end = {
            .color = Vec3_Lerp(a.end.color, b.end.color, t),
            .dist = Q_lerpf(a.end.dist, b.end.dist, t)
        },
        .density = Q_lerpf(a.density, b.density, t),
        .falloff = Q_lerpf(a.falloff, b.falloff, t)
    };
}

//==============================================

// pmove_state_t is the information necessary for client side movement
// prediction
typedef enum {
    // can accelerate and turn
    PM_NORMAL,
    PM_GRAPPLE, // [Paril-KEX] pull towards velocity, no gravity
    PM_NOCLIP,
    PM_SPECTATOR,   // only clip to world
    PM_FLY,         // normal clipmask

    // no acceleration or turning
    PM_DEAD,
    PM_GIB,     // different bounding box
    PM_FREEZE
} pmove_type_t;

// pmove->pm_flags
typedef enum : uint32_t {
    PMF_NONE                = 0U,
    PMF_DUCKED              = BIT(0),
    PMF_JUMP_HELD           = BIT(1),
    PMF_ON_GROUND           = BIT(2),
    PMF_ON_LADDER           = BIT(3),   // signal to game that we are on a ladder
    PMF_TIME_WATERJUMP      = BIT(4),   // pm_time is waterjump
    PMF_TIME_LAND           = BIT(5),   // pm_time is time before rejump
    PMF_TIME_TELEPORT       = BIT(6),   // pm_time is non-moving time
    PMF_TIME_TRICK          = BIT(7),   // pm_time is trick jump time
    PMF_NO_PREDICTION       = BIT(8),   // temporarily disables prediction
    PMF_NO_GROUND_SEEK      = BIT(9),   // temporarily disables ground seeking
    PMF_NO_PLAYER_COLLISION = BIT(10),  // don't collide with other players

    PMF_TIME_MASK = PMF_TIME_WATERJUMP | PMF_TIME_LAND | PMF_TIME_TELEPORT | PMF_TIME_TRICK,
    PMF_EXTERNAL_MASK = PMF_NO_PREDICTION | PMF_NO_GROUND_SEEK | PMF_NO_PLAYER_COLLISION,
} pmove_flags_t;

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

typedef void (*trace_func_t)(trace_t *tr, const trace_args_t *args);

typedef struct {
    // state (in / out)
    player_state_t  *s;

    // command (in)
    usercmd_t       cmd;

    // results (out)
    touch_list_t    touch;
    box3_t          box;        // bounding box size
    int             groundentitynum;
    contents_t      watertype;
    water_level_t   waterlevel;

    // callbacks to test the world
    trace_func_t    trace;
    trace_func_t    clip;
    contents_t      (*pointcontents)(vec3_t point);

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

stuck_result_t PM_FixStuckObject_Generic(vec3_t *origin, box3_t own, int ignore,
                                         contents_t mask, trace_func_t trace_func);

typedef enum : uint32_t {
    PHYSICS_PC           = 0U,
    PHYSICS_N64_MOVEMENT = BIT(0),
    PHYSICS_PSX_MOVEMENT = BIT(1),
    PHYSICS_PSX_SCALE    = BIT(2),
    PHYSICS_DEATHMATCH   = BIT(3),
    PHYSICS_QW_MOVEMENT  = BIT(4),
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

vec3_t PM_ClipVelocity(vec3_t in, vec3_t normal, float overbounce);

void PM_RecordTrace(touch_list_t *touch, const trace_t *tr);

void PM_StepSlideMove_Generic(vec3_t *origin, vec3_t *velocity, float frametime, box3_t box, int passent,
                              contents_t mask, touch_list_t *touch, bool has_time, trace_func_t trace_func);

void PM_ClampAngles(player_state_t *s, const usercmd_t *cmd);

void BG_Pmove(pmove_t *pmove);

void BG_AddBlend(float r, float g, float b, float a, vec4_t *v_blend);

const char *BG_EventName(entity_event_t event);
