// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#include "g_local.h"

game_locals_t  game;
level_locals_t level;

#ifndef Q2_VM
const game_import_t *gi;
#endif

bool use_psx_assets;

spawn_temp_t   st;

const trace_t null_trace;

edict_t   g_edicts[MAX_EDICTS];
gclient_t g_clients[MAX_CLIENTS];

vm_cvar_t developer;
vm_cvar_t deathmatch;
vm_cvar_t coop;
vm_cvar_t skill;
vm_cvar_t fraglimit;
vm_cvar_t timelimit;
// ZOID
vm_cvar_t capturelimit;
vm_cvar_t g_quick_weapon_switch;
vm_cvar_t g_instant_weapon_switch;
// ZOID
vm_cvar_t password;
vm_cvar_t spectator_password;
vm_cvar_t needpass;
vm_cvar_t maxspectators;
vm_cvar_t g_select_empty;
vm_cvar_t sv_dedicated;
vm_cvar_t sv_running;
vm_cvar_t sv_fps;

vm_cvar_t filterban;

vm_cvar_t sv_maxvelocity;
vm_cvar_t sv_gravity;

vm_cvar_t sv_rollspeed;
vm_cvar_t sv_rollangle;

vm_cvar_t sv_cheats;
vm_cvar_t sv_maxclients;

vm_cvar_t g_debug_monster_paths;
vm_cvar_t g_debug_monster_kills;

vm_cvar_t flood_msgs;
vm_cvar_t flood_persecond;
vm_cvar_t flood_waitdelay;

vm_cvar_t sv_stopspeed; // PGM     (this was a define in g_phys.c)

vm_cvar_t g_strict_saves;

// ROGUE cvars
vm_cvar_t gamerules;
vm_cvar_t huntercam;
vm_cvar_t g_dm_strong_mines;
vm_cvar_t g_dm_random_items;
// ROGUE

// [Kex]
vm_cvar_t g_instagib;
vm_cvar_t g_coop_player_collision;
vm_cvar_t g_coop_squad_respawn;
vm_cvar_t g_coop_enable_lives;
vm_cvar_t g_coop_num_lives;
vm_cvar_t g_coop_instanced_items;
vm_cvar_t g_allow_grapple;
vm_cvar_t g_grapple_fly_speed;
vm_cvar_t g_grapple_pull_speed;
vm_cvar_t g_grapple_damage;
vm_cvar_t g_coop_health_scaling;
vm_cvar_t g_weapon_respawn_time;

// dm"flags"
vm_cvar_t g_no_health;
vm_cvar_t g_no_items;
vm_cvar_t g_dm_weapons_stay;
vm_cvar_t g_dm_no_fall_damage;
vm_cvar_t g_dm_instant_items;
vm_cvar_t g_dm_same_level;
vm_cvar_t g_friendly_fire;
vm_cvar_t g_dm_force_respawn;
vm_cvar_t g_dm_force_respawn_time;
vm_cvar_t g_dm_spawn_farthest;
vm_cvar_t g_no_armor;
vm_cvar_t g_dm_allow_exit;
vm_cvar_t g_infinite_ammo;
vm_cvar_t g_dm_no_quad_drop;
vm_cvar_t g_dm_no_quadfire_drop;
vm_cvar_t g_no_mines;
vm_cvar_t g_dm_no_stack_double;
vm_cvar_t g_no_nukes;
vm_cvar_t g_no_spheres;
vm_cvar_t g_teamplay_armor_protect;
vm_cvar_t g_allow_techs;
vm_cvar_t g_start_items;
vm_cvar_t g_map_list;
vm_cvar_t g_map_list_shuffle;

vm_cvar_t sv_airaccelerate;
vm_cvar_t g_override_physics_flags;

vm_cvar_t g_damage_scale;
vm_cvar_t g_disable_player_collision;
vm_cvar_t ai_damage_scale;
vm_cvar_t ai_model_scale;
vm_cvar_t ai_allow_dm_spawn;
vm_cvar_t ai_movement_disabled;
vm_cvar_t g_monster_footsteps;
vm_cvar_t g_auto_save_min_time;

static const vm_cvar_reg_t g_cvars[] = {
    { &developer, "developer", "0", 0 },
    { &deathmatch, "deathmatch", "0", CVAR_LATCH },
    { &coop, "coop", "0", CVAR_LATCH },
    { &teamplay, "teamplay", "0", CVAR_LATCH },

    // FIXME: sv_ prefix is wrong for these
    { &sv_rollspeed, "sv_rollspeed", "200", 0 },
    { &sv_rollangle, "sv_rollangle", "2", 0 },
    { &sv_maxvelocity, "sv_maxvelocity", "2000", 0 },
    { &sv_gravity, "sv_gravity", "800", 0 },

    { &sv_stopspeed, "sv_stopspeed", "100", 0 }, // PGM - was #define in g_phys.c

    // ROGUE
    { &huntercam, "huntercam", "1", CVAR_SERVERINFO | CVAR_LATCH },
    { &g_dm_strong_mines, "g_dm_strong_mines", "0", 0 },
    { &g_dm_random_items, "g_dm_random_items", "0", 0 },
    // ROGUE

    // [Kex] Instagib
    { &g_instagib, "g_instagib", "0", 0 },

    // [Paril-KEX]
    { &g_coop_player_collision, "g_coop_player_collision", "0", CVAR_LATCH },
    { &g_coop_squad_respawn, "g_coop_squad_respawn", "1", CVAR_LATCH },
    { &g_coop_enable_lives, "g_coop_enable_lives", "0", CVAR_LATCH },
    { &g_coop_num_lives, "g_coop_num_lives", "2", CVAR_LATCH },
    { &g_coop_instanced_items, "g_coop_instanced_items", "1", CVAR_LATCH },
    { &g_allow_grapple, "g_allow_grapple", "auto", 0 },
    { &g_grapple_fly_speed, "g_grapple_fly_speed", STRINGIFY(CTF_DEFAULT_GRAPPLE_SPEED), 0 },
    { &g_grapple_pull_speed, "g_grapple_pull_speed", STRINGIFY(CTF_DEFAULT_GRAPPLE_PULL_SPEED), 0 },
    { &g_grapple_damage, "g_grapple_damage", "10", 0 },

    { &g_debug_monster_paths, "g_debug_monster_paths", "0", 0 },
    { &g_debug_monster_kills, "g_debug_monster_kills", "0", CVAR_LATCH },

    // noset vars
    { &sv_dedicated, "dedicated", "0", CVAR_NOSET },
    { &sv_running, "sv_running", NULL, 0 },
    { &sv_fps, "sv_fps", NULL, 0 },

    // latched vars
    { &sv_cheats, "cheats", "0", CVAR_SERVERINFO | CVAR_LATCH },
    { &sv_maxclients, "maxclients", "8", CVAR_SERVERINFO | CVAR_LATCH },
    { NULL, "gamename", GAMEVERSION, CVAR_SERVERINFO | CVAR_LATCH },

    { &maxspectators, "maxspectators", "4", CVAR_SERVERINFO },
    { &skill, "skill", "1", CVAR_LATCH },
    { &gamerules, "gamerules", "0", CVAR_LATCH }, // PGM

    // change anytime vars
    { &fraglimit, "fraglimit", "0", CVAR_SERVERINFO },
    { &timelimit, "timelimit", "0", CVAR_SERVERINFO },
    // ZOID
    { &capturelimit, "capturelimit", "0", CVAR_SERVERINFO },
    { &g_quick_weapon_switch, "g_quick_weapon_switch", "1", 0 },
    { &g_instant_weapon_switch, "g_instant_weapon_switch", "0", 0 },
    // ZOID
    { &password, "password", "", CVAR_USERINFO },
    { &spectator_password, "spectator_password", "", CVAR_USERINFO },
    { &needpass, "needpass", "0", CVAR_SERVERINFO },
    { &filterban, "filterban", "1", 0 },

    { &g_select_empty, "g_select_empty", "0", CVAR_ARCHIVE },

    // flood control
    { &flood_msgs, "flood_msgs", "4", 0 },
    { &flood_persecond, "flood_persecond", "4", 0 },
    { &flood_waitdelay, "flood_waitdelay", "10", 0 },

    { &g_strict_saves, "g_strict_saves", "1", 0 },

    { &sv_airaccelerate, "sv_airaccelerate", "0", 0 },
    { &g_override_physics_flags, "g_override_physics_flags", "-1", 0 },

    { &g_damage_scale, "g_damage_scale", "1", 0 },
    { &g_disable_player_collision, "g_disable_player_collision", "0", 0 },
    { &ai_damage_scale, "ai_damage_scale", "1", 0 },
    { &ai_model_scale, "ai_model_scale", "0", 0 },
    { &ai_allow_dm_spawn, "ai_allow_dm_spawn", "0", 0 },
    { &ai_movement_disabled, "ai_movement_disabled", "0", 0 },
    { &g_monster_footsteps, "g_monster_footsteps", "1", 0 },
    { &g_auto_save_min_time, "g_auto_save_min_time", "-1", 0 },

    { &g_coop_health_scaling, "g_coop_health_scaling", "0", CVAR_LATCH },
    { &g_weapon_respawn_time, "g_weapon_respawn_time", "30", 0 },

    // dm "flags"
    { &g_no_health, "g_no_health", "0", 0 },
    { &g_no_items, "g_no_items", "0", 0 },
    { &g_dm_weapons_stay, "g_dm_weapons_stay", "0", CVAR_LATCH },
    { &g_dm_no_fall_damage, "g_dm_no_fall_damage", "0", 0 },
    { &g_dm_instant_items, "g_dm_instant_items", "1", 0 },
    { &g_dm_same_level, "g_dm_same_level", "0", 0 },
    { &g_friendly_fire, "g_friendly_fire", "0", 0 },
    { &g_dm_force_respawn, "g_dm_force_respawn", "0", 0 },
    { &g_dm_force_respawn_time, "g_dm_force_respawn_time", "0", 0 },
    { &g_dm_spawn_farthest, "g_dm_spawn_farthest", "1", 0 },
    { &g_no_armor, "g_no_armor", "0", 0 },
    { &g_dm_allow_exit, "g_dm_allow_exit", "0", 0 },
    { &g_infinite_ammo, "g_infinite_ammo", "0", CVAR_LATCH },
    { &g_dm_no_quad_drop, "g_dm_no_quad_drop", "0", 0 },
    { &g_dm_no_quadfire_drop, "g_dm_no_quadfire_drop", "0", 0 },
    { &g_no_mines, "g_no_mines", "0", 0 },
    { &g_dm_no_stack_double, "g_dm_no_stack_double", "0", 0 },
    { &g_no_nukes, "g_no_nukes", "0", 0 },
    { &g_no_spheres, "g_no_spheres", "0", 0 },
    { &g_teamplay_force_join, "g_teamplay_force_join", "0", 0 },
    { &g_teamplay_armor_protect, "g_teamplay_armor_protect", "0", 0 },
    { &g_allow_techs, "g_allow_techs", "auto", 0 },

    { &g_start_items, "g_start_items", "", CVAR_LATCH },
    { &g_map_list, "g_map_list", "", 0 },
    { &g_map_list_shuffle, "g_map_list_shuffle", "0", 0 },
};

/*
============
PreInitGame

This will be called when the dll is first loaded, which
only happens when a new game is started or a save game
is loaded.

Called at early initialization stage to allow the game modify cvars like
maxclients, etc. Game is only allowed to register/set cvars at this point.
============
*/
qvm_exported void G_PreInit(void)
{
    for (int i = 0; i < q_countof(g_cvars); i++) {
        const vm_cvar_reg_t *reg = &g_cvars[i];
        trap_Cvar_Register(reg->var, reg->name, reg->default_string, reg->flags);
    }

    if (skill.integer < 0)
        trap_Cvar_Set("skill", "0");
    else if (skill.integer > 3)
        trap_Cvar_Set("skill", "3");

    if (coop.integer && deathmatch.integer) {
        G_Printf("Deathmatch and Coop both set, disabling Coop\n");
        trap_Cvar_Set("coop", "0");
    }

    // dedicated servers can't be single player and are usually DM
    // so unless they explicitly set coop, force it to deathmatch
    if (sv_dedicated.integer && !coop.integer)
        trap_Cvar_Set("deathmatch", "1");

    // ZOID
    CTFInit();
    // ZOID

    // ZOID
    // This gamemode only supports deathmatch
    if (ctf.integer) {
        if (!deathmatch.integer) {
            G_Printf("Forcing deathmatch.\n");
            trap_Cvar_Set("deathmatch", "1");
        }
        // force coop off
        if (coop.integer)
            trap_Cvar_Set("coop", "0");
        // force tdm off
        if (teamplay.integer)
            trap_Cvar_Set("teamplay", "0");
    }
    if (teamplay.integer) {
        if (!deathmatch.integer) {
            G_Printf("Forcing deathmatch.\n");
            trap_Cvar_Set("deathmatch", "1");
        }
        // force coop off
        if (coop.integer)
            trap_Cvar_Set("coop", "0");
    }
    // ZOID
 
    // init maxclients
    if (deathmatch.integer) {
        if (sv_maxclients.integer <= 1)
            trap_Cvar_Set("maxclients", "8");
    } else if (coop.integer) {
        if (sv_maxclients.integer <= 1)
            trap_Cvar_Set("maxclients", "4");
    } else {    // non-deathmatch, non-coop is one player
        trap_Cvar_Set("maxclients", "1");
    }

    if (gamerules.integer != RDM_TAG && gamerules.integer != RDM_DEATHBALL)
        trap_Cvar_Set("gamerules", "0");
}

/*
============
InitGame

Called after PreInitGame when the game has set up cvars.
============
*/
qvm_exported void G_Init(void)
{
    G_Printf("==== InitGame ====\n");

    // seed RNG
    Q_srand(trap_RealTime());

    // items
    InitItems();

    // initialize all clients for this game
    game.maxclients = sv_maxclients.integer;
    level.num_edicts = game.maxclients;

    // initialize all entities for this game
    trap_LocateGameData(g_edicts, sizeof(g_edicts[0]), g_clients, sizeof(g_clients[0]));
    trap_SetNumEdicts(level.num_edicts);

    // variable FPS support
    Q_assert_soft(sv_fps.integer >= 10);
    game.tick_rate = sv_fps.integer;
    game.frame_time = 1000 / game.tick_rate;
    game.frame_time_sec = 1.0f / game.tick_rate;

    //======
    // ROGUE
    if (gamerules.integer)
        InitGameRules(); // if there are game rules to set up, do so now.
    // ROGUE
    //======

    G_LoadL10nFile();

    char buf[MAX_QPATH];
    trap_Cvar_VariableString("game", buf, sizeof(buf));
    use_psx_assets = !strncmp(buf, "psx", 3);
}

//===================================================================

qvm_exported void G_Shutdown(void)
{
    G_Printf("==== ShutdownGame ====\n");

    memset(&game, 0, sizeof(game));

    G_FreeMemory();
    G_FreeL10nFile();
}

qvm_exported void G_RestartFilesystem(void)
{
    G_LoadL10nFile();
}

#ifndef Q2_VM

/*
=================
GetGameAPI

Returns a pointer to the structure with all entry points
and global variables
=================
*/
q_exported const game_export_t *GetGameAPI(const game_import_t *import)
{
    static const game_export_t ge = {
        .apiversion = GAME_API_VERSION,
        .structsize = sizeof(ge),

        .PreInit = G_PreInit,
        .Init = G_Init,
        .Shutdown = G_Shutdown,
        .SpawnEntities = G_SpawnEntities,

        .CanSave = G_CanSave,
        .WriteGame = G_WriteGame,
        .ReadGame = G_ReadGame,
        .WriteLevel = G_WriteLevel,
        .ReadLevel = G_ReadLevel,

        .ClientThink = G_ClientThink,
        .ClientConnect = G_ClientConnect,
        .ClientUserinfoChanged = G_ClientUserinfoChanged,
        .ClientDisconnect = G_ClientDisconnect,
        .ClientBegin = G_ClientBegin,
        .ClientCommand = G_ClientCommand,

        .RunFrame = G_RunFrame,
        .PrepFrame = G_PrepFrame,

        .ServerCommand = G_ServerCommand,
        .RestartFilesystem = G_RestartFilesystem,
    };

    gi = import;
    return &ge;
}

#endif

//======================================================================

/*
=================
ClientEndServerFrames
=================
*/
static void ClientEndServerFrames(void)
{
    edict_t *ent;

    // calc the player views now that all pushing
    // and damage has been added
    for (int i = 0; i < game.maxclients; i++) {
        ent = g_edicts + i;
        if (!ent->r.inuse || !ent->client)
            continue;
        ClientEndServerFrame(ent);
    }
}

/*
=================
CreateTargetChangeLevel

Returns the created target changelevel
=================
*/
static edict_t *CreateTargetChangeLevel(const char *map)
{
    edict_t *ent;

    ent = G_Spawn();
    ent->classname = "target_changelevel";
    if (map != level.nextmap)
        Q_strlcpy(level.nextmap, map, sizeof(level.nextmap));
    ent->map = level.nextmap;
    return ent;
}

/*
=================
EndDMLevel

The timelimit or fraglimit has been exceeded
=================
*/
void EndDMLevel(void)
{
    edict_t *ent;

    // stay on same level flag
    if (g_dm_same_level.integer) {
        BeginIntermission(CreateTargetChangeLevel(level.mapname));
        return;
    }

    if (*level.forcemap) {
        BeginIntermission(CreateTargetChangeLevel(level.forcemap));
        return;
    }

    char buffer[MAX_STRING_CHARS];
    trap_Cvar_VariableString("g_map_list", buffer, sizeof(buffer));

    // see if it's in the map list
    if (*buffer) {
        const char *str = buffer;
        char first_map[MAX_QPATH];
        char *map;

        first_map[0] = 0;
        while (1) {
            map = COM_Parse(&str);
            if (!*map)
                break;

            if (Q_strcasecmp(map, level.mapname) == 0) {
                // it's in the list, go to the next one
                map = COM_Parse(&str);
                if (!*map) {
                    // end of list, go to first one
                    if (!first_map[0]) { // there isn't a first one, same level
                        BeginIntermission(CreateTargetChangeLevel(level.mapname));
                        return;
                    } else {
#if 0
                        // [Paril-KEX] re-shuffle if necessary
                        if (g_map_list_shuffle.integer) {
                            auto values = str_split(g_map_list->string, ' ');

                            if (values.size() == 1) {
                                // meh
                                BeginIntermission(CreateTargetChangeLevel(level.mapname));
                                return;
                            }

                            std::shuffle(values.begin(), values.end(), mt_rand);

                            // if the current map is the map at the front, push it to the end
                            if (values[0] == level.mapname)
                                std::swap(values[0], values[values.size() - 1]);

                            trap_Cvar_ForceSet("g_map_list", fmt::format("{}", join_strings(values, " ")).data());

                            BeginIntermission(CreateTargetChangeLevel(values[0].c_str()));
                            return;
                        }
#endif

                        BeginIntermission(CreateTargetChangeLevel(first_map));
                        return;
                    }
                } else {
                    BeginIntermission(CreateTargetChangeLevel(map));
                    return;
                }
            }
            if (!first_map[0])
                Q_strlcpy(first_map, map, sizeof(first_map));
        }
    }

    if (level.nextmap[0]) { // go to a specific map
        BeginIntermission(CreateTargetChangeLevel(level.nextmap));
        return;
    }

    // search for a changelevel
    ent = G_Find(NULL, FOFS(classname), "target_changelevel");

    if (!ent) {
        // the map designer didn't include a changelevel,
        // so create a fake ent that goes back to the same level
        BeginIntermission(CreateTargetChangeLevel(level.mapname));
        return;
    }

    BeginIntermission(ent);
}

/*
=================
CheckNeedPass
=================
*/
static void CheckNeedPass(void)
{
    int need;

    // if password or spectator_password has changed, update needpass
    // as needed
    if (password.modified || spectator_password.modified) {
        need = 0;

        if (*password.string && Q_strcasecmp(password.string, "none"))
            need |= 1;
        if (*spectator_password.string && Q_strcasecmp(spectator_password.string, "none"))
            need |= 2;

        trap_Cvar_Set("needpass", va("%d", need));
        password.modified = spectator_password.modified = false;
    }
}

/*
=================
CheckDMRules
=================
*/
static void CheckDMRules(void)
{
    gclient_t *cl;

    if (level.intermissiontime)
        return;

    if (!deathmatch.integer)
        return;

    // ZOID
    if (ctf.integer && CTFCheckRules()) {
        EndDMLevel();
        return;
    }
    if (CTFInMatch())
        return; // no checking in match mode
    // ZOID

    //=======
    // ROGUE
    if (gamerules.integer && DMGame.CheckDMRules) {
        if (DMGame.CheckDMRules())
            return;
    }
    // ROGUE
    //=======

    if (timelimit.value) {
        if (level.time >= SEC(timelimit.value * 60)) {
            G_ClientPrintf(NULL, PRINT_HIGH, "Time limit hit.\n");
            EndDMLevel();
            return;
        }
    }

    if (fraglimit.integer) {
        // [Paril-KEX]
        if (teamplay.integer) {
            CheckEndTDMLevel();
            return;
        }

        for (int i = 0; i < game.maxclients; i++) {
            cl = g_clients + i;
            if (!g_edicts[i].r.inuse)
                continue;

            if (cl->resp.score >= fraglimit.integer) {
                G_ClientPrintf(NULL, PRINT_HIGH, "Frag limit hit.\n");
                EndDMLevel();
                return;
            }
        }
    }
}

/*
=============
ExitLevel
=============
*/
static void ExitLevel(void)
{
    // [Paril-KEX] N64 fade
    if (level.intermission_fade) {
        level.intermission_fade_time = level.time + SEC(1.3f);
        level.intermission_fading = true;
        return;
    }

    if (!level.intermission_fading)
        ClientEndServerFrames();

    level.exitintermission = 0;
    level.intermissiontime = 0;

    // [Paril-KEX] support for intermission completely wiping players
    // back to default stuff
    if (level.intermission_clear) {
        level.intermission_clear = false;

        for (int i = 0; i < game.maxclients; i++) {
            gclient_t *client = &g_clients[i];

            // [Kex] Maintain user info to keep the player skin.
            char userinfo[MAX_INFO_STRING];
            Q_strlcpy(userinfo, client->pers.userinfo, sizeof(userinfo));

            memset(&client->pers, 0, sizeof(client->pers));
            memset(&client->resp.coop_respawn, 0, sizeof(client->resp.coop_respawn));
            g_edicts[i].health = 0; // this should trip the power armor, etc to reset as well

            Q_strlcpy(client->pers.userinfo, userinfo, sizeof(client->pers.userinfo));
            Q_strlcpy(client->resp.coop_respawn.userinfo, userinfo, sizeof(client->resp.coop_respawn.userinfo));
        }
    }

    // [Paril-KEX] end of unit, so clear level trackers
    if (level.intermission_eou) {
        memset(game.level_entries, 0, sizeof(game.level_entries));

        // give all players their lives back
        if (g_coop_enable_lives.integer) {
            for (int i = 0; i < game.maxclients; i++) {
                if (g_edicts[i].r.inuse)
                    g_clients[i].pers.lives = g_coop_num_lives.integer + 1;
            }
        }
    }

    if (CTFNextMap())
        return;

    if (level.changemap == NULL) {
        G_Error("Got null changemap when trying to exit level. Was a trigger_changelevel configured correctly?");
        return;
    }

    // for N64 mainly, but if we're directly changing to "victorXXX.pcx" then
    // end game
    int start_offset = (level.changemap[0] == '*' ? 1 : 0);

    if (strlen(level.changemap) > (6 + start_offset) &&
        !Q_strncasecmp(level.changemap + start_offset, "victor", 6) &&
        !Q_strncasecmp(level.changemap + strlen(level.changemap) - 4, ".pcx", 4))
        trap_AddCommandString(va("gamemap \"!%s\"\n", level.changemap));
    else
        trap_AddCommandString(va("gamemap \"%s\"\n", level.changemap));

    level.changemap = NULL;
}

static void G_CheckCvars(void)
{
    if (sv_airaccelerate.modified) {
        trap_SetConfigstring(CS_AIRACCEL, va("%d", sv_airaccelerate.integer));
        pm_config.airaccel = sv_airaccelerate.integer;
        sv_airaccelerate.modified = false;
    }

    if (sv_gravity.modified) {
        level.gravity = sv_gravity.value;
        sv_gravity.modified = false;
    }
}

static bool G_AnyDeadPlayersWithoutLives(void)
{
    for (int i = 0; i < game.maxclients; i++) {
        edict_t *player = &g_edicts[i];
        if (player->r.inuse && player->health <= 0 && !player->client->pers.lives)
            return true;
    }

    return false;
}

/*
================
G_RunFrame

Advances the world by 0.1 seconds
================
*/
qvm_exported void G_RunFrame(int64_t time)
{
    level.in_frame = true;
    level.time = time;

    G_CheckCvars();

    if (level.intermission_fading) {
        if (level.intermission_fade_time > level.time) {
            float alpha = Q_clipf(1.3f - TO_SEC(level.intermission_fade_time - level.time), 0, 1);

            for (int i = 0; i < game.maxclients; i++) {
                if (g_edicts[i].r.inuse)
                    Vector4Set(g_clients[i].ps.screen_blend, 0, 0, 0, alpha);
            }
        } else {
            level.intermission_fade = false;
            ExitLevel();
            level.intermission_fading = false;
        }

        level.in_frame = false;

        return;
    }

    edict_t *ent;

    // exit intermissions

    if (level.exitintermission) {
        ExitLevel();
        level.in_frame = false;
        return;
    }

    // reload the map start save if restart time is set (all players are dead)
    if (level.coop_level_restart_time > 0 && level.time > level.coop_level_restart_time) {
        ClientEndServerFrames();
        trap_AddCommandString("restart_level\n");
    }

    // clear client coop respawn states; this is done
    // early since it may be set multiple times for different
    // players
    if (coop.integer && (g_coop_enable_lives.integer || g_coop_squad_respawn.integer)) {
        for (int i = 0; i < game.maxclients; i++) {
            edict_t *player = &g_edicts[i];
            if (!player->r.inuse)
                continue;
            if (player->client->respawn_time >= level.time)
                player->client->coop_respawn_state = COOP_RESPAWN_WAITING;
            else if (g_coop_enable_lives.integer && player->health <= 0 && player->client->pers.lives == 0)
                player->client->coop_respawn_state = COOP_RESPAWN_NO_LIVES;
            else if (g_coop_enable_lives.integer && G_AnyDeadPlayersWithoutLives())
                player->client->coop_respawn_state = COOP_RESPAWN_NO_LIVES;
            else
                player->client->coop_respawn_state = COOP_RESPAWN_NONE;
        }
    }

    //
    // treat each object in turn
    // even the world gets a chance to think
    //
    ent = g_edicts;
    for (int i = 0; i < level.num_edicts; i++, ent++) {
        if (!ent->r.inuse) {
            // defer removing client info so that disconnected, etc works
            if (i < game.maxclients) {
                if (ent->timestamp && level.time < ent->timestamp) {
                    trap_SetConfigstring(CS_PLAYERSKINS + i, "");
                    ent->timestamp = 0;
                }
            }
            continue;
        }

        level.current_entity = ent;

        // Paril: RF_BEAM entities update their old_origin by hand.
        if (!(ent->s.renderfx & RF_BEAM))
            VectorCopy(ent->s.origin, ent->s.old_origin);

        // if the ground entity moved, make sure we are still on it
        if ((ent->groundentity) && (ent->groundentity->r.linkcount != ent->groundentity_linkcount)) {
            contents_t mask = G_GetClipMask(ent);

            if (!(ent->flags & (FL_SWIM | FL_FLY)) && (ent->r.svflags & SVF_MONSTER)) {
                ent->groundentity = NULL;
                M_CheckGround(ent, mask);
            } else {
                // if it's still 1 point below us, we're good
                vec3_t end;
                VectorAdd(ent->s.origin, ent->gravityVector, end);
                trace_t tr;
                trap_Trace(&tr, ent->s.origin, ent->r.mins, ent->r.maxs, end, ent->s.number, mask);

                if (tr.startsolid || tr.allsolid || tr.entnum != ent->groundentity->s.number)
                    ent->groundentity = NULL;
                else
                    ent->groundentity_linkcount = ent->groundentity->r.linkcount;
            }
        }

        if (i < game.maxclients) {
            ClientBeginServerFrame(ent);
            continue;
        }

        G_RunEntity(ent);
    }

    // see if it is time to end a deathmatch
    CheckDMRules();

    // see if needpass needs updated
    CheckNeedPass();

    if (coop.integer && (g_coop_enable_lives.integer || g_coop_squad_respawn.integer)) {
        // rarely, we can see a flash of text if all players respawned
        // on some other player, so if everybody is now alive we'll reset
        // back to empty
        bool reset_coop_respawn = true;

        for (int i = 0; i < game.maxclients; i++) {
            edict_t *player = &g_edicts[i];
            if (player->r.inuse && player->health > 0) {
                reset_coop_respawn = false;
                break;
            }
        }

        if (reset_coop_respawn) {
            for (int i = 0; i < game.maxclients; i++) {
                edict_t *player = &g_edicts[i];
                if (player->r.inuse)
                    player->client->coop_respawn_state = COOP_RESPAWN_NONE;
            }
        }
    }

    // build the playerstate_t structures for all players
    ClientEndServerFrames();

    // [Paril-KEX] if not in intermission and player 1 is loaded in
    // the game as an entity, increase timer on current entry
    if (level.entry && !level.intermissiontime && g_edicts[0].r.inuse && g_edicts[0].client->pers.connected)
        level.entry->time += FRAME_TIME;

    // [Paril-KEX] run monster pains now
    for (int i = game.maxclients + BODY_QUEUE_SIZE; i < level.num_edicts; i++) {
        edict_t *e = &g_edicts[i];

        if (!e->r.inuse || !(e->r.svflags & SVF_MONSTER))
            continue;

        M_ProcessPain(e);
    }

    level.in_frame = false;
}

/*
================
G_PrepFrame

This has to be done before the world logic, because
player processing happens outside RunFrame
================
*/
qvm_exported void G_PrepFrame(void)
{
    for (int i = 0; i < level.num_edicts; i++) {
        edict_t *ent = &g_edicts[i];
        if (!ent->r.inuse)
            continue;
        if (ent->free_after_event) {
            G_FreeEdict(ent);
            continue;
        }
        memset(ent->s.event, 0, sizeof(ent->s.event));
        memset(ent->s.event_param, 0, sizeof(ent->s.event_param));
        ent->r.svflags &= ~SVF_PHS;
    }
}

#ifndef GAME_HARD_LINKED
// this is only here so the functions in q_shared.c can link
void Com_LPrintf(print_type_t type, const char *fmt, ...)
{
    va_list     argptr;
    char        text[MAX_STRING_CHARS];

    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    trap_Print(type, text);
}

void Com_Error(error_type_t type, const char *fmt, ...)
{
    va_list     argptr;
    char        text[MAX_STRING_CHARS];

    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    trap_Error(text);
}
#endif
