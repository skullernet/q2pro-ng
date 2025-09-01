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

#include "g_local.h"
#include "g_ptrs.h"

#define SAVE_MAGIC1     "SSV2"
#define SAVE_MAGIC2     "SAV2"

#define SAVE_VERSION_MINIMUM            1
#define SAVE_VERSION_CURRENT            1

typedef enum {
    F_BYTE,
    F_INT,
    F_UINT,         // hexadecimal
    F_INT64,
    F_UINT64,       // hexadecimal
    F_BOOL,
    F_FLOAT,
    F_LSTRING,      // string on disk, pointer in memory
    F_ZSTRING,      // string on disk, string in memory
    F_EDICT,        // index on disk, pointer in memory
    F_CLIENT,       // index on disk, pointer in memory
    F_ITEM,
    F_POINTER,
    F_STRUCT,

    // these use custom writing methods
    F_INVENTORY,
    F_MAX_AMMO,
    F_STATS,
    F_REINFORCEMENTS,
} fieldtype_t;

typedef struct save_field_s {
    const char *name;
    uint32_t ofs;
    uint32_t size;
    uint16_t count;
    uint8_t kind;
    uint8_t ptrtyp;
    const struct save_field_s *fields;  // for structs
} save_field_t;

#define OFFSET(name) offsetof(STRUCT, name)
#define FIELD(name) ((STRUCT *)0)->name

#define KIND2(name) _Generic(FIELD(name), \
    int32_t: F_INT, \
    int32_t *: F_INT, \
    uint32_t: F_UINT, \
    float: F_FLOAT, \
    float *: F_FLOAT, \
    char *: F_ZSTRING, \
    const char *: F_ZSTRING, \
    byte *: F_BYTE, \
    bool: F_BOOL, \
    int64_t: F_INT64, \
    uint64_t: F_UINT64, \
    const gitem_t *: F_ITEM, \
    edict_t *: F_EDICT, \
    mod_t: F_INT)

// hack to distinguish between char * and char []
#define KIND(name) _Generic(&FIELD(name), \
    char **: F_LSTRING, \
    const char **: F_LSTRING, \
    default: KIND2(name))

#define COUNT2(name) _Generic(FIELD(name), \
    int32_t *: sizeof(FIELD(name)) / sizeof(int32_t), \
    float *: sizeof(FIELD(name)) / sizeof(float), \
    byte *: sizeof(FIELD(name)), \
    char *: sizeof(FIELD(name)), \
    default: 1)

// ditto
#define COUNT(name) _Generic(&FIELD(name), \
    char **: 1, \
    const char **: 1, \
    default: COUNT2(name))

// generic field
#define F(name) \
    { #name, OFFSET(name), sizeof(FIELD(name)) / COUNT(name), COUNT(name), KIND(name), 0, NULL }

// custom field (size unknown)
#define C(kind, name) \
    { #name, OFFSET(name), 0, 0, kind, 0, NULL }

// custom array
#define A(kind, name) \
    { #name, OFFSET(name), sizeof(FIELD(name)[0]), q_countof(FIELD(name)), kind, 0, NULL }

// function or moveinfo pointer
#define P(name, ptrtyp) \
    { #name, OFFSET(name), sizeof(void *), 1, F_POINTER, ptrtyp, NULL }

// struct
#define S(type, name) \
    { #name, OFFSET(name), sizeof(type), 1, F_STRUCT, 0, type##_fields }

// array of structs
#define SA(type, name) \
    { #name, OFFSET(name), sizeof(type), q_countof(FIELD(name)), F_STRUCT, 0, type##_fields }

#define STRUCT moveinfo_t
static const save_field_t moveinfo_t_fields[] = {
    F(start_origin),
    F(start_angles),
    F(end_origin),
    F(end_angles),
    F(end_angles_reversed),

    F(sound_start),
    F(sound_middle),
    F(sound_end),

    F(accel),
    F(speed),
    F(decel),
    F(distance),

    F(wait),

    F(state),
    F(reversing),
    F(dir),
    F(dest),
    F(current_speed),
    F(move_speed),
    F(next_speed),
    F(remaining_distance),
    F(decel_distance),
    P(endfunc, P_moveinfo_endfunc),
    P(blocked, P_moveinfo_blocked),
    { 0 }
};
#undef STRUCT

#define STRUCT reinforcement_t
static const save_field_t reinforcement_t_fields[] = {
    F(classname),
    F(strength),
    F(radius),
    F(mins),
    F(maxs),
    { 0 }
};
#undef STRUCT

#define STRUCT monsterinfo_t
static const save_field_t monsterinfo_t_fields[] = {
    P(active_move, P_mmove_t),
    P(next_move, P_mmove_t),
    F(aiflags),
    F(nextframe),
    F(scale),

    P(stand, P_monsterinfo_stand),
    P(idle, P_monsterinfo_idle),
    P(search, P_monsterinfo_search),
    P(walk, P_monsterinfo_walk),
    P(run, P_monsterinfo_run),
    P(dodge, P_monsterinfo_dodge),
    P(attack, P_monsterinfo_attack),
    P(melee, P_monsterinfo_melee),
    P(sight, P_monsterinfo_sight),
    P(checkattack, P_monsterinfo_checkattack),
    P(setskin, P_monsterinfo_setskin),
    P(physics_change, P_monsterinfo_physchanged),

    F(pausetime),
    F(attack_finished),
    F(fire_wait),

    F(saved_goal),
    F(search_time),
    F(trail_time),
    F(last_sighting),
    F(attack_state),
    F(lefty),
    F(idle_time),
    F(linkcount),

    F(power_armor_type),
    F(power_armor_power),

    F(initial_power_armor_type),
    F(max_power_armor_power),
    F(weapon_sound),
    F(engine_sound),

    P(blocked, P_monsterinfo_blocked),
    F(last_hint_time),
    F(goal_hint),
    F(medicTries),
    F(badMedic1),
    F(badMedic2),
    F(healer),
    P(duck, P_monsterinfo_duck),
    P(unduck, P_monsterinfo_unduck),
    P(sidestep, P_monsterinfo_sidestep),
    F(base_height),
    F(next_duck_time),
    F(duck_wait_time),
    F(last_player_enemy),
    F(blindfire),
    F(can_jump),
    F(had_visibility),
    F(drop_height),
    F(jump_height),
    F(blind_fire_delay),
    F(blind_fire_target),
    F(slots_from_commander),
    F(monster_slots),
    F(monster_used),
    F(commander),
    F(quad_time),
    F(invincible_time),
    F(double_time),

    F(surprise_time),
    F(armor_type),
    F(armor_power),
    F(close_sight_tripped),
    F(melee_debounce_time),
    F(strafe_check_time),
    F(base_health),
    F(health_scaling),
    F(next_move_time),
    F(bad_move_time),
    F(bump_time),
    F(random_change_time),
    F(path_blocked_counter),
    F(path_wait_time),
    F(combat_style),

    F(damage_attacker),
    F(damage_inflictor),
    F(damage_blood),
    F(damage_knockback),
    F(damage_from),
    F(damage_mod),

    F(fly_max_distance),
    F(fly_min_distance),
    F(fly_acceleration),
    F(fly_speed),
    F(fly_ideal_position),
    F(fly_position_time),
    F(fly_buzzard),
    F(fly_above),
    F(fly_pinned),
    F(fly_thrusters),
    F(fly_recovery_time),
    F(fly_recovery_dir),

    F(checkattack_time),
    F(start_frame),
    F(dodge_time),
    F(move_block_counter),
    F(move_block_change_time),
    F(react_to_damage_time),

    C(F_REINFORCEMENTS, reinforcements),
    F(chosen_reinforcements),

    F(jump_time),
    { 0 }
};
#undef STRUCT

#define STRUCT bmodel_anim_t
static const save_field_t bmodel_anim_t_fields[] = {
    F(params[0].start),
    F(params[0].end),
    F(params[0].style),
    F(params[0].speed),
    F(params[0].nowrap),
    F(params[1].start),
    F(params[1].end),
    F(params[1].style),
    F(params[1].speed),
    F(params[1].nowrap),
    F(enabled),
    F(alternate),
    F(currently_alternate),
    F(next_tick),
    { 0 }
};
#undef STRUCT

#define STRUCT player_fog_t
static const save_field_t player_fog_t_fields[] = {
    F(color),
    F(density),
    F(sky_factor),
    { 0 }
};
#undef STRUCT

#define STRUCT player_heightfog_t
static const save_field_t player_heightfog_t_fields[] = {
    F(start.color),
    F(start.dist),
    F(end.color),
    F(end.dist),
    F(density),
    F(falloff),
    { 0 }
};
#undef STRUCT

#define STRUCT edict_t
static const save_field_t edict_t_fields[] = {
    F(s.origin),
    F(s.angles),
    F(s.old_origin),
    F(s.modelindex),
    F(s.modelindex2),
    F(s.modelindex3),
    F(s.modelindex4),
    F(s.frame),
    F(s.skinnum),
    F(s.effects),
    F(s.renderfx),
    F(s.sound),
    F(s.morefx),
    F(s.alpha),
    F(s.scale),
    F(s.othernum),

    F(r.linkcount),
    F(r.svflags),
    F(r.mins),
    F(r.maxs),
    F(r.solid),
    F(r.ownernum),
    F(r.clientmask),

    F(spawn_count),
    F(movetype),
    F(clipmask),
    F(flags),

    F(model),
    F(freetime),

    F(message),
    F(classname),
    F(spawnflags),

    F(timestamp),

    F(angle),
    F(target),
    F(targetname),
    F(killtarget),
    F(team),
    F(pathtarget),
    F(deathtarget),
    F(healthtarget),
    F(itemtarget),
    F(combattarget),
    F(target_ent),

    F(speed),
    F(accel),
    F(decel),
    F(movedir),
    F(pos1),
    F(pos2),
    F(pos3),

    F(velocity),
    F(avelocity),
    F(mass),
    F(air_finished),
    F(gravity),

    F(goalentity),
    F(movetarget),
    F(yaw_speed),
    F(ideal_yaw),

    F(nextthink),
    P(prethink, P_prethink),
    P(postthink, P_prethink),
    P(think, P_think),
    P(touch, P_touch),
    P(use, P_use),
    P(pain, P_pain),
    P(die, P_die),

    F(touch_debounce_time),
    F(pain_debounce_time),
    F(damage_debounce_time),
    F(fly_sound_debounce_time),
    F(last_move_time),

    F(health),
    F(max_health),
    F(gib_health),
    F(show_hostile),

    F(powerarmor_time),

    F(map),

    F(viewheight),
    F(deadflag),
    F(takedamage),
    F(dmg),
    F(radius_dmg),
    F(dmg_radius),
    F(sounds),
    F(count),

    F(chain),
    F(enemy),
    F(oldenemy),
    F(activator),
    F(groundentity),
    F(groundentity_linkcount),
    F(teamchain),
    F(teammaster),

    F(mynoise),
    F(mynoise2),

    F(noise_index),
    F(noise_index2),
    F(volume),
    F(attenuation),

    F(wait),
    F(delay),
    F(random),

    F(teleport_time),

    F(watertype),
    F(waterlevel),

    F(move_origin),
    F(move_angles),

    F(style),

    F(item),

    S(moveinfo_t, moveinfo),
    S(monsterinfo_t, monsterinfo),

    F(plat2flags),
    F(offset),
    F(gravityVector),
    F(bad_area),
    F(hint_chain),
    F(monster_hint_chain),
    F(target_hint_chain),
    F(hint_chain_id),

    F(clock_message),

    F(dead_time),
    F(beam),
    F(beam2),
    F(proboscus),
    F(disintegrator),
    F(disintegrator_time),
    F(hackflags),

    S(player_fog_t, fog_off),
    S(player_fog_t, fog),

    S(player_heightfog_t, heightfog_off),
    S(player_heightfog_t, heightfog),

    F(slime_debounce_time),

    S(bmodel_anim_t, bmodel_anim),

    F(style_on),
    F(style_off),

    F(crosslevel_flags),
    F(no_gravity_time),
    F(vision_cone),
    F(free_after_event),
    { 0 }
};
#undef STRUCT

#define STRUCT level_locals_t
static const save_field_t level_locals_t_fields[] = {
    F(time),

    F(level_name),
    F(mapname),
    F(nextmap),

    F(intermissiontime),
    F(changemap),
    F(achievement),
    F(exitintermission),
    F(intermission_clear),
    F(intermission_origin),
    F(intermission_angle),

    F(pic_health),
    F(pic_ping),
    F(snd_fry),

    F(total_secrets),
    F(found_secrets),

    F(total_goals),
    F(found_goals),

    F(total_monsters),
    F(killed_monsters),

    F(body_que),
    F(power_cubes),

    F(disguise_violator),
    F(disguise_violation_time),
    F(disguise_icon),

    F(coop_level_restart_time),

    F(goals),
    F(goal_num),

    F(valid_poi),
    F(current_poi),
    F(current_poi_image),
    F(current_poi_stage),
    F(current_dynamic_poi),

    F(start_items),
    F(no_grapple),

    F(gravity),
    F(hub_map),
    F(health_bar_entities[0]),
    F(health_bar_entities[1]),
    F(story_active),
    F(next_auto_save),

    F(primary_objective_string),
    F(secondary_objective_string),
    F(primary_objective_title),
    F(secondary_objective_title),
    { 0 }
};
#undef STRUCT

#define STRUCT client_persistent_t
static const save_field_t client_persistent_t_fields[] = {
    F(userinfo),
    F(netname),
    F(hand),
    F(autoswitch),
    F(autoshield),

    F(health),
    F(max_health),
    F(savedFlags),

    F(selected_item),
    F(selected_item_time),
    A(F_INVENTORY, inventory),

    A(F_MAX_AMMO, max_ammo),

    F(weapon),
    F(lastweapon),

    F(power_cubes),
    F(score),

    F(game_help1changed),
    F(game_help2changed),
    F(helpchanged),
    F(help_time),

    F(spectator),

    F(megahealth_time),
    F(lives),
    { 0 }
};
#undef STRUCT

#define STRUCT gclient_t
static const save_field_t gclient_t_fields[] = {
    F(ps.pm_type),

    F(ps.origin),
    F(ps.velocity),
    F(ps.pm_flags),
    F(ps.pm_time),
    F(ps.gravity),
    F(ps.delta_angles),

    F(ps.viewangles),
    F(ps.viewheight),

    F(ps.bobtime),

    F(ps.gunindex),
    F(ps.gunskin),
    F(ps.gunframe),
    F(ps.gunrate),

    F(ps.screen_blend),
    F(ps.damage_blend),

    F(ps.fov),

    F(ps.rdflags),

    C(F_STATS, ps.stats),

    S(client_persistent_t, pers),

    S(client_persistent_t, resp.coop_respawn),

    F(resp.entertime),
    F(resp.score),
    F(resp.spectator),

    F(newweapon),

    F(killer_yaw),

    F(weaponstate),

    F(damage_alpha),
    F(bonus_alpha),
    F(damage_blend),
    F(v_angle),
    F(v_forward),
    F(oldviewangles),
    F(oldvelocity),
    F(oldgroundentity),
    F(flash_time),

    F(next_drown_time),
    F(old_waterlevel),
    F(breather_sound),

    F(anim_end),
    F(anim_priority),
    F(anim_duck),
    F(anim_run),
    F(anim_time),

    F(quad_time),
    F(invincible_time),
    F(breather_time),
    F(enviro_time),
    F(invisible_time),

    F(grenade_blew_up),
    F(grenade_time),
    F(grenade_finished_time),
    F(quadfire_time),
    F(silencer_shots),
    F(weapon_sound),

    F(pickup_msg_time),

    F(respawn_time),

    F(double_time),
    F(ir_time),
    F(nuke_time),
    F(tracker_pain_time),

    F(empty_click_sound),

    F(trail_head),
    F(trail_tail),

    F(landmark_free_fall),
    F(landmark_name),
    F(landmark_rel_pos),
    F(landmark_noise_time),

    F(invisibility_fade_time),

    F(last_step_time),
    F(last_ladder_pos),
    F(last_ladder_sound),

    F(sight_entity),
    F(sight_entity_time),
    F(sound_entity),
    F(sound_entity_time),
    F(sound2_entity),
    F(sound2_entity_time),

    S(player_fog_t, wanted_fog),
    S(player_heightfog_t, wanted_heightfog),

    F(last_firing_time),
    { 0 }
};
#undef STRUCT

#define STRUCT level_entry_t
static const save_field_t level_entry_t_fields[] = {
    F(map_name),
    F(pretty_name),
    F(total_secrets),
    F(found_secrets),
    F(total_monsters),
    F(killed_monsters),
    F(time),
    F(visit_order),
    { 0 }
};
#undef STRUCT

#define STRUCT game_locals_t
static const save_field_t game_locals_t_fields[] = {
    F(helpmessage1),
    F(helpmessage2),
    F(help1changed),
    F(help2changed),

    F(maxclients),

    F(cross_level_flags),
    F(cross_unit_flags),

    F(autosaved),

    SA(level_entry_t, level_entries),
    { 0 }
};
#undef STRUCT

//=========================================================

static qhandle_t g_savefile;

//
// writing
//

static const union {
    level_locals_t level;
    game_locals_t game;
    gclient_t client;
    reinforcement_t reinforcement;
} empty;

static struct {
    int indent;
} block;

#define indent(s)   (int)(block.indent + strlen(s)), s
#define write_str(...)  trap_FS_FilePrintf(g_savefile, __VA_ARGS__)

static void begin_block(const char *name)
{
    write_str("%*s {\n", indent(name));
    block.indent += 2;
}

static void end_block(void)
{
    block.indent -= 2;
    write_str("%*s}\n", indent(""));
}

static void write_tok(const char *name, const char *tok)
{
    write_str("%*s %s\n", indent(name), tok);
}

static void write_int(const char *name, int v)
{
    write_str("%*s %d\n", indent(name), v);
}

static void write_uint(const char *name, unsigned v)
{
    if (v < 1024)
        write_str("%*s %u\n", indent(name), v);
    else
        write_str("%*s %#x\n", indent(name), v);
}

static void write_int64(const char *name, int64_t v)
{
    write_str("%*s %"PRId64"\n", indent(name), v);
}

static void write_uint64(const char *name, uint64_t v)
{
    write_str("%*s %#"PRIx64"\n", indent(name), v);
}

static void write_int_v(const char *name, const int32_t *v, int n)
{
    write_str("%*s ", indent(name));
    for (int i = 0; i < n; i++)
        write_str("%d ", v[i]);
    write_str("\n");
}

static void write_float_v(const char *name, const float *v, int n)
{
    write_str("%*s ", indent(name));
    for (int i = 0; i < n; i++)
        write_str("%.6g ", v[i]);
    write_str("\n");
}

static void write_string(const char *name, const char *s)
{
    char buffer[MAX_STRING_CHARS * 4];

    COM_EscapeString(buffer, s, sizeof(buffer));
    write_str("%*s \"%s\"\n", indent(name), buffer);
}

static void write_byte_v(const char *name, const byte *p, int n)
{
    if (n == 1) {
        write_int(name, *p);
        return;
    }

    write_str("%*s ", indent(name));
    int cnt = 1;
    for (int i = 0; i < n; i++)
        if (p[i])
            cnt = i + 1;
    for (int i = 0; i < cnt; i++)
        write_str("%02x", p[i]);
    write_str("\n");
}

static void write_pointer(const char *name, const void *p, ptr_type_t type)
{
    const save_ptr_t *ptr;
    int i;

    for (i = 0, ptr = save_ptrs[type]; i < num_save_ptrs[type]; i++, ptr++) {
        if (ptr->ptr == p) {
            write_tok(name, ptr->name);
            return;
        }
    }

    G_Error("unknown pointer of type %d: %p", type, p);
}

static void write_inventory(const int16_t *inven)
{
    begin_block("inventory");
    for (int i = IT_NULL + 1; i < IT_TOTAL; i++) {
        if (inven[i]) {
            Q_assert(itemlist[i].classname);
            write_int(itemlist[i].classname, inven[i]);
        }
    }
    end_block();
}

static void write_max_ammo(const int16_t *max_ammo)
{
    begin_block("max_ammo");
    for (int i = AMMO_BULLETS; i < AMMO_MAX; i++)
        if (max_ammo[i])
            write_int(GetItemByAmmo(i)->classname, max_ammo[i]);
    end_block();
}

static const struct {
    const char *name;
    int         stat;
} statdefs[] = {
    { "pickup_icon", STAT_PICKUP_ICON },
    { "pickup_string", STAT_PICKUP_STRING },
    { "selected_item_name", STAT_SELECTED_ITEM_NAME },
};

static void write_stats(const int32_t *stats)
{
    int i;

    for (i = 0; i < q_countof(statdefs); i++)
        if (stats[statdefs[i].stat])
            break;
    if (i == q_countof(statdefs))
        return;

    begin_block("ps.stats");
    for (i = 0; i < q_countof(statdefs); i++)
        if (stats[statdefs[i].stat])
            write_int(statdefs[i].name, stats[statdefs[i].stat]);
    end_block();
}

static void write_fields(const char *name, const save_field_t *field, const void *from, const void *to);

static void write_reinforcements(const reinforcement_list_t *list)
{
    if (!list->num_reinforcements)
        return;

    begin_block(va("reinforcements %d", list->num_reinforcements));
    for (int i = 0; i < list->num_reinforcements; i++)
        write_fields(va("%d", i), reinforcement_t_fields, &empty.reinforcement, &list->reinforcements[i]);
    end_block();
}

static void write_struct(const save_field_t *field, const byte *from, const byte *to)
{
    if (field->count == 1) {
        write_fields(field->name, field->fields, from, to);
        return;
    }

    begin_block(field->name);
    for (int i = 0; i < field->count; i++)
        write_fields(va("%d", i), field->fields, from + i * field->size, to + i * field->size);
    end_block();
}

static void write_field(const save_field_t *field, const void *from, const void *to)
{
    const void *e = (const byte *)from + field->ofs;
    const void *p = (const byte *)to   + field->ofs;
    size_t size = field->size * field->count;

    if (size && !memcmp(e, p, size))
        return;

    switch (field->kind) {
    case F_BYTE:
        write_byte_v(field->name, p, field->count);
        break;
    case F_INT:
        write_int_v(field->name, p, field->count);
        break;
    case F_UINT:
        write_uint(field->name, *(uint32_t *)p);
        break;
    case F_INT64:
        write_int64(field->name, *(int64_t *)p);
        break;
    case F_UINT64:
        write_uint64(field->name, *(uint64_t *)p);
        break;
    case F_BOOL:
        write_str("%*s %s\n", indent(field->name), *(bool *)p ? "true" : "false");
        break;
    case F_FLOAT:
        write_float_v(field->name, p, field->count);
        break;

    case F_ZSTRING:
        write_string(field->name, (const char *)p);
        break;
    case F_LSTRING:
        write_string(field->name, *(char **)p);
        break;

    case F_EDICT:
        write_int(field->name, *(edict_t **)p - g_edicts);
        break;
    case F_CLIENT:
        write_int(field->name, *(gclient_t **)p - g_clients);
        break;

    case F_ITEM:
        write_tok(field->name, (*(gitem_t **)p)->classname);
        break;
    case F_POINTER:
        write_pointer(field->name, *(void **)p, field->ptrtyp);
        break;

    case F_STRUCT:
        write_struct(field, e, p);
        break;

    case F_INVENTORY:
        write_inventory(p);
        break;

    case F_MAX_AMMO:
        write_max_ammo(p);
        break;

    case F_STATS:
        write_stats(p);
        break;

    case F_REINFORCEMENTS:
        write_reinforcements(p);
        break;
    }
}

static void write_fields(const char *name, const save_field_t *field, const void *from, const void *to)
{
    begin_block(name);
    while (field->name) {
        write_field(field, from, to);
        field++;
    }
    end_block();
}

//
// reading
//

static struct {
    char data[MAX_STRING_CHARS * 4];
    char token[MAX_STRING_CHARS];
    const char *ptr;
    int len;
    int number;
    int version;
} line;

static const char *read_line(void)
{
    int ret = trap_FS_ReadLine(g_savefile, line.data, sizeof(line.data));
    if (ret < 0) {
        char buf[MAX_QPATH];
        trap_FS_ErrorString(ret, buf, sizeof(buf));
        G_Error("error reading input file: %s", buf);
    }
    line.ptr = line.data;
    line.number++;
    return line.ptr;
}

q_noreturn q_cold q_printf(1, 2)
static void parse_error(const char *fmt, ...)
{
    va_list     argptr;
    char        text[MAX_STRING_CHARS];

    va_start(argptr, fmt);
    Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    G_Error("error at line %d: %s", line.number, text);
}

static int unescape_char(int c)
{
    switch (c) {
        case 'a': return '\a';
        case 'b': return '\b';
        case 't': return '\t';
        case 'n': return '\n';
        case 'v': return '\v';
        case 'f': return '\f';
        case 'r': return '\r';
        case '\\': return '\\';
        case '"': return '"';
    }
    return 0;
}

static void parse_quoted(const char *s)
{
    int len = 0;

    while (1) {
        int c;

        if (!*s)
            parse_error("unterminated quoted string");

        if (*s == '\"') {
            s++;
            break;
        }

        if (*s == '\\') {
            c = unescape_char(s[1]);
            if (c) {
                s += 2;
            } else {
                int c1, c2;

                if (s[1] != 'x' || (c1 = Q_charhex(s[2])) == -1 || (c2 = Q_charhex(s[3])) == -1)
                    parse_error("bad escape sequence");

                c = (c1 << 4) | c2;
                s += 4;
            }
        } else {
            c = *s++;
        }

        if (len == sizeof(line.token) - 1)
            parse_error("oversize token");

        line.token[len++] = c;
    }

    line.len = len;
    line.ptr = s;
}

static void parse_word(const char *s)
{
    int len = 0;

    do {
        if (len == sizeof(line.token) - 1)
            parse_error("oversize token");
        line.token[len++] = *s++;
    } while (*s > 32);

    line.len = len;
    line.ptr = s;
}

static char *parse(void)
{
    const char *s = line.ptr;

    if (!s)
        s = read_line();
skip:
    while (*s <= 32) {
        if (!*s) {
            s = read_line();
            continue;
        }
        s++;
    }

    if (*s == '/' && s[1] == '/') {
        s = read_line();
        goto skip;
    }

    if (*s == '\"') {
        s++;
        parse_quoted(s);
    } else {
        parse_word(s);
    }

    line.token[line.len] = 0;
    return line.token;
}

static void expect(const char *what)
{
    const char *token = parse();

    if (strcmp(token, what))
        parse_error("expected %s, got %s", what, COM_MakePrintable(token));
}

static void unknown(const char *what)
{
    const char *token = COM_MakePrintable(line.token);

    if (g_strict_saves.integer)
        parse_error("unknown %s: %s", what, token);

    G_Printf("WARNING: line %d: unknown %s: %s\n", line.number, what, token);
    line.ptr = NULL;    // skip to next line
}

static int parse_int_tok(const char *tok, int v_min, int v_max)
{
    char *end;
    long v;

    v = strtol(tok, &end, 0);
    if (end == tok || *end)
        parse_error("expected int, got %s", COM_MakePrintable(tok));

    if (v < v_min || v > v_max)
        parse_error("value out of range: %ld", v);

    return v;
}

static int parse_int(int v_min, int v_max)
{
    return parse_int_tok(parse(), v_min, v_max);
}

static int parse_int16(void)
{
    return parse_int(INT16_MIN, INT16_MAX);
}

static int parse_int32(void)
{
    return parse_int(INT32_MIN, INT32_MAX);
}

static unsigned parse_uint_tok(const char *tok, unsigned v_max)
{
    char *end;
    unsigned long v;

    v = strtoul(tok, &end, 0);
    if (end == tok || *end)
        parse_error("expected int, got %s", COM_MakePrintable(tok));

    if (v > v_max)
        parse_error("value out of range: %lu", v);

    return v;
}

static unsigned parse_uint(unsigned v_max)
{
    return parse_uint_tok(parse(), v_max);
}

static int parse_array(int count)
{
    const char *tok = parse();
    if (!strcmp(tok, "}"))
        return -1;
    return parse_uint_tok(tok, count - 1);
}

static float parse_float(void)
{
    char *tok, *end;
    float v;

    tok = parse();
    v = strtof(tok, &end);
    if (end == tok || *end)
        parse_error("expected float, got %s", COM_MakePrintable(tok));

    return v;
}

static uint64_t parse_uint64(void)
{
    char *tok, *end;
    uint64_t v;

    tok = parse();
    v = strtoull(tok, &end, 0);
    if (end == tok || *end)
        parse_error("expected int, got %s", COM_MakePrintable(tok));

    return v;
}

static void parse_int_v(int32_t *v, int n)
{
    for (int i = 0; i < n; i++)
        v[i] = parse_int32();
}

static void parse_float_v(float *v, int n)
{
    for (int i = 0; i < n; i++)
        v[i] = parse_float();
}

static void parse_byte_v(byte *v, int n)
{
    if (n == 1) {
        *v = parse_uint(255);
        return;
    }

    parse();
    if (line.len & 1 || line.len < 2 || line.len > n * 2)
        parse_error("unexpected number of characters");

    for (int i = 0; i < line.len / 2; i++) {
        int c1 = Q_charhex(line.token[i * 2 + 0]);
        int c2 = Q_charhex(line.token[i * 2 + 1]);
        if (c1 == -1 || c2 == -1)
            parse_error("not a hex character");
        v[i] = (c1 << 4) | c2;
    }
}

static bool parse_bool(void)
{
    char *tok = parse();
    if (!strcmp(tok, "false"))
        return false;
    if (!strcmp(tok, "true"))
        return true;
    parse_error("expected bool, got %s", COM_MakePrintable(tok));
}

static char *read_string(void)
{
    char *s;

    parse();
    s = G_Malloc(line.len + 1);
    memcpy(s, line.token, line.len + 1);

    return s;
}

static void read_zstring(char *s, int size)
{
    if (Q_strlcpy(s, parse(), size) >= size)
        parse_error("oversize string");
}

static const gitem_t *read_item(void)
{
    const gitem_t *item = FindItemByClassname(parse());

    if (!item)
        unknown("item");

    return item;
}

static void *read_pointer(ptr_type_t type)
{
    const save_ptr_t *ptrs = save_ptrs[type];
    const char *name = parse();
    int left = 0;
    int right = num_save_ptrs[type] - 1;

    while (left <= right) {
        int i = (left + right) / 2;
        int r = strcmp(name, ptrs[i].name);
        if (r < 0)
            right = i - 1;
        else if (r > 0)
            left = i + 1;
        else
            return (void *)ptrs[i].ptr;
    }

    unknown("pointer");
    return NULL;
}

static void read_inventory(int16_t *inven)
{
    expect("{");
    while (1) {
        const char *tok = parse();
        if (!strcmp(tok, "}"))
            break;
        const gitem_t *item = FindItemByClassname(tok);
        if (item)
            inven[item->id] = parse_int16();
        else
            unknown("item");
    }
}

static void read_max_ammo(int16_t *max_ammo)
{
    expect("{");
    while (1) {
        const char *tok = parse();
        if (!strcmp(tok, "}"))
            break;
        const gitem_t *item = FindItemByClassname(tok);
        if (item && (item->flags & IF_AMMO))
            max_ammo[item->tag] = parse_int16();
        else
            unknown("ammo");
    }
}

static void read_stats(int32_t *stats)
{
    expect("{");
    while (1) {
        const char *tok = parse();
        if (!strcmp(tok, "}"))
            break;
        int i;
        for (i = 0; i < q_countof(statdefs); i++)
            if (!strcmp(tok, statdefs[i].name))
                break;
        if (i < q_countof(statdefs))
            stats[statdefs[i].stat] = parse_int32();
        else
            unknown("stat");
    }
}

static void read_fields(const save_field_t *field, void *base);

static void read_reinforcements(reinforcement_list_t *list)
{
    int count = parse_int(1, MAX_REINFORCEMENTS_TOTAL);
    int num;

    expect("{");
    list->num_reinforcements = count;
    list->reinforcements = G_Malloc(sizeof(list->reinforcements[0]) * count);
    while ((num = parse_array(count)) != -1)
        read_fields(reinforcement_t_fields, &list->reinforcements[num]);
}

static void read_struct(const save_field_t *field, byte *base)
{
    int num;

    if (field->count == 1) {
        read_fields(field->fields, base);
        return;
    }

    expect("{");
    while ((num = parse_array(field->count)) != -1)
        read_fields(field->fields, base + num * field->size);
}

static void read_field(const save_field_t *field, void *base)
{
    void *p = (byte *)base + field->ofs;

    switch (field->kind) {
    case F_BYTE:
        parse_byte_v(p, field->count);
        break;
    case F_INT:
        parse_int_v(p, field->count);
        break;
    case F_UINT:
        *(uint32_t *)p = parse_uint(UINT32_MAX);
        break;
    case F_INT64:
    case F_UINT64:
        *(uint64_t *)p = parse_uint64();
        break;
    case F_BOOL:
        *(bool *)p = parse_bool();
        break;
    case F_FLOAT:
        parse_float_v(p, field->count);
        break;

    case F_LSTRING:
        *(char **)p = read_string();
        break;
    case F_ZSTRING:
        read_zstring(p, field->count);
        break;

    case F_EDICT:
        *(edict_t **)p = &g_edicts[parse_uint(MAX_EDICTS - 1)];
        break;
    case F_CLIENT:
        *(gclient_t **)p = &g_clients[parse_uint(game.maxclients - 1)];
        break;

    case F_ITEM:
        *(const gitem_t **)p = read_item();
        break;
    case F_POINTER:
        *(void **)p = read_pointer(field->ptrtyp);
        break;

    case F_STRUCT:
        read_struct(field, p);
        break;

    case F_INVENTORY:
        read_inventory(p);
        break;
    case F_MAX_AMMO:
        read_max_ammo(p);
        break;
    case F_STATS:
        read_stats(p);
        break;

    case F_REINFORCEMENTS:
        read_reinforcements(p);
        break;
    }
}

static const save_field_t *find_field(const save_field_t *f, const char *tok)
{
    while (f->name) {
        if (!strcmp(f->name, tok))
            return f;
        f++;
    }
    return NULL;
}

static void read_fields(const save_field_t *field, void *base)
{
    const save_field_t *f = field;
    expect("{");
    while (1) {
        const char *tok = parse();
        if (!strcmp(tok, "}"))
            break;
        // expect fields in order we wrote them,
        // but also allow (slow) out of order lookup
        f = find_field(f, tok);
        if (!f)
            f = find_field(field, tok);
        if (f)
            read_field(f++, base);
        else
            unknown("field");
    }
}


//=========================================================

/*
============
WriteGame

This will be called whenever the game goes to a new level,
and when the user explicitly saves the game.

Game information include cross level data, like multi level
triggers, help computer info, and all client states.

A single player death will automatically restore from the
last save position.
============
*/
qvm_exported void G_WriteGame(qhandle_t handle, bool autosave)
{
    if (!autosave)
        SaveClientData();

    g_savefile = handle;

    memset(&block, 0, sizeof(block));
    write_str(SAVE_MAGIC1 " version %d\n", SAVE_VERSION_CURRENT);

    game.autosaved = autosave;
    write_fields("game", game_locals_t_fields, &empty.game, &game);
    game.autosaved = false;

    begin_block("clients");
    for (int i = 0; i < game.maxclients; i++)
        write_fields(va("%d", i), gclient_t_fields, &empty.client, &g_clients[i]);
    end_block();
}

qvm_exported void G_ReadGame(qhandle_t handle)
{
    int num;

    g_savefile = handle;

    memset(&line, 0, sizeof(line));
    expect(SAVE_MAGIC1);
    expect("version");
    line.version = parse_int32();
    if (line.version < SAVE_VERSION_MINIMUM || line.version > SAVE_VERSION_CURRENT)
        G_Error("Savegame has bad version");

    int maxclients = game.maxclients;

    expect("game");
    read_fields(game_locals_t_fields, &game);

    // should agree with server's version
    if (game.maxclients != maxclients)
        G_Error("Savegame has bad maxclients");

    memset(g_clients, 0, sizeof(g_clients[0]) * game.maxclients);

    expect("clients");
    expect("{");
    while ((num = parse_array(game.maxclients)) != -1)
        read_fields(gclient_t_fields, &g_clients[num]);
}

//==========================================================

/*
=================
WriteLevel

=================
*/
qvm_exported void G_WriteLevel(qhandle_t handle)
{
    int     i;
    edict_t *ent, *nullent;

    g_savefile = handle;

    memset(&block, 0, sizeof(block));
    write_str(SAVE_MAGIC2 " version %d\n", SAVE_VERSION_CURRENT);

    // write out level_locals_t
    write_fields("level", level_locals_t_fields, &empty.level, &level);

    // init dummy entity to get default values
    nullent = &g_edicts[ENTITYNUM_NONE];
    G_InitEdict(nullent);

    // write out all the entities
    begin_block("entities");
    for (i = 0; i < level.num_edicts; i++) {
        ent = &g_edicts[i];
        if (!ent->r.inuse)
            continue;
        write_fields(va("%d", i), edict_t_fields, nullent, ent);
    }
    end_block();

    memset(nullent, 0, sizeof(*nullent));
}

/*
=================
ReadLevel

SpawnEntities will allready have been called on the
level the same way it was when the level was saved.

That is necessary to get the baselines
set up identically.

The server will have cleared all of the world links before
calling ReadLevel.

No clients are connected yet.
=================
*/
qvm_exported void G_ReadLevel(qhandle_t handle)
{
    int     entnum;
    int     i;
    edict_t *ent;

    // free any dynamic memory allocated by loading the level
    // base state
    G_FreeMemory();

    // clear old pointers
    for (const save_field_t *f = level_locals_t_fields; f->name; f++)
        if (f->kind == F_LSTRING)
            *(char **)((byte *)&level + f->ofs) = NULL;

    g_savefile = handle;

    memset(&line, 0, sizeof(line));
    expect(SAVE_MAGIC2);
    expect("version");
    line.version = parse_int32();
    if (line.version < SAVE_VERSION_MINIMUM || line.version > SAVE_VERSION_CURRENT)
        G_Error("Savegame has bad version");

    // wipe all the entities except world
    memset(g_edicts, 0, sizeof(g_edicts[0]) * ENTITYNUM_WORLD);
    level.num_edicts = game.maxclients;

    // load the level locals
    expect("level");
    read_fields(level_locals_t_fields, &level);

    // load all the entities
    expect("entities");
    expect("{");
    while ((entnum = parse_array(ENTITYNUM_WORLD)) != -1) {
        if (entnum >= level.num_edicts)
            level.num_edicts = entnum + 1;

        ent = &g_edicts[entnum];
        if (ent->r.inuse)
            parse_error("duplicate entity: %d", entnum);

        G_InitEdict(ent);
        read_fields(edict_t_fields, ent);
    }

    // set final amount of edicts
    trap_SetNumEdicts(level.num_edicts);

    // mark all clients as unconnected
    for (i = 0; i < game.maxclients; i++) {
        ent = &g_edicts[i];
        ent->client = g_clients + i;
        ent->client->pers.connected = false;
        ent->client->pers.spawned = false;
    }

    // do any load time things at this point
    for (i = 0; i < level.num_edicts; i++) {
        ent = &g_edicts[i];

        if (!ent->r.inuse)
            continue;

        Q_assert(ent->classname);

        // fire any cross-level triggers
        if (strcmp(ent->classname, "target_crosslevel_target") == 0 ||
            strcmp(ent->classname, "target_crossunit_target") == 0)
            ent->nextthink = level.time + SEC(ent->delay);

        // let the server rebuild world links for this ent
        trap_LinkEntity(ent);
    }

    // precache player inventory items
    G_PrecacheInventoryItems();

    // refresh global precache indices
    G_RefreshPrecaches();
}

// [Paril-KEX]
qvm_exported bool G_CanSave(bool autosave)
{
    // autosave silently checks if savegames are enabled
    if (autosave)
        return !deathmatch.integer;

    if (deathmatch.integer) {
        G_Printf("Can't savegame in a deathmatch.\n");
        return false;
    }

    if (game.maxclients == 1 && g_edicts[0].health <= 0) {
        G_Printf("Can't savegame while dead!\n");
        return false;
    }

    // don't allow saving during cameras/intermissions as this
    // causes the game to act weird when these are loaded
    if (level.intermissiontime) {
        G_Printf("Can't savegame during intermission!\n");
        return false;
    }

    return true;
}
