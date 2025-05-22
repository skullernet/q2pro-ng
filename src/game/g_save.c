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
    F_INT16,
    F_INT,
    F_UINT,         // hexadecimal
    F_INT64,
    F_UINT64,       // hexadecimal
    F_BOOL,
    F_FLOAT,
    F_LSTRING,      // string on disk, pointer in memory
    F_ZSTRING,      // string on disk, string in memory
    F_VECTOR,
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

// generic array
#define GA_(kind, type, name, count) \
    { #name, _OFS(name), sizeof(type), count, kind, 0, NULL }

// generic field
#define GF_(kind, type, name) GA_(kind, type, name, 1)

// custom field (size unknown)
#define CF_(kind, name) \
    { #name, _OFS(name), 0, 0, kind, 0, NULL }

// function or moveinfo pointer
#define P(name, ptrtyp) \
    { #name, _OFS(name), sizeof(void *), 1, F_POINTER, ptrtyp, NULL }

// array of structs
#define RA(type, name, count) \
    { #name, _OFS(name), sizeof(type), count, F_STRUCT, 0, type##_fields }

// arrays
#define FA(name, count) GA_(F_FLOAT,   float,   name, count)
#define SZ(name, count) GA_(F_ZSTRING, char,    name, count)
#define BA(name, count) GA_(F_BYTE,    byte,    name, count)
#define SA(name, count) GA_(F_INT16,   int16_t, name, count)
#define IA(name, count) GA_(F_INT,     int,     name, count)

// single fields
#define F(name) FA(name, 1)
#define B(name) BA(name, 1)
#define S(name) SA(name, 1)
#define I(name) IA(name, 1)
#define R(type, name) RA(type, name, 1)

#define H(name)   GF_(F_UINT,    int,       name)
#define H64(name) GF_(F_UINT64,  uint64_t,  name)
#define O(name)   GF_(F_BOOL,    bool,      name)
#define L(name)   GF_(F_LSTRING, char *,    name)
#define V(name)   GF_(F_VECTOR,  vec3_t,    name)
#define M(name)   GF_(F_ITEM,    gitem_t *, name)
#define E(name)   GF_(F_EDICT,   edict_t *, name)
#define T(name)   GF_(F_INT64,   gtime_t,   name)

static const save_field_t moveinfo_t_fields[] = {
#define _OFS(x) offsetof(moveinfo_t, x)
    V(start_origin),
    V(start_angles),
    V(end_origin),
    V(end_angles),
    V(end_angles_reversed),

    I(sound_start),
    I(sound_middle),
    I(sound_end),

    F(accel),
    F(speed),
    F(decel),
    F(distance),

    F(wait),

    I(state),
    O(reversing),
    V(dir),
    V(dest),
    F(current_speed),
    F(move_speed),
    F(next_speed),
    F(remaining_distance),
    F(decel_distance),
    P(endfunc, P_moveinfo_endfunc),
    P(blocked, P_moveinfo_blocked),
#undef _OFS
    { 0 }
};

static const save_field_t reinforcement_t_fields[] = {
#define _OFS(x) offsetof(reinforcement_t, x)
    L(classname),
    I(strength),
    F(radius),
    V(mins),
    V(maxs),
#undef _OFS
    { 0 }
};

static const save_field_t monsterinfo_t_fields[] = {
#define _OFS(x) offsetof(monsterinfo_t, x)
    P(active_move, P_mmove_t),
    P(next_move, P_mmove_t),
    H64(aiflags),
    I(nextframe),
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

    T(pausetime),
    T(attack_finished),
    T(fire_wait),

    V(saved_goal),
    T(search_time),
    T(trail_time),
    V(last_sighting),
    I(attack_state),
    O(lefty),
    T(idle_time),
    I(linkcount),

    I(power_armor_type),
    I(power_armor_power),

    I(initial_power_armor_type),
    I(max_power_armor_power),
    I(weapon_sound),
    I(engine_sound),

    P(blocked, P_monsterinfo_blocked),
    T(last_hint_time),
    E(goal_hint),
    I(medicTries),
    E(badMedic1),
    E(badMedic2),
    E(healer),
    P(duck, P_monsterinfo_duck),
    P(unduck, P_monsterinfo_unduck),
    P(sidestep, P_monsterinfo_sidestep),
    F(base_height),
    T(next_duck_time),
    T(duck_wait_time),
    E(last_player_enemy),
    O(blindfire),
    O(can_jump),
    O(had_visibility),
    F(drop_height),
    F(jump_height),
    T(blind_fire_delay),
    V(blind_fire_target),
    I(slots_from_commander),
    I(monster_slots),
    I(monster_used),
    E(commander),
    T(quad_time),
    T(invincible_time),
    T(double_time),

    T(surprise_time),
    I(armor_type),
    I(armor_power),
    O(close_sight_tripped),
    T(melee_debounce_time),
    T(strafe_check_time),
    I(base_health),
    I(health_scaling),
    T(next_move_time),
    T(bad_move_time),
    T(bump_time),
    T(random_change_time),
    T(path_blocked_counter),
    T(path_wait_time),
    I(combat_style),

    E(damage_attacker),
    E(damage_inflictor),
    I(damage_blood),
    I(damage_knockback),
    V(damage_from),
    I(damage_mod),

    F(fly_max_distance),
    F(fly_min_distance),
    F(fly_acceleration),
    F(fly_speed),
    V(fly_ideal_position),
    T(fly_position_time),
    O(fly_buzzard),
    O(fly_above),
    O(fly_pinned),
    O(fly_thrusters),
    T(fly_recovery_time),
    V(fly_recovery_dir),

    T(checkattack_time),
    I(start_frame),
    T(dodge_time),
    I(move_block_counter),
    T(move_block_change_time),
    T(react_to_damage_time),

    CF_(F_REINFORCEMENTS, reinforcements),
    BA(chosen_reinforcements, MAX_REINFORCEMENTS),

    T(jump_time),
#undef _OFS
    { 0 }
};

static const save_field_t bmodel_anim_t_fields[] = {
#define _OFS(x) offsetof(bmodel_anim_t, x)
    I(params[0].start),
    I(params[0].end),
    I(params[0].style),
    I(params[0].speed),
    O(params[0].nowrap),
    I(params[1].start),
    I(params[1].end),
    I(params[1].style),
    I(params[1].speed),
    O(params[1].nowrap),
    O(enabled),
    O(alternate),
    O(currently_alternate),
    T(next_tick),
#undef _OFS
    { 0 }
};

static const save_field_t player_fog_t_fields[] = {
#define _OFS(x) offsetof(player_fog_t, x)
    V(color),
    F(density),
    F(sky_factor),
#undef _OFS
    { 0 }
};

static const save_field_t player_heightfog_t_fields[] = {
#define _OFS(x) offsetof(player_heightfog_t, x)
    V(start.color),
    F(start.dist),
    V(end.color),
    F(end.dist),
    F(density),
    F(falloff),
#undef _OFS
    { 0 }
};

static const save_field_t edict_t_fields[] = {
#define _OFS FOFS
    V(s.origin),
    V(s.angles),
    V(s.old_origin),
    I(s.modelindex),
    I(s.modelindex2),
    I(s.modelindex3),
    I(s.modelindex4),
    I(s.frame),
    H(s.skinnum),
    H(s.effects),
    H(s.renderfx),
    I(s.sound),
    H(s.morefx),
    F(s.alpha),
    F(s.scale),

    // [...]

    I(r.linkcount),
    H(r.svflags),
    V(r.mins),
    V(r.maxs),
    I(r.solid),
    I(r.ownernum),

    I(spawn_count),
    I(movetype),
    H(clipmask),
    H64(flags),

    L(model),
    T(freetime),

    L(message),
    L(classname),
    H(spawnflags),

    T(timestamp),

    F(angle),
    L(target),
    L(targetname),
    L(killtarget),
    L(team),
    L(pathtarget),
    L(deathtarget),
    L(healthtarget),
    L(itemtarget),
    L(combattarget),
    E(target_ent),

    F(speed),
    F(accel),
    F(decel),
    V(movedir),
    V(pos1),
    V(pos2),
    V(pos3),

    V(velocity),
    V(avelocity),
    I(mass),
    T(air_finished),
    F(gravity),

    E(goalentity),
    E(movetarget),
    F(yaw_speed),
    F(ideal_yaw),

    T(nextthink),
    P(prethink, P_prethink),
    P(postthink, P_prethink),
    P(think, P_think),
    P(touch, P_touch),
    P(use, P_use),
    P(pain, P_pain),
    P(die, P_die),

    T(touch_debounce_time),
    T(pain_debounce_time),
    T(damage_debounce_time),
    T(fly_sound_debounce_time),
    T(last_move_time),

    I(health),
    I(max_health),
    I(gib_health),
    T(show_hostile),

    T(powerarmor_time),

    L(map),

    I(viewheight),
    O(deadflag),
    O(takedamage),
    I(dmg),
    I(radius_dmg),
    F(dmg_radius),
    I(sounds),
    I(count),

    E(chain),
    E(enemy),
    E(oldenemy),
    E(activator),
    E(groundentity),
    I(groundentity_linkcount),
    E(teamchain),
    E(teammaster),

    E(mynoise),
    E(mynoise2),

    I(noise_index),
    I(noise_index2),
    F(volume),
    F(attenuation),

    F(wait),
    F(delay),
    F(random),

    T(teleport_time),

    H(watertype),
    I(waterlevel),

    V(move_origin),
    V(move_angles),

    I(style),

    M(item),

    R(moveinfo_t, moveinfo),
    R(monsterinfo_t, monsterinfo),

    H(plat2flags),
    V(offset),
    V(gravityVector),
    E(bad_area),
    E(hint_chain),
    E(monster_hint_chain),
    E(target_hint_chain),
    I(hint_chain_id),

    SZ(clock_message, CLOCK_MESSAGE_SIZE),

    T(dead_time),
    E(beam),
    E(beam2),
    E(proboscus),
    E(disintegrator),
    T(disintegrator_time),
    H(hackflags),

    R(player_fog_t, fog_off),
    R(player_fog_t, fog),

    R(player_heightfog_t, heightfog_off),
    R(player_heightfog_t, heightfog),

    BA(item_picked_up_by, MAX_CLIENTS / 8),
    T(slime_debounce_time),

    R(bmodel_anim_t, bmodel_anim),

    L(style_on),
    L(style_off),

    H(crosslevel_flags),
    T(no_gravity_time),
    F(vision_cone),
    O(free_after_event),
#undef _OFS
    { 0 }
};

static const save_field_t level_locals_t_fields[] = {
#define _OFS(x) offsetof(level_locals_t, x)
    T(time),

    SZ(level_name, MAX_QPATH),
    SZ(mapname, MAX_QPATH),
    SZ(nextmap, MAX_QPATH),

    T(intermissiontime),
    L(changemap),
    L(achievement),
    O(exitintermission),
    O(intermission_clear),
    V(intermission_origin),
    V(intermission_angle),

    I(pic_health),
    I(snd_fry),

    I(total_secrets),
    I(found_secrets),

    I(total_goals),
    I(found_goals),

    I(total_monsters),
    I(killed_monsters),

    I(body_que),
    I(power_cubes),

    E(disguise_violator),
    T(disguise_violation_time),
    I(disguise_icon),

    T(coop_level_restart_time),

    L(goals),
    I(goal_num),

    I(vwep_offset),

    L(start_items),
    O(no_grapple),

    F(gravity),
    O(hub_map),
    E(health_bar_entities[0]),
    E(health_bar_entities[1]),
    O(story_active),
    T(next_auto_save),

    L(primary_objective_string),
    L(secondary_objective_string),
    L(primary_objective_title),
    L(secondary_objective_title),

    F(skyrotate),
    I(skyautorotate),
#undef _OFS
    { 0 }
};

static const save_field_t client_persistent_t_fields[] = {
#define _OFS(x) offsetof(client_persistent_t, x)
    SZ(userinfo, MAX_INFO_STRING),
    SZ(netname, 16),
    I(hand),
    I(autoswitch),
    I(autoshield),

    I(health),
    I(max_health),
    H64(savedFlags),

    I(selected_item),
    T(selected_item_time),
    GA_(F_INVENTORY, int, inventory, IT_TOTAL),

    GA_(F_MAX_AMMO, int16_t, max_ammo, AMMO_MAX),

    M(weapon),
    M(lastweapon),

    H(power_cubes),
    I(score),

    I(game_help1changed),
    I(game_help2changed),
    I(helpchanged),
    T(help_time),

    O(spectator),
    O(bob_skip),

    T(megahealth_time),
    I(lives),
#undef _OFS
    { 0 }
};

static const save_field_t gclient_t_fields[] = {
#define _OFS CLOFS
    I(ps.pmove.pm_type),

    V(ps.pmove.origin),
    V(ps.pmove.velocity),
    I(ps.pmove.pm_flags),
    I(ps.pmove.pm_time),
    I(ps.pmove.gravity),
    IA(ps.pmove.delta_angles, 3),

    V(ps.viewangles),
    V(ps.viewoffset),
    V(ps.kick_angles),

    V(ps.gunangles),
    V(ps.gunoffset),
    I(ps.gunindex),
    I(ps.gunframe),

    FA(ps.screen_blend, 4),
    FA(ps.damage_blend, 4),

    I(ps.fov),

    I(ps.rdflags),

    CF_(F_STATS, ps.stats),

    R(client_persistent_t, pers),

    R(client_persistent_t, resp.coop_respawn),

    T(resp.entertime),
    I(resp.score),
    O(resp.spectator),

    M(newweapon),

    F(killer_yaw),

    I(weaponstate),

    V(kick.angles),
    V(kick.origin),
    T(kick.total),
    T(kick.time),
    T(quake_time),
    F(v_dmg_roll),
    F(v_dmg_pitch),
    T(v_dmg_time),
    T(fall_time),
    F(fall_value),
    F(damage_alpha),
    F(bonus_alpha),
    V(damage_blend),
    V(v_angle),
    V(v_forward),
    F(bobtime),
    V(oldviewangles),
    V(oldvelocity),
    E(oldgroundentity),
    T(flash_time),

    T(next_drown_time),
    I(old_waterlevel),
    I(breather_sound),

    I(anim_end),
    I(anim_priority),
    O(anim_duck),
    O(anim_run),
    T(anim_time),

    T(quad_time),
    T(invincible_time),
    T(breather_time),
    T(enviro_time),
    T(invisible_time),

    O(grenade_blew_up),
    T(grenade_time),
    T(grenade_finished_time),
    T(quadfire_time),
    I(silencer_shots),
    I(weapon_sound),

    T(pickup_msg_time),

    T(respawn_time),

    T(double_time),
    T(ir_time),
    T(nuke_time),
    T(tracker_pain_time),

    T(empty_click_sound),

    E(trail_head),
    E(trail_tail),

    O(landmark_free_fall),
    SZ(landmark_name, MAX_QPATH),
    V(landmark_rel_pos),
    T(landmark_noise_time),

    T(invisibility_fade_time),
    V(last_ladder_pos),
    T(last_ladder_sound),

    E(sight_entity),
    T(sight_entity_time),
    E(sound_entity),
    T(sound_entity_time),
    E(sound2_entity),
    T(sound2_entity_time),

    R(player_fog_t, wanted_fog),
    R(player_heightfog_t, wanted_heightfog),

    T(last_firing_time),
#undef _OFS
    { 0 }
};

static const save_field_t level_entry_t_fields[] = {
#define _OFS(x) offsetof(level_entry_t, x)
    SZ(map_name, MAX_QPATH),
    SZ(pretty_name, MAX_QPATH),
    I(total_secrets),
    I(found_secrets),
    I(total_monsters),
    I(killed_monsters),
    T(time),
    I(visit_order),
#undef _OFS
    { 0 }
};

static const save_field_t game_locals_t_fields[] = {
#define _OFS(x) offsetof(game_locals_t, x)
    SZ(helpmessage1, MAX_TOKEN_CHARS),
    SZ(helpmessage2, MAX_TOKEN_CHARS),
    I(help1changed),
    I(help2changed),

    I(maxclients),

    H(cross_level_flags),
    H(cross_unit_flags),

    O(autosaved),

    RA(level_entry_t, level_entries, MAX_LEVELS_PER_UNIT),
#undef _OFS
    { 0 }
};

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

#define indent(s)   (int)(block.indent * 2 + strlen(s)), s

q_printf(1, 2)
static void write_str(const char *fmt, ...)
{
    va_list     argptr;
    char        text[MAX_STRING_CHARS * 4];
    size_t      len;

    va_start(argptr, fmt);
    len = Q_vsnprintf(text, sizeof(text), fmt, argptr);
    va_end(argptr);

    if (len >= sizeof(text))
        gi.error("oversize line");

    fs->WriteFile(text, len, g_savefile);
}

static void begin_block(const char *name)
{
    write_str("%*s {\n", indent(name));
    block.indent++;
}

static void end_block(void)
{
    block.indent--;
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

static void write_uint_hex(const char *name, unsigned v)
{
    if (v < 256)
        write_str("%*s %u\n", indent(name), v);
    else
        write_str("%*s %#x\n", indent(name), v);
}

static void write_int64(const char *name, int64_t v)
{
    write_str("%*s %"PRId64"\n", indent(name), v);
}

static void write_uint64_hex(const char *name, uint64_t v)
{
    write_str("%*s %#"PRIx64"\n", indent(name), v);
}

static void write_int16_v(const char *name, const int16_t *v, int n)
{
    write_str("%*s ", indent(name));
    for (int i = 0; i < n; i++)
        write_str("%d ", v[i]);
    write_str("\n");
}

static void write_int_v(const char *name, const int *v, int n)
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
    for (int i = 0; i < n; i++)
        write_str("%02x", p[i]);
    write_str("\n");
}

static void write_vector(const char *name, const vec_t *v)
{
    write_str("%*s %.6g %.6g %.6g\n", indent(name), v[0], v[1], v[2]);
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

    gi.error("unknown pointer of type %d: %p", type, p);
}

static void write_inventory(const int *inven)
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

static void write_stats(const int16_t *stats)
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
    case F_INT16:
        write_int16_v(field->name, p, field->count);
        break;
    case F_INT:
        write_int_v(field->name, p, field->count);
        break;
    case F_UINT:
        write_uint_hex(field->name, *(unsigned *)p);
        break;
    case F_INT64:
        write_int64(field->name, *(int64_t *)p);
        break;
    case F_UINT64:
        write_uint64_hex(field->name, *(uint64_t *)p);
        break;
    case F_BOOL:
        write_str("%*s %s\n", indent(field->name), *(bool *)p ? "true" : "false");
        break;
    case F_FLOAT:
        write_float_v(field->name, p, field->count);
        break;
    case F_VECTOR:
        write_vector(field->name, p);
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
        write_stats((const int16_t *)p);
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
    const char *filename;
} line;

static const char *read_line(void)
{
    int ret = fs->ReadLine(g_savefile, line.data, sizeof(line.data));
    if (ret < 0)
        gi.error("%s: error reading input file", line.filename);
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

    gi.error("%s: line %d: %s", line.filename, line.number, text);
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

    if (g_strict_saves->integer)
        parse_error("unknown %s: %s", what, token);

    gi.dprintf("WARNING: %s: line %d: unknown %s: %s\n", line.filename, line.number, what, token);
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

static void parse_int16_v(int16_t *v, int n)
{
    for (int i = 0; i < n; i++)
        v[i] = parse_int16();
}

static void parse_int_v(int *v, int n)
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
    if (line.len != n * 2)
        parse_error("unexpected number of characters");

    for (int i = 0; i < n; i++) {
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

static void read_vector(vec_t *v)
{
    v[0] = parse_float();
    v[1] = parse_float();
    v[2] = parse_float();
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

static void read_inventory(int *inven)
{
    expect("{");
    while (1) {
        const char *tok = parse();
        if (!strcmp(tok, "}"))
            break;
        const gitem_t *item = FindItemByClassname(tok);
        if (item)
            inven[item->id] = parse_int32();
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

static void read_stats(int16_t *stats)
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
            stats[statdefs[i].stat] = parse_int16();
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
    case F_INT16:
        parse_int16_v(p, field->count);
        break;
    case F_INT:
        parse_int_v(p, field->count);
        break;
    case F_UINT:
        *(unsigned *)p = parse_uint(UINT32_MAX);
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
    case F_VECTOR:
        read_vector((vec_t *)p);
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

static void read_fields(const save_field_t *field, void *base)
{
    expect("{");
    while (1) {
        const char *tok = parse();
        if (!strcmp(tok, "}"))
            break;
        while (field->name) {
            if (!strcmp(field->name, tok))
                break;
            field++;
        }
        if (field->name)
            read_field(field, base);
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
void WriteGame(qhandle_t handle, bool autosave)
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

void ReadGame(qhandle_t handle, const char *filename)
{
    int num;

    gi.FreeTags(TAG_GAME);

    g_savefile = handle;

    memset(&line, 0, sizeof(line));
    line.filename = filename;
    expect(SAVE_MAGIC1);
    expect("version");
    line.version = parse_int32();
    if (line.version < SAVE_VERSION_MINIMUM || line.version > SAVE_VERSION_CURRENT)
        gi.error("Savegame has bad version");

    int maxclients = game.maxclients;

    expect("game");
    read_fields(game_locals_t_fields, &game);

    // should agree with server's version
    if (game.maxclients != maxclients)
        gi.error("Savegame has bad maxclients");

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
void WriteLevel(qhandle_t handle)
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
    for (i = 0; i < globals.num_edicts; i++) {
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
void ReadLevel(qhandle_t handle, const char *filename)
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
    line.filename = filename;
    expect(SAVE_MAGIC2);
    expect("version");
    line.version = parse_int32();
    if (line.version < SAVE_VERSION_MINIMUM || line.version > SAVE_VERSION_CURRENT)
        gi.error("Savegame has bad version");

    // wipe all the entities except world
    memset(g_edicts, 0, sizeof(g_edicts[0]) * ENTITYNUM_WORLD);
    globals.num_edicts = game.maxclients;

    // load the level locals
    expect("level");
    read_fields(level_locals_t_fields, &level);

    // load all the entities
    expect("entities");
    expect("{");
    while ((entnum = parse_array(ENTITYNUM_WORLD)) != -1) {
        if (entnum >= globals.num_edicts)
            globals.num_edicts = entnum + 1;

        ent = &g_edicts[entnum];
        if (ent->r.inuse)
            parse_error("duplicate entity: %d", entnum);

        G_InitEdict(ent);
        read_fields(edict_t_fields, ent);

        // let the server rebuild world links for this ent
        gi.linkentity(ent);
    }

    // mark all clients as unconnected
    for (i = 0; i < game.maxclients; i++) {
        ent = &g_edicts[i];
        ent->client = g_clients + i;
        ent->client->pers.connected = false;
        ent->client->pers.spawned = false;
    }

    // do any load time things at this point
    for (i = 0; i < globals.num_edicts; i++) {
        ent = &g_edicts[i];

        if (!ent->r.inuse)
            continue;

        Q_assert(ent->classname);

        // fire any cross-level triggers
        if (strcmp(ent->classname, "target_crosslevel_target") == 0 ||
            strcmp(ent->classname, "target_crossunit_target") == 0)
            ent->nextthink = level.time + SEC(ent->delay);
    }

    // precache player inventory items
    G_PrecacheInventoryItems();

    // refresh global precache indices
    G_RefreshPrecaches();
}

// [Paril-KEX]
bool G_CanSave(void)
{
    if (game.maxclients == 1 && g_edicts[0].health <= 0) {
        gi.cprintf(&g_edicts[0], PRINT_HIGH, "Can't savegame while dead!\n");
        return false;
    }

    // don't allow saving during cameras/intermissions as this
    // causes the game to act weird when these are loaded
    if (level.intermissiontime) {
        gi.cprintf(&g_edicts[0], PRINT_HIGH, "Can't savegame during intermission!\n");
        return false;
    }

    return true;
}
