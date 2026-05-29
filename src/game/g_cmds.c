// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
#include "g_local.h"
#include "m_player.h"

static void SelectNextItem(edict_t *ent, cmdflags_t flags)
{
    gclient_t *cl;
    item_id_t  i, index;
    const gitem_t *it;
    item_flags_t   itflags;

    cl = ent->client;

    // ZOID
    if (!(flags & VALIDATE)) {
        if (cl->menu) {
            PMenu_Next(ent);
            return;
        }
        if (cl->chase_target) {
            ChaseNext(ent);
            return;
        }
    }
    // ZOID

    itflags = (flags & WEAPON) ? IF_WEAPON : (flags & POWERUP) ? IF_POWERUP : IF_ANY;

    // scan for the next valid one
    for (i = IT_NULL + 1; i <= IT_TOTAL; i++) {
        index = (cl->pers.selected_item + i) % IT_TOTAL;
        if (!cl->pers.inventory[index])
            continue;
        it = &itemlist[index];
        if (!it->use)
            continue;
        if (!(it->flags & itflags))
            continue;

        cl->pers.selected_item = index;
        cl->pers.selected_item_time = level.time + SELECTED_ITEM_TIME;
        cl->ps.stats[STAT_SELECTED_ITEM_NAME] = CS_ITEMS + index;
        return;
    }

    cl->pers.selected_item = IT_NULL;
}

static void SelectPrevItem(edict_t *ent, cmdflags_t flags)
{
    gclient_t *cl;
    item_id_t  i, index;
    const gitem_t *it;
    item_flags_t   itflags;

    cl = ent->client;

    // ZOID
    if (cl->menu) {
        PMenu_Prev(ent);
        return;
    }
    if (cl->chase_target) {
        ChasePrev(ent);
        return;
    }
    // ZOID

    itflags = (flags & WEAPON) ? IF_WEAPON : (flags & POWERUP) ? IF_POWERUP : IF_ANY;

    // scan for the next valid one
    for (i = IT_NULL + 1; i <= IT_TOTAL; i++) {
        index = (cl->pers.selected_item + IT_TOTAL - i) % IT_TOTAL;
        if (!cl->pers.inventory[index])
            continue;
        it = &itemlist[index];
        if (!it->use)
            continue;
        if (!(it->flags & itflags))
            continue;

        cl->pers.selected_item = index;
        cl->pers.selected_item_time = level.time + SELECTED_ITEM_TIME;
        cl->ps.stats[STAT_SELECTED_ITEM_NAME] = CS_ITEMS + index;
        return;
    }

    cl->pers.selected_item = IT_NULL;
}

void ValidateSelectedItem(edict_t *ent)
{
    gclient_t *cl;

    cl = ent->client;

    if (cl->pers.inventory[cl->pers.selected_item])
        return; // valid

    SelectNextItem(ent, VALIDATE);
}

//=================================================================================

static void SpawnAndGiveItem(edict_t *ent, item_id_t id)
{
    const gitem_t *it = GetItemByIndex(id);

    if (!it)
        return;

    edict_t *it_ent = G_Spawn();
    it_ent->classname = it->classname;
    SpawnItem(it_ent, it);

    if (it_ent->r.inuse) {
        Touch_Item(it_ent, ent, &null_trace, true);
        if (it_ent->r.inuse)
            G_FreeEdict(it_ent);
    }
}

/*
==================
Cmd_Give_f

Give items to a client
==================
*/
static void Cmd_Give_f(edict_t *ent, cmdflags_t flags)
{
    char           name[MAX_QPATH];
    char           count[MAX_QPATH];
    const gitem_t *it;
    item_id_t      index;
    int            i;
    bool           give_all;

    trap_Argv(1, name, sizeof(name));
    trap_Argv(2, count, sizeof(count));

    if (Q_strcasecmp(name, "all") == 0)
        give_all = true;
    else
        give_all = false;

    if (give_all || Q_strcasecmp(name, "health") == 0) {
        if (trap_Argc() == 3)
            ent->health = Q_atoi(count);
        else
            ent->health = ent->max_health;
        if (!give_all)
            return;
    }

    if (give_all || Q_strcasecmp(name, "weapons") == 0) {
        for (i = 0; i < IT_TOTAL; i++) {
            it = itemlist + i;
            if (!it->pickup)
                continue;
            if (!(it->flags & IF_WEAPON))
                continue;
            ent->client->pers.inventory[i] += 1;
        }
        if (!give_all)
            return;
    }

    if (give_all || Q_strcasecmp(name, "ammo") == 0) {
        if (give_all)
            SpawnAndGiveItem(ent, IT_ITEM_PACK);

        for (i = 0; i < IT_TOTAL; i++) {
            it = itemlist + i;
            if (!it->pickup)
                continue;
            if (!(it->flags & IF_AMMO))
                continue;
            Add_Ammo(ent, it, 1000);
        }
        if (!give_all)
            return;
    }

    if (give_all || Q_strcasecmp(name, "armor") == 0) {
        ent->client->pers.inventory[IT_ARMOR_JACKET] = 0;
        ent->client->pers.inventory[IT_ARMOR_COMBAT] = 0;
        ent->client->pers.inventory[IT_ARMOR_BODY] = GetItemByIndex(IT_ARMOR_BODY)->armor_info->max_count;

        if (!give_all)
            return;
    }

    if (give_all) {
        SpawnAndGiveItem(ent, IT_ITEM_POWER_SHIELD);

        for (i = 0; i < IT_TOTAL; i++) {
            it = itemlist + i;
            if (!it->pickup)
                continue;
            // ROGUE
            if (it->flags & (IF_ARMOR | IF_WEAPON | IF_AMMO | IF_NOT_GIVEABLE | IF_TECH))
                continue;
            if (it->pickup == CTFPickup_Flag)
                continue;
            if ((it->flags & IF_HEALTH) && !it->use)
                continue;
            // ROGUE
            ent->client->pers.inventory[i] = (it->flags & IF_KEY) ? 8 : 1;
        }

        G_CheckPowerArmor(ent);
        ent->client->pers.power_cubes = 0xFF;
        return;
    }

    it = FindItem(name);
    if (!it)
        it = FindItemByClassname(name);
    if (!it) {
        trap_Args(name, sizeof(name));
        it = FindItem(COM_StripQuotes(name));
    }

    if (!it) {
        G_ClientPrintf(ent, PRINT_HIGH, "Unknown item\n");
        return;
    }

    // ROGUE
    if (it->flags & IF_NOT_GIVEABLE) {
        G_ClientPrintf(ent, PRINT_HIGH, "Item cannot be given\n");
        return;
    }
    // ROGUE

    index = it->id;

    if (!it->pickup) {
        ent->client->pers.inventory[index] = 1;
        return;
    }

    if (it->flags & IF_AMMO) {
        if (trap_Argc() == 3)
            ent->client->pers.inventory[index] = Q_atoi(count);
        else
            ent->client->pers.inventory[index] += it->quantity;
    } else {
        SpawnAndGiveItem(ent, index);
    }
}

/*
==================
Cmd_God_f

Sets client to godmode

argv(0) god
==================
*/
static void Cmd_God_f(edict_t *ent, cmdflags_t flags)
{
    ent->flags ^= FL_GODMODE;
    if (!(ent->flags & FL_GODMODE))
        G_ClientPrintf(ent, PRINT_HIGH, "godmode OFF\n");
    else
        G_ClientPrintf(ent, PRINT_HIGH, "godmode ON\n");
}

/*
==================
Cmd_Immortal_f

Sets client to immortal - take damage but never go below 1 hp

argv(0) immortal
==================
*/
static void Cmd_Immortal_f(edict_t *ent, cmdflags_t flags)
{
    ent->flags ^= FL_IMMORTAL;
    if (!(ent->flags & FL_IMMORTAL))
        G_ClientPrintf(ent, PRINT_HIGH, "immortal OFF\n");
    else
        G_ClientPrintf(ent, PRINT_HIGH, "immortal ON\n");
}

/*
==================
Cmd_Resurrect_f

Resurrect dead player
==================
*/
static void Cmd_Resurrect_f(edict_t *ent, cmdflags_t flags)
{
    if (ent->health > 0)
        return;

    // clear entity values
    ent->health = ent->max_health;
    ent->deadflag = false;
    ent->takedamage = true;
    ent->flags &= ~(FL_NO_KNOCKBACK | FL_ALIVE_KNOCKBACK_ONLY | FL_NO_DAMAGE_EFFECTS);
    ent->clipmask = MASK_PLAYERSOLID;
    ent->movetype = MOVETYPE_WALK;
    ent->r.solid = SOLID_BBOX;
    ent->r.box = player_box;
    ent->r.svflags |= SVF_PLAYER;
    ent->r.svflags &= ~SVF_DEADMONSTER;
    ent->s.modelindex = MODELINDEX_PLAYER;
    ent->s.modelindex2 = MODELINDEX_PLAYER;
    ent->s.frame = 0;
    ent->s.skinnum = 0;
    ent->client->anim_priority = ANIM_BASIC;
    ent->client->ps.pm_type = PM_NORMAL;
    ent->air_finished = level.time + SEC(12);
    ent->dead_time = 0;

    trap_LinkEntity(ent);
    G_FixStuckObject(ent, ent->s.origin);

    // force the current weapon up
    ent->client->newweapon = ent->client->pers.lastweapon;
    ChangeWeapon(ent);
}

/*
==================
Cmd_Target_f

Fire specific targets
==================
*/
static void Cmd_Target_f(edict_t *ent, cmdflags_t flags)
{
    char buf[MAX_QPATH];
    trap_Argv(1, buf, sizeof(buf));

    ent->target = buf;
    G_UseTargets(ent, ent);
    ent->target = NULL;
}

static void Cmd_Target_c(int firstarg, int argnum)
{
    if (argnum != 1)
        return;

    trap_SetCompletionOptions(CMPL_CASELESS | CMPL_CHECKDUPS);

    for (int i = game.maxclients + BODY_QUEUE_SIZE; i < level.num_edicts; i++) {
        const edict_t *ent = &g_edicts[i];
        if (!ent->r.inuse)
            continue;
        if (ent->targetname)
            trap_AddCommandCompletion(ent->targetname);
    }
}

/*
=================
Cmd_Spawn_f

Spawn class name

argv(0) spawn
argv(1) <classname>
argv(2+n) "key"...
argv(3+n) "value"...
=================
*/
static void Cmd_Spawn_f(edict_t *ent, cmdflags_t flags)
{
    char buf[MAX_QPATH];
    trap_Argv(1, buf, sizeof(buf));

    solid_t backup = ent->r.solid;
    ent->r.solid = SOLID_NOT;
    trap_LinkEntity(ent);

    edict_t *other = G_Spawn();
    other->classname = G_CopyString(buf);

    vec3_t forward;
    AngleVectors(ent->client->v_angle, &forward, NULL, NULL);

    other->s.origin = Vec3_MA(ent->s.origin, 24, forward);
    other->s.angles.yaw = ent->s.angles.yaw;

    ED_InitSpawnVars();

    int argc = trap_Argc();
    if (argc > 3) {
        char key[MAX_QPATH];
        char val[MAX_QPATH];
        for (int i = 2; i < argc; i += 2) {
            trap_Argv(i, key, sizeof(key));
            trap_Argv(i + 1, val, sizeof(val));
            ED_ParseField(key, val, other);
        }
    }

    ED_CallSpawn(other);

    if (other->r.inuse) {
        vec3_t start, end;

        start = ent->s.origin;
        start.z += ent->viewheight;

        end = Vec3_MA(start, 8192, forward);

        trace_t tr = G_TraceLine(start, end, other->s.number, MASK_SHOT | CONTENTS_MONSTERCLIP);
        other->s.origin = tr.endpos;

        for (int i = 0; i < 3; i++) {
            if (tr.plane.normal.xyz[i] > 0)
                other->s.origin.xyz[i] -= other->r.box.mins.xyz[i] * tr.plane.normal.xyz[i];
            else
                other->s.origin.xyz[i] += other->r.box.maxs.xyz[i] * -tr.plane.normal.xyz[i];
        }

        while (1) {
            tr = G_Trace(other->s.origin, other->s.origin, other->r.box,
                         other->s.number, MASK_SHOT | CONTENTS_MONSTERCLIP);
            if (!tr.startsolid)
                break;

            float f = Vec2_Length(Vec2_FromVec3(Box3_Size(other->r.box)));

            other->s.origin = Vec3_MA(other->s.origin, -f, forward);
            vec3_t dir = Vec3_Sub(other->s.origin, ent->s.origin);

            if (Vec3_Dot(dir, forward) < 0) {
                G_ClientPrintf(ent, PRINT_HIGH, "Couldn't find a suitable spawn location\n");
                G_FreeEdict(other);
                break;
            }
        }

        if (other->r.inuse)
            trap_LinkEntity(other);

        if ((other->r.svflags & SVF_MONSTER) && other->think)
            other->think(other);
    }

    ent->r.solid = backup;
    trap_LinkEntity(ent);
}

/*
=================
Cmd_Teleport_f

Teleport

argv(0) teleport
argv(1) x
argv(2) y
argv(3) z
=================
*/
static void Cmd_Teleport_f(edict_t *ent, cmdflags_t flags)
{
    char buf[MAX_QPATH];
    int i;

    if (trap_Argc() < 4) {
        G_ClientPrintf(ent, PRINT_HIGH, "Not enough args; teleport x y z\n");
        return;
    }

    for (i = 0; i < 3; i++) {
        trap_Argv(1 + i, buf, sizeof(buf));
        ent->s.origin.xyz[i] = Q_atof(buf);
    }

    if (trap_Argc() > 4) {
        vec3_t ang;

        for (i = 0; i < 3; i++) {
            trap_Argv(4 + i, buf, sizeof(buf));
            ang.xyz[i] = Q_atof(buf);
        }

        P_SetClientAngles(ent->client, ang);
    }

    trap_LinkEntity(ent);
}

/*
==================
Cmd_Notarget_f

Sets client to notarget

argv(0) notarget
==================
*/
static void Cmd_Notarget_f(edict_t *ent, cmdflags_t flags)
{
    ent->flags ^= FL_NOTARGET;
    if (!(ent->flags & FL_NOTARGET))
        G_ClientPrintf(ent, PRINT_HIGH, "notarget OFF\n");
    else
        G_ClientPrintf(ent, PRINT_HIGH, "notarget ON\n");
}

/*
==================
Cmd_Novisible_f

Sets client to "super notarget"

argv(0) notarget
==================
*/
static void Cmd_Novisible_f(edict_t *ent, cmdflags_t flags)
{
    ent->flags ^= FL_NOVISIBLE;
    if (!(ent->flags & FL_NOVISIBLE))
        G_ClientPrintf(ent, PRINT_HIGH, "novisible OFF\n");
    else
        G_ClientPrintf(ent, PRINT_HIGH, "novisible ON\n");
}

static void Cmd_Nodrown_f(edict_t *ent, cmdflags_t flags)
{
    ent->flags ^= FL_DEEPONE;
    if (!(ent->flags & FL_DEEPONE))
        G_ClientPrintf(ent, PRINT_HIGH, "nodrown OFF\n");
    else
        G_ClientPrintf(ent, PRINT_HIGH, "nodrown ON\n");
}

/*
==================
Cmd_Noclip_f

argv(0) noclip
==================
*/
static void Cmd_Noclip_f(edict_t *ent, cmdflags_t flags)
{
    if (ent->movetype == MOVETYPE_NOCLIP) {
        ent->movetype = MOVETYPE_WALK;
        ent->r.svflags &= ~SVF_DEADMONSTER;
        G_ClientPrintf(ent, PRINT_HIGH, "noclip OFF\n");
    } else {
        ent->movetype = MOVETYPE_NOCLIP;
        ent->r.svflags |= SVF_DEADMONSTER;
        G_ClientPrintf(ent, PRINT_HIGH, "noclip ON\n");
    }
}

/*
==================
Cmd_Use_f

Use an inventory item
==================
*/
static void Cmd_Use_f(edict_t *ent, cmdflags_t flags)
{
    item_id_t index;
    const gitem_t *it;
    const char *s;
    char buf[MAX_QPATH];

    if (ent->health <= 0 || ent->deadflag)
        return;

    trap_Args(buf, sizeof(buf));
    s = COM_StripQuotes(buf);

    if (flags & BY_INDEX)
        it = GetItemByIndex(Q_atoi(s));
    else
        it = FindItem(s);

    if (!it) {
        G_ClientPrintf(ent, PRINT_HIGH, "Unknown item: %s\n", s);
        return;
    }
    if (!it->use) {
        G_ClientPrintf(ent, PRINT_HIGH, "Item is not usable.\n");
        return;
    }
    index = it->id;

    // Paril: Use_Weapon handles weapon availability
    if (!(it->flags & IF_WEAPON) && !ent->client->pers.inventory[index]) {
        G_ClientPrintf(ent, PRINT_HIGH, "Out of item: %s\n", it->pickup_name);
        return;
    }

    // allow weapon chains for use
    ent->client->no_weapon_chains = flags & NO_CHAINS;

    it->use(ent, it);

    ValidateSelectedItem(ent);
}

/*
==================
Cmd_Drop_f

Drop an inventory item
==================
*/
static void Cmd_Drop_f(edict_t *ent, cmdflags_t flags)
{
    item_id_t index;
    const gitem_t *it;
    const char *s;
    char buf[MAX_QPATH];

    if (ent->health <= 0 || ent->deadflag)
        return;

    trap_Args(buf, sizeof(buf));
    s = COM_StripQuotes(buf);

    // ZOID--special case for tech powerups
    if (Q_strcasecmp(s, "tech") == 0) {
        it = CTFWhat_Tech(ent);

        if (it) {
            it->drop(ent, it);
            ValidateSelectedItem(ent);
        }

        return;
    }
    // ZOID

    if (flags & BY_INDEX)
        it = GetItemByIndex(Q_atoi(s));
    else
        it = FindItem(s);

    if (!it) {
        G_ClientPrintf(ent, PRINT_HIGH, "Unknown item: %s\n", s);
        return;
    }
    if (!G_CanDropItem(it)) {
        G_ClientPrintf(ent, PRINT_HIGH, "Item is not droppable.\n");
        return;
    }
    index = it->id;
    if (!ent->client->pers.inventory[index]) {
        G_ClientPrintf(ent, PRINT_HIGH, "Out of item: %s\n", it->pickup_name);
        return;
    }

    it->drop(ent, it);

    ValidateSelectedItem(ent);
}

static void Cmd_Item_c(int firstarg, int argnum)
{
    if (argnum != 1)
        return;

    trap_SetCompletionOptions(CMPL_CASELESS | CMPL_CHECKDUPS | CMPL_STRIPQUOTES);

    for (int i = IT_NULL + 1; i < IT_TOTAL; i++)
        if (itemlist[i].pickup_name)
            trap_AddCommandCompletion(itemlist[i].pickup_name);
}

/*
=================
Cmd_Inven_f
=================
*/
static void Cmd_Inven_f(edict_t *ent, cmdflags_t flags)
{
    char       text[MAX_STRING_CHARS];
    int        i, count;
    gclient_t *cl;

    cl = ent->client;

    cl->showscores = false;
    cl->showhelp = false;

    // ZOID
    if (ent->client->menu) {
        PMenu_Close(ent);
        ent->client->update_chase = true;
        return;
    }
    // ZOID

    if (cl->showinventory) {
        cl->showinventory = false;
        return;
    }

    // ZOID
    if (G_TeamplayEnabled() && cl->resp.ctf_team == CTF_NOTEAM) {
        CTFOpenJoinMenu(ent);
        return;
    }
    // ZOID

    cl->showinventory = true;

    for (i = count = 0; i < IT_TOTAL; i++)
        if (cl->pers.inventory[i])
            count = i + 1;

    Q_strlcpy(text, "inven", sizeof(text));
    for (i = 0; i < count; i++)
        Q_strlcat(text, va(" %d", cl->pers.inventory[i]), sizeof(text));

    trap_ClientCommand(ent, text, true);
}

/*
=================
Cmd_InvUse_f
=================
*/
static void Cmd_InvUse_f(edict_t *ent, cmdflags_t flags)
{
    const gitem_t *it;

    // ZOID
    if (ent->client->menu) {
        PMenu_Select(ent);
        return;
    }
    // ZOID

    if (ent->health <= 0 || ent->deadflag)
        return;

    ValidateSelectedItem(ent);

    if (ent->client->pers.selected_item == IT_NULL) {
        G_ClientPrintf(ent, PRINT_HIGH, "No item to use.\n");
        return;
    }

    it = &itemlist[ent->client->pers.selected_item];
    if (!it->use) {
        G_ClientPrintf(ent, PRINT_HIGH, "Item is not usable.\n");
        return;
    }

    // don't allow weapon chains for invuse
    ent->client->no_weapon_chains = true;
    it->use(ent, it);

    ValidateSelectedItem(ent);
}

/*
=================
Cmd_WeapPrev_f
=================
*/
static void Cmd_WeapPrev_f(edict_t *ent, cmdflags_t flags)
{
    gclient_t *cl;
    item_id_t  i, index;
    const gitem_t *it;
    item_id_t  selected_weapon;

    cl = ent->client;

    if (ent->health <= 0 || ent->deadflag)
        return;
    if (!cl->pers.weapon)
        return;

    // don't allow weapon chains for weapprev
    cl->no_weapon_chains = true;

    selected_weapon = cl->pers.weapon->id;

    // scan for the next valid one
    for (i = IT_NULL + 1; i <= IT_TOTAL; i++) {
        // PMM - prevent scrolling through ALL weapons
        index = (selected_weapon + IT_TOTAL - i) % IT_TOTAL;
        if (!cl->pers.inventory[index])
            continue;
        it = &itemlist[index];
        if (!it->use)
            continue;
        if (!(it->flags & IF_WEAPON))
            continue;
        it->use(ent, it);
        // ROGUE
        if (cl->newweapon == it)
            return; // successful
        // ROGUE
    }
}

/*
=================
Cmd_WeapNext_f
=================
*/
static void Cmd_WeapNext_f(edict_t *ent, cmdflags_t flags)
{
    gclient_t *cl;
    item_id_t  i, index;
    const gitem_t *it;
    item_id_t  selected_weapon;

    cl = ent->client;

    if (ent->health <= 0 || ent->deadflag)
        return;
    if (!cl->pers.weapon)
        return;

    // don't allow weapon chains for weapnext
    cl->no_weapon_chains = true;

    selected_weapon = cl->pers.weapon->id;

    // scan for the next valid one
    for (i = IT_NULL + 1; i <= IT_TOTAL; i++) {
        // PMM - prevent scrolling through ALL weapons
        index = (selected_weapon + i) % IT_TOTAL;
        if (!cl->pers.inventory[index])
            continue;
        it = &itemlist[index];
        if (!it->use)
            continue;
        if (!(it->flags & IF_WEAPON))
            continue;
        it->use(ent, it);
        // PMM - prevent scrolling through ALL weapons

        // ROGUE
        if (cl->newweapon == it)
            return;
        // ROGUE
    }
}

/*
=================
Cmd_WeapLast_f
=================
*/
static void Cmd_WeapLast_f(edict_t *ent, cmdflags_t flags)
{
    gclient_t *cl;
    int        index;
    const gitem_t *it;

    cl = ent->client;

    if (ent->health <= 0 || ent->deadflag)
        return;
    if (!cl->pers.weapon || !cl->pers.lastweapon)
        return;

    // don't allow weapon chains for weaplast
    cl->no_weapon_chains = true;

    index = cl->pers.lastweapon->id;
    if (!cl->pers.inventory[index])
        return;
    it = &itemlist[index];
    if (!it->use)
        return;
    if (!(it->flags & IF_WEAPON))
        return;
    it->use(ent, it);
}

/*
=================
Cmd_InvDrop_f
=================
*/
static void Cmd_InvDrop_f(edict_t *ent, cmdflags_t flags)
{
    const gitem_t *it;

    if (ent->health <= 0 || ent->deadflag)
        return;

    ValidateSelectedItem(ent);

    if (ent->client->pers.selected_item == IT_NULL) {
        G_ClientPrintf(ent, PRINT_HIGH, "No item to drop.\n");
        return;
    }

    it = &itemlist[ent->client->pers.selected_item];
    if (!G_CanDropItem(it)) {
        G_ClientPrintf(ent, PRINT_HIGH, "Item is not droppable.\n");
        return;
    }
    it->drop(ent, it);

    ValidateSelectedItem(ent);
}

/*
=================
Cmd_Kill_f
=================
*/
static void Cmd_Kill_f(edict_t *ent, cmdflags_t flags)
{
    // ZOID
    if (ent->client->resp.spectator)
        return;
    // ZOID

    if ((level.time - ent->client->respawn_time) < SEC(5))
        return;

    ent->flags &= ~FL_GODMODE;
    ent->health = 0;

    // ROGUE
    //  make sure no trackers are still hurting us.
    if (ent->client->tracker_pain_time)
        RemoveAttackingPainDaemons(ent);

    if (ent->client->owned_sphere) {
        G_FreeEdict(ent->client->owned_sphere);
        ent->client->owned_sphere = NULL;
    }
    // ROGUE

    // [Paril-KEX] don't allow kill to take points away in TDM
    player_die(ent, ent, ent, 100000, vec3_origin, teamplay.integer ? MOD_SUICIDE_NP : MOD_SUICIDE);
}

/*
=================
Cmd_Kill_AI_f

Kill spawned monsters, free unspawned ones
=================
*/
static void Cmd_Kill_AI_f(edict_t *ent, cmdflags_t flags)
{
    for (int i = game.maxclients + BODY_QUEUE_SIZE; i < level.num_edicts; i++) {
        edict_t *edict = &g_edicts[i];
        if (!edict->r.inuse)
            continue;
        if (!(edict->r.svflags & SVF_MONSTER))
            continue;
        if (edict->health < 1)
            continue;

        if (edict->r.svflags & SVF_NOCLIENT) {
            G_MonsterKilled(edict);
            G_FreeEdict(edict);
            continue;
        }

        // kill it next frame
        edict->health = 0;
        edict->enemy = ent;
        edict->monsterinfo.damage_attacker = ent;
        edict->monsterinfo.damage_inflictor = world;
        edict->monsterinfo.damage_blood = 1;
        edict->monsterinfo.damage_knockback = 0;
        edict->monsterinfo.damage_from = edict->s.origin;
        edict->monsterinfo.damage_mod = MOD_UNKNOWN;
    }
}

/*
=================
Cmd_Where_f
=================
*/
static void Cmd_Where_f(edict_t *ent, cmdflags_t flags)
{
    G_ClientPrintf(ent, PRINT_HIGH, "Location: %s %s\n", vtos(ent->s.origin), vtos(ent->client->ps.viewangles));
}

/*
=================
Cmd_Clear_AI_Enemy_f
=================
*/
static void Cmd_Clear_AI_Enemy_f(edict_t *ent, cmdflags_t flags)
{
    for (int i = game.maxclients + BODY_QUEUE_SIZE; i < level.num_edicts; i++) {
        edict_t *edict = &g_edicts[i];
        if (!edict->r.inuse)
            continue;
        if (!(edict->r.svflags & SVF_MONSTER))
            continue;
        edict->monsterinfo.aiflags |= AI_FORGET_ENEMY;
    }
}

/*
=================
Cmd_AlertAll_f
=================
*/
static void Cmd_AlertAll_f(edict_t *ent, cmdflags_t flags)
{
    for (int i = game.maxclients + BODY_QUEUE_SIZE; i < level.num_edicts; i++) {
        edict_t *t = &g_edicts[i];

        if (!t->r.inuse || t->health <= 0 || !(t->r.svflags & SVF_MONSTER))
            continue;

        t->enemy = ent;
        FoundTarget(t);
    }
}

/*
==================
Cmd_Score_f

Display the scoreboard
==================
*/
static void Cmd_Score_f(edict_t *ent, cmdflags_t flags)
{
    if (level.intermissiontime)
        return;

    ent->client->showinventory = false;
    ent->client->showhelp = false;

    // ZOID
    if (ent->client->menu)
        PMenu_Close(ent);
    // ZOID

    if (!deathmatch.integer && !coop.integer)
        return;

    if (ent->client->showscores) {
        ent->client->showscores = false;
        ent->client->update_chase = true;
        return;
    }

    ent->client->showscores = true;
    DeathmatchScoreboard(ent);
}

/*
==================
Cmd_Help_f

Display the current help message
==================
*/
void Cmd_Help_f(edict_t *ent, cmdflags_t flags)
{
    // this is for backwards compatibility
    if (deathmatch.integer) {
        Cmd_Score_f(ent, flags);
        return;
    }

    if (level.intermissiontime)
        return;

    ent->client->showinventory = false;
    ent->client->showscores = false;

    if (ent->client->showhelp &&
        (ent->client->pers.game_help1changed == game.help1changed ||
         ent->client->pers.game_help2changed == game.help2changed)) {
        ent->client->showhelp = false;
        return;
    }

    ent->client->showhelp = true;
    ent->client->pers.helpchanged = 0;
    HelpComputer(ent);
}

/*
=================
Cmd_PutAway_f
=================
*/
static void Cmd_PutAway_f(edict_t *ent, cmdflags_t flags)
{
    ent->client->showscores = false;
    ent->client->showhelp = false;
    ent->client->showinventory = false;

    // ZOID
    if (ent->client->menu)
        PMenu_Close(ent);
    ent->client->update_chase = true;
    // ZOID
}

static int PlayerSort(const void *a, const void *b)
{
    int anum, bnum;

    anum = *(const int *)a;
    bnum = *(const int *)b;

    anum = g_clients[anum].ps.stats[STAT_FRAGS];
    bnum = g_clients[bnum].ps.stats[STAT_FRAGS];

    if (anum < bnum)
        return -1;
    if (anum > bnum)
        return 1;
    return 0;
}

#define MAX_IDEAL_PACKET_SIZE   1024

/*
=================
Cmd_Players_f
=================
*/
static void Cmd_Players_f(edict_t *ent, cmdflags_t flags)
{
    int     i;
    int     count;
    char    small[64];
    char    large[MAX_IDEAL_PACKET_SIZE];
    int     index[MAX_CLIENTS];

    count = 0;
    for (i = 0; i < game.maxclients; i++)
        if (g_clients[i].pers.connected) {
            index[count] = i;
            count++;
        }

    // sort by frags
    qsort(index, count, sizeof(index[0]), PlayerSort);

    // print information
    large[0] = 0;

    for (i = 0; i < count; i++) {
        Q_snprintf(small, sizeof(small), "%3i %s\n",
                   g_clients[index[i]].ps.stats[STAT_FRAGS],
                   g_clients[index[i]].pers.netname);
        if (strlen(small) + strlen(large) > sizeof(large) - 50) {
            // can't print all of them in one packet
            strcat(large, "...\n");
            break;
        }
        strcat(large, small);
    }

    G_ClientPrintf(ent, PRINT_HIGH, "%s\n%i players\n", large, count);
}

bool CheckFlood(edict_t *ent)
{
    int        i;
    gclient_t *cl;

    if (flood_msgs.integer) {
        cl = ent->client;

        if (level.time < cl->flood_locktill) {
            G_ClientPrintf(ent, PRINT_HIGH, "You can't talk for %.f more seconds\n",
                           TO_SEC(cl->flood_locktill - level.time));
            return true;
        }
        i = cl->flood_whenhead - flood_msgs.integer + 1;
        if (i < 0)
            i = (sizeof(cl->flood_when) / sizeof(cl->flood_when[0])) + i;
        if (i >= q_countof(cl->flood_when))
            i = 0;
        if (cl->flood_when[i] && level.time - cl->flood_when[i] < SEC(flood_persecond.value)) {
            cl->flood_locktill = level.time + SEC(flood_waitdelay.value);
            G_ClientPrintf(ent, PRINT_CHAT, "You can't talk for %d more seconds\n",
                           flood_waitdelay.integer);
            return true;
        }
        cl->flood_whenhead = (cl->flood_whenhead + 1) % (sizeof(cl->flood_when) / sizeof(cl->flood_when[0]));
        cl->flood_when[cl->flood_whenhead] = level.time;
    }
    return false;
}

/*
=================
Cmd_Wave_f
=================
*/
static void Cmd_Wave_f(edict_t *ent, cmdflags_t flags)
{
    // no dead or noclip waving
    if (ent->deadflag || ent->movetype == MOVETYPE_NOCLIP)
        return;

    // can't wave when ducked
    bool do_animate = ent->client->anim_priority <= ANIM_WAVE && !(ent->client->ps.pm_flags & PMF_DUCKED);

    if (do_animate)
        ent->client->anim_priority = ANIM_WAVE;

    const char *other_notify_msg = NULL, *other_notify_none_msg = NULL;

    ray3_t aim = P_ProjectSource(ent, ent->client->v_angle, vec3_origin, false);

    // see who we're aiming at
    edict_t *aiming_at = NULL;
    float best_dist = -9999;

    for (int i = 0; i < game.maxclients; i++) {
        edict_t *player = &g_edicts[i];
        if (!player->r.inuse || player == ent)
            continue;

        vec3_t dir = Vec3_Sub(player->s.origin, aim.start);
        float dist = Vec3_Normalize(&dir);

        if (Vec3_Dot(dir, ent->client->v_forward) < 0.97f)
            continue;
        if (dist < best_dist)
            continue;

        best_dist = dist;
        aiming_at = player;
    }

    char buf[MAX_QPATH];
    trap_Argv(1, buf, sizeof(buf));
    int cmd = Q_atoi(buf);

    switch (cmd) {
    case GESTURE_FLIP_OFF:
        other_notify_msg = "%s flipped the bird at %s.\n";
        other_notify_none_msg = "%s flipped the bird.\n";
        if (do_animate) {
            ent->s.frame = FRAME_flip01 - 1;
            ent->client->anim_end = FRAME_flip12;
        }
        break;
    case GESTURE_SALUTE:
        other_notify_msg = "%s salutes %s.\n";
        other_notify_none_msg = "%s salutes.\n";
        if (do_animate) {
            ent->s.frame = FRAME_salute01 - 1;
            ent->client->anim_end = FRAME_salute11;
        }
        break;
    case GESTURE_TAUNT:
        other_notify_msg = "%s taunts %s.\n";
        other_notify_none_msg = "%s taunts.\n";
        if (do_animate) {
            ent->s.frame = FRAME_taunt01 - 1;
            ent->client->anim_end = FRAME_taunt17;
        }
        break;
    case GESTURE_WAVE:
        other_notify_msg = "%s waves at %s.\n";
        other_notify_none_msg = "%s waves.\n";
        if (do_animate) {
            ent->s.frame = FRAME_wave01 - 1;
            ent->client->anim_end = FRAME_wave11;
        }
        break;
    case GESTURE_POINT:
    default:
        other_notify_msg = "%s points at %s.\n";
        other_notify_none_msg = "%s points.\n";
        if (do_animate) {
            ent->s.frame = FRAME_point01 - 1;
            ent->client->anim_end = FRAME_point12;
        }
        break;
    }

    if (CheckFlood(ent))
        return;

    bool has_a_target = false;

    if (cmd == GESTURE_POINT) {
        for (int i = 0; i < game.maxclients; i++) {
            edict_t *player = &g_edicts[i];
            if (player->r.inuse && player != ent && OnSameTeam(ent, player)) {
                has_a_target = true;
                break;
            }
        }
    }

    if (cmd == GESTURE_POINT && has_a_target) {
        // don't do this stuff if we're flooding
        vec3_t end = Vec3_MA(aim.start, 2048, ent->client->v_forward);
        trace_t tr = G_TraceLine(aim.start, end, ent->s.number, MASK_SHOT & ~CONTENTS_WINDOW);
        other_notify_msg = "%s pinged a location.\n";

        if (tr.fraction != 1.0f) {
            // send to all teammates
            for (int i = 0; i < game.maxclients; i++) {
                edict_t *player = &g_edicts[i];
                if (!player->r.inuse)
                    continue;
                if (player != ent && !OnSameTeam(ent, player))
                    continue;

                trap_ClientCommand(player, va("ping %d %s %d", ent->s.number, vtoa(tr.endpos), level.pic_ping), false);
                G_ClientPrintf(player, PRINT_HIGH, other_notify_msg, ent->client->pers.netname);
            }
        }
    } else {
        edict_t *targ = NULL;
        while ((targ = findradius(targ, ent->s.origin, 1024)) != NULL) {
            if (ent == targ)
                continue;
            if (!targ->client)
                continue;
            if (!trap_InVis(ent->s.origin, targ->s.origin, VIS_PVS | VIS_NOAREAS))
                continue;

            if (aiming_at && other_notify_msg)
                G_ClientPrintf(targ, PRINT_HIGH, other_notify_msg, ent->client->pers.netname, aiming_at->client->pers.netname);
            else if (other_notify_none_msg)
                G_ClientPrintf(targ, PRINT_HIGH, other_notify_none_msg, ent->client->pers.netname);
        }

        if (aiming_at && other_notify_msg)
            G_ClientPrintf(ent, PRINT_HIGH, other_notify_msg, ent->client->pers.netname, aiming_at->client->pers.netname);
        else if (other_notify_none_msg)
            G_ClientPrintf(ent, PRINT_HIGH, other_notify_none_msg, ent->client->pers.netname);
    }

    ent->client->anim_time = 0;
}

/*
==================
Cmd_Say_f
==================
*/
static void Cmd_Say_f(edict_t *ent, cmdflags_t flags)
{
    int     j;
    edict_t *other;
    char    text[152];
    char    buf[152];

    if (trap_Argc() < 2 && !(flags & ZERO_ARG))
        return;

    if (CheckFlood(ent))
        return;

    Q_snprintf(text, sizeof(text), "%s: ", ent->client->pers.netname);

    if (flags & ZERO_ARG) {
        trap_Argv(0, buf, sizeof(buf));
        Q_strlcat(text, buf, sizeof(text));
        Q_strlcat(text, " ", sizeof(text));
        trap_Args(buf, sizeof(buf));
        Q_strlcat(text, buf, sizeof(text));
    } else {
        trap_Args(buf, sizeof(buf));
        Q_strlcat(text, COM_StripQuotes(buf), sizeof(text));
    }

    // don't let text be too long for malicious reasons
    text[150] = 0;

    Q_strlcat(text, "\n", sizeof(text));

    if (sv_dedicated.integer)
        Com_LPrintf(PRINT_TALK, "%s", text);

    for (j = 0; j < game.maxclients; j++) {
        other = &g_edicts[j];
        if (!other->r.inuse)
            continue;
        if (!other->client)
            continue;
        G_ClientPrintf(other, PRINT_CHAT, "%s", text);
    }
}

static void Cmd_PlayerList_f(edict_t *ent, cmdflags_t flags)
{
    int i;
    char st[80];
    char text[MAX_IDEAL_PACKET_SIZE];
    edict_t *e2;

    // connect time, ping, score, name
    *text = 0;
    for (i = 0, e2 = g_edicts; i < game.maxclients; i++, e2++) {
        if (!e2->r.inuse)
            continue;

        int sec = TO_SEC(level.time - e2->client->resp.entertime);
        Q_snprintf(st, sizeof(st), "%02d:%02d %4d %3d %s%s\n",
                   sec / 60, sec % 60,
                   e2->client->ping,
                   e2->client->resp.score,
                   e2->client->pers.netname,
                   e2->client->resp.spectator ? " (spectator)" : "");
        if (strlen(text) + strlen(st) > sizeof(text) - 50) {
            if (strlen(text) < sizeof(text) - 12)
                strcat(text, "And more...\n");
            G_ClientPrintf(ent, PRINT_HIGH, "%s", text);
            return;
        }
        strcat(text, st);
    }

    if (*text)
        G_ClientPrintf(ent, PRINT_HIGH, "%s", text);
}

static void Cmd_ListMonsters_f(edict_t *ent, cmdflags_t flags)
{
    int monsters = 0;

    for (int i = game.maxclients + BODY_QUEUE_SIZE; i < level.num_edicts; i++) {
        edict_t *e = &g_edicts[i];

        if (!e->r.inuse)
            continue;
        if (!(e->r.svflags & SVF_MONSTER) || (e->monsterinfo.aiflags & AI_DO_NOT_COUNT))
            continue;
        if (e->deadflag)
            continue;

        G_ClientPrintf(ent, PRINT_HIGH, "%s\n", etos(e));
        monsters++;
    }

    G_ClientPrintf(ent, PRINT_HIGH, "%d monsters listed\n", monsters);
}

static void Cmd_ShowMonsters_f(edict_t *ent, cmdflags_t flags)
{
    int monsters = 0;

    trap_R_ClearDebugLines();

    for (int i = game.maxclients + BODY_QUEUE_SIZE; i < level.num_edicts; i++) {
        edict_t *e = &g_edicts[i];
        uint32_t color;

        if (!e->r.inuse)
            continue;
        if (!(e->r.svflags & SVF_MONSTER) || (e->monsterinfo.aiflags & AI_DO_NOT_COUNT))
            continue;
        if (e->deadflag)
            continue;

        if (e->r.svflags & SVF_NOCLIENT)
            color = U32_BLUE;
        else
            color = U32_RED;
        trap_R_AddDebugBounds(e->r.absbox, color, 10000, false);
        monsters++;
    }

    G_ClientPrintf(ent, PRINT_HIGH, "%d monsters shown\n", monsters);
}

static void Cmd_ShowSecrets_f(edict_t *self, cmdflags_t flags)
{
    edict_t *ent, *other;
    int secrets = 0;
    bool found;

    trap_R_ClearDebugLines();
    ent = NULL;
    while ((ent = G_Find(ent, FOFS(classname), "target_secret"))) {
        if (!ent->targetname)
            continue;

        other = NULL;
        found = false;
        while ((other = G_Find(other, FOFS(target), ent->targetname))) {
            trap_R_AddDebugBounds(other->r.absbox, U32_RED, 10000, false);
            found = true;
        }

        if (found)
            secrets++;
    }

    G_ClientPrintf(self, PRINT_HIGH, "%d secrets revealed\n", secrets);
}

typedef struct {
    const char *name;
    void (*func)(edict_t *ent, cmdflags_t flags);
    cmdflags_t flags;
    void (*comp)(int firstarg, int argnum);
} clientcmd_t;

static const clientcmd_t clientcmds[] = {
    { "players", Cmd_Players_f, INTERMISS },
    { "say", Cmd_Say_f, INTERMISS },
    { "score", Cmd_Score_f, INTERMISS },
    { "help", Cmd_Help_f, INTERMISS },
    { "listmonsters", Cmd_ListMonsters_f, SP_ONLY },
    { "showmonsters", Cmd_ShowMonsters_f, SP_ONLY },
    { "showsecrets", Cmd_ShowSecrets_f, SP_ONLY },
    { "use", Cmd_Use_f, 0, Cmd_Item_c },
    { "use_only", Cmd_Use_f, NO_CHAINS, Cmd_Item_c },
    { "use_index", Cmd_Use_f, BY_INDEX },
    { "use_index_only", Cmd_Use_f, NO_CHAINS | BY_INDEX },
    { "drop", Cmd_Drop_f, 0, Cmd_Item_c },
    { "drop_index", Cmd_Drop_f, BY_INDEX },
    { "give", Cmd_Give_f, CHEAT, Cmd_Item_c },
    { "god", Cmd_God_f, CHEAT },
    { "immortal", Cmd_Immortal_f, CHEAT },
    { "resurrect", Cmd_Resurrect_f, CHEAT },
    { "target", Cmd_Target_f, SP_ONLY, Cmd_Target_c },
    { "spawn", Cmd_Spawn_f, SP_ONLY, Cmd_Spawn_c },
    { "teleport", Cmd_Teleport_f, CHEAT },
    { "notarget", Cmd_Notarget_f, CHEAT },
    { "novisible", Cmd_Novisible_f, CHEAT },
    { "nodrown", Cmd_Nodrown_f, CHEAT },
    { "noclip", Cmd_Noclip_f, CHEAT },
    { "inven", Cmd_Inven_f },
    { "invnext", SelectNextItem },
    { "invprev", SelectPrevItem },
    { "invnextw", SelectNextItem, WEAPON },
    { "invprevw", SelectPrevItem, WEAPON },
    { "invnextp", SelectNextItem, POWERUP },
    { "invprevp", SelectPrevItem, POWERUP },
    { "invuse", Cmd_InvUse_f },
    { "invdrop", Cmd_InvDrop_f },
    { "weapprev", Cmd_WeapPrev_f },
    { "weapnext", Cmd_WeapNext_f },
    { "weaplast", Cmd_WeapLast_f },
    { "lastweap", Cmd_WeapLast_f },
    { "kill", Cmd_Kill_f },
    { "kill_ai", Cmd_Kill_AI_f, CHEAT },
    { "where", Cmd_Where_f },
    { "clear_ai_enemy", Cmd_Clear_AI_Enemy_f, CHEAT },
    { "alertall", Cmd_AlertAll_f, CHEAT },
    { "putaway", Cmd_PutAway_f },
    { "wave", Cmd_Wave_f },
// ZOID
    { "team", CTFTeam_f, TEAMPL },
    { "switchteam", CTFSwitchTeam_f, TEAMPL },
    { "say_team", CTFSayTeam_f, INTERMISS | TEAMPL },
    { "id", CTFID_f, TEAMPL },
    { "yes", CTFVoteYes_f, TEAMPL },
    { "no", CTFVoteNo_f, TEAMPL },
    { "ready", CTFReady_f, TEAMPL },
    { "notready", CTFNotReady_f, TEAMPL },
    { "ghost", CTFGhost_f, TEAMPL },
    { "admin", CTFAdmin_f, TEAMPL },
    { "stats", CTFStats_f, TEAMPL },
    { "warp", CTFWarp_f, TEAMPL },
    { "boot", CTFBoot_f, TEAMPL },
    { "playerlist", CTFPlayerList_f, TEAMPL },
    { "observer", CTFObserver_f, TEAMPL },
// ZOID
    { "playerlist", Cmd_PlayerList_f },
    { "say_team", Cmd_Say_f, INTERMISS },
};

static const clientcmd_t *FindClientCommand(const char *name)
{
    for (int i = 0; i < q_countof(clientcmds); i++) {
        const clientcmd_t *cmd = &clientcmds[i];
        if (level.intermissiontime && !(cmd->flags & INTERMISS))
            continue;
        if (cmd->flags & TEAMPL && !G_TeamplayEnabled())
            continue;
        if (Q_strcasecmp(name, cmd->name) == 0)
            return cmd;
    }
    return NULL;
}

/*
=================
ClientCommand
=================
*/
q_exported void G_ClientCommand(int clientnum)
{
    edict_t *ent = &g_edicts[clientnum];
    if (!ent->client)
        return; // not fully in game yet

    char buf[MAX_QPATH];
    trap_Argv(0, buf, sizeof(buf));

    const clientcmd_t *cmd = FindClientCommand(buf);

    // anything that doesn't match a command will be a chat
    if (!cmd) {
        if (!level.intermissiontime)
            Cmd_Say_f(ent, ZERO_ARG);
        return;
    }

    if (cmd->flags & SP_ONLY && game.maxclients > 1) {
        G_ClientPrintf(ent, PRINT_HIGH, "Only possible in single player\n");
        return;
    }

    if (cmd->flags & CHEAT && game.maxclients > 1 && !sv_cheats.integer) {
        G_ClientPrintf(ent, PRINT_HIGH, "You must run the server with '+set cheats 1' to enable this command.\n");
        return;
    }

    cmd->func(ent, cmd->flags);
}

/*
=================
G_CompleteClientCommand

Completes client command name or argument for local system.
=================
*/
void G_CompleteClientCommand(int firstarg, int argnum)
{
    if (argnum == 0) {
        for (int i = 0; i < q_countof(clientcmds); i++) {
            const clientcmd_t *cmd = &clientcmds[i];
            if (cmd->flags & TEAMPL && !G_TeamplayEnabled())
                continue;
            if (cmd->flags & SP_ONLY && game.maxclients > 1)
                continue;
            trap_AddCommandCompletion(cmd->name);
        }
        return;
    }

    char buf[MAX_QPATH];
    trap_Argv(firstarg, buf, sizeof(buf));

    const clientcmd_t *cmd = FindClientCommand(buf);
    if (cmd && cmd->comp)
        cmd->comp(firstarg, argnum);
}
