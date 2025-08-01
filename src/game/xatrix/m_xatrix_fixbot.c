// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.
/*
    fixbot.c
*/

#include "g_local.h"
#include "m_xatrix_fixbot.h"

static int sound_pain1;
static int sound_die;
static int sound_weld1;
static int sound_weld2;
static int sound_weld3;

void fixbot_run(edict_t *self);
void fixbot_attack(edict_t *self);
void fixbot_stand(edict_t *self);
static void fixbot_fire_blaster(edict_t *self);
static void fixbot_fire_welder(edict_t *self);

static void use_scanner(edict_t *self);
static void change_to_roam(edict_t *self);
static void fly_vertical(edict_t *self);

static void roam_goal(edict_t *self);

const mmove_t fixbot_move_forward;
const mmove_t fixbot_move_stand;
const mmove_t fixbot_move_stand2;
const mmove_t fixbot_move_roamgoal;

const mmove_t fixbot_move_weld_start;
const mmove_t fixbot_move_weld;
const mmove_t fixbot_move_weld_end;
const mmove_t fixbot_move_takeoff;
const mmove_t fixbot_move_landing;
const mmove_t fixbot_move_turn;

// [Paril-KEX] clean up bot goals if we get interrupted
void THINK(bot_goal_check)(edict_t *self)
{
    edict_t *owner = NULL;

    if (self->r.ownernum != ENTITYNUM_NONE)
        owner = &g_edicts[self->r.ownernum];

    if (!owner || !owner->r.inuse || owner->goalentity != self) {
        G_FreeEdict(self);
        return;
    }

    self->nextthink = level.time + FRAME_TIME;
}

edict_t *healFindMonster(edict_t *self, float radius);

typedef enum {
    FB_NONE, FB_HEAL, FB_WELD, FB_ROAM
} fixbot_mode_t;

static void fixbot_set_fly_parameters(edict_t *self, fixbot_mode_t mode)
{
    self->monsterinfo.fly_position_time = 0;
    self->monsterinfo.fly_acceleration = 5;
    self->monsterinfo.fly_speed = 110;
    self->monsterinfo.fly_buzzard = false;
    self->monsterinfo.fly_thrusters = mode == FB_HEAL;

    switch (mode) {
    case FB_HEAL:
        self->monsterinfo.fly_min_distance = 100;
        self->monsterinfo.fly_max_distance = 100;
        break;
    case FB_WELD:
    case FB_ROAM:
        self->monsterinfo.fly_min_distance = 16;
        self->monsterinfo.fly_max_distance = 16;
        break;
    default:
        // timid bot
        self->monsterinfo.fly_min_distance = 300;
        self->monsterinfo.fly_max_distance = 500;
        break;
    }
}

static bool fixbot_search(edict_t *self)
{
    edict_t *ent;

    if (!self->enemy) {
        ent = healFindMonster(self, 1024);
        if (ent) {
            self->oldenemy = self->enemy;
            self->enemy = ent;
            self->enemy->monsterinfo.healer = self;
            self->monsterinfo.aiflags |= AI_MEDIC;
            FoundTarget(self);
            fixbot_set_fly_parameters(self, FB_HEAL);
            return true;
        }
    }
    return false;
}

static void landing_goal(edict_t *self)
{
    trace_t  tr;
    vec3_t   forward, right, up;
    vec3_t   end;
    edict_t *ent;

    ent = G_Spawn();
    ent->classname = "bot_goal";
    ent->r.solid = SOLID_BBOX;
    ent->r.ownernum = self->s.number;
    ent->think = bot_goal_check;
    trap_LinkEntity(ent);

    VectorSet(ent->r.mins, -32, -32, -24);
    VectorSet(ent->r.maxs, 32, 32, 24);

    AngleVectors(self->s.angles, forward, right, up);
    VectorMA(self->s.origin, 32, forward, end); // FIXME
    VectorMA(self->s.origin, -8096, up, end);

    trap_Trace(&tr, self->s.origin, ent->r.mins, ent->r.maxs, end, self->s.number, MASK_MONSTERSOLID);

    VectorCopy(tr.endpos, ent->s.origin);

    self->goalentity = self->enemy = ent;
    M_SetAnimation(self, &fixbot_move_landing);
}

static void takeoff_goal(edict_t *self)
{
    trace_t  tr;
    vec3_t   forward, right, up;
    vec3_t   end;
    edict_t *ent;

    ent = G_Spawn();
    ent->classname = "bot_goal";
    ent->r.solid = SOLID_BBOX;
    ent->r.ownernum = self->s.number;
    ent->think = bot_goal_check;
    trap_LinkEntity(ent);

    VectorSet(ent->r.mins, -32, -32, -24);
    VectorSet(ent->r.maxs, 32, 32, 24);

    AngleVectors(self->s.angles, forward, right, up);
    VectorMA(self->s.origin, 32, forward, end); // FIXME
    VectorMA(self->s.origin, 128, up, end);

    trap_Trace(&tr, self->s.origin, ent->r.mins, ent->r.maxs, end, self->s.number, MASK_MONSTERSOLID);

    VectorCopy(tr.endpos, ent->s.origin);

    self->goalentity = self->enemy = ent;
    M_SetAnimation(self, &fixbot_move_takeoff);
}

#define SPAWNFLAG_FIXBOT_FLAGS (SPAWNFLAG_FIXBOT_FIXIT | SPAWNFLAG_FIXBOT_TAKEOFF | SPAWNFLAG_FIXBOT_LANDING | SPAWNFLAG_FIXBOT_WORKING)

static void change_to_roam(edict_t *self)
{
    if (fixbot_search(self))
        return;

    fixbot_set_fly_parameters(self, FB_ROAM);
    M_SetAnimation(self, &fixbot_move_roamgoal);

    if (self->spawnflags & SPAWNFLAG_FIXBOT_LANDING) {
        landing_goal(self);
        M_SetAnimation(self, &fixbot_move_landing);
        self->spawnflags &= ~SPAWNFLAG_FIXBOT_FLAGS;
        self->spawnflags |= SPAWNFLAG_FIXBOT_WORKING;
    }
    if (self->spawnflags & SPAWNFLAG_FIXBOT_TAKEOFF) {
        takeoff_goal(self);
        M_SetAnimation(self, &fixbot_move_takeoff);
        self->spawnflags &= ~SPAWNFLAG_FIXBOT_FLAGS;
        self->spawnflags |= SPAWNFLAG_FIXBOT_WORKING;
    }
    if (self->spawnflags & SPAWNFLAG_FIXBOT_FIXIT) {
        M_SetAnimation(self, &fixbot_move_roamgoal);
        self->spawnflags &= ~SPAWNFLAG_FIXBOT_FLAGS;
        self->spawnflags |= SPAWNFLAG_FIXBOT_WORKING;
    }
    if (!self->spawnflags)
        M_SetAnimation(self, &fixbot_move_stand2);
}

static void roam_goal(edict_t *self)
{
    trace_t  tr;
    vec3_t   forward, right, up;
    vec3_t   end;
    edict_t *ent;
    vec3_t   dang;
    float    len, oldlen;
    int      i;
    vec3_t   whichvec = { 0 };

    ent = G_Spawn();
    ent->classname = "bot_goal";
    ent->r.solid = SOLID_BBOX;
    ent->r.ownernum = self->s.number;
    ent->think = bot_goal_check;
    ent->nextthink = level.time + FRAME_TIME;
    trap_LinkEntity(ent);

    oldlen = 0;

    for (i = 0; i < 12; i++) {
        VectorCopy(self->s.angles, dang);

        if (i < 6)
            dang[YAW] += 30 * i;
        else
            dang[YAW] -= 30 * (i - 6);

        AngleVectors(dang, forward, right, up);
        VectorMA(self->s.origin, 8192, forward, end);

        trap_Trace(&tr, self->s.origin, NULL, NULL, end, self->s.number, MASK_PROJECTILE);

        len = Distance(self->s.origin, tr.endpos);
        if (len > oldlen) {
            oldlen = len;
            VectorCopy(tr.endpos, whichvec);
        }
    }

    VectorCopy(whichvec, ent->s.origin);
    self->goalentity = self->enemy = ent;

    M_SetAnimation(self, &fixbot_move_turn);
}

static void use_scanner(edict_t *self)
{
    edict_t *ent = NULL;
    float   radius = 1024;

    while ((ent = findradius(ent, self->s.origin, radius)) != NULL) {
        if (ent->health < 100)
            continue;
        if (strcmp(ent->classname, "object_repair") != 0)
            continue;
        if (!visible(self, ent))
            continue;

        // remove the old one
        if (strcmp(self->goalentity->classname, "bot_goal") == 0) {
            self->goalentity->nextthink = level.time + FRAME_TIME;
            self->goalentity->think = G_FreeEdict;
        }

        self->goalentity = self->enemy = ent;

        fixbot_set_fly_parameters(self, FB_WELD);

        if (Distance(self->s.origin, self->goalentity->s.origin) < 86)
            M_SetAnimation(self, &fixbot_move_weld_start);
        return;
    }

    if (!self->goalentity) {
        M_SetAnimation(self, &fixbot_move_stand);
        return;
    }

    if (Distance(self->s.origin, self->goalentity->s.origin) < 86) {
        if (strcmp(self->goalentity->classname, "object_repair") == 0) {
            M_SetAnimation(self, &fixbot_move_weld_start);
        } else {
            self->goalentity->nextthink = level.time + FRAME_TIME;
            self->goalentity->think = G_FreeEdict;
            self->goalentity = self->enemy = NULL;
            M_SetAnimation(self, &fixbot_move_stand);
        }
        return;
    }
}

/*
    when the bot has found a landing pad
    it will proceed to its goalentity
    just above the landing pad and
    decend translated along the z the current
    frames are at 10fps
*/
static void blastoff(edict_t *self, const vec3_t start, const vec3_t aimdir, int damage, int kick, entity_event_t te_impact, int hspread, int vspread)
{
    trace_t    tr;
    vec3_t     dir;
    vec3_t     forward, right, up;
    vec3_t     end, pos;
    float      r;
    float      u;
    vec3_t     water_start;
    bool       water = false;
    contents_t content_mask = MASK_PROJECTILE | MASK_WATER;

    hspread += (self->s.frame - FRAME_takeoff_01);
    vspread += (self->s.frame - FRAME_takeoff_01);

    trap_Trace(&tr, self->s.origin, NULL, NULL, start, self->s.number, MASK_PROJECTILE);
    if (!(tr.fraction < 1.0f)) {
        vectoangles(aimdir, dir);
        AngleVectors(dir, forward, right, up);

        r = crandom() * hspread;
        u = crandom() * vspread;
        VectorMA(start, 8192, forward, end);
        VectorMA(end, r, right, end);
        VectorMA(end, u, up, end);

        if (trap_PointContents(start) & MASK_WATER) {
            water = true;
            VectorCopy(start, water_start);
            content_mask &= ~MASK_WATER;
        }

        trap_Trace(&tr, start, NULL, NULL, end, self->s.number, content_mask);

        // see if we hit water
        if (tr.contents & MASK_WATER) {
            entity_event_t color;

            water = true;
            VectorCopy(tr.endpos, water_start);

            if (!VectorCompare(start, tr.endpos)) {
                if (tr.contents & CONTENTS_WATER)
                    color = EV_SPLASH_BLUE_WATER;
                else if (tr.contents & CONTENTS_SLIME)
                    color = EV_SPLASH_SLIME;
                else if (tr.contents & CONTENTS_LAVA)
                    color = EV_SPLASH_LAVA;
                else
                    color = EV_SPLASH_UNKNOWN;

                if (color != EV_SPLASH_UNKNOWN) {
                    G_SnapVectorTowards(tr.endpos, start, pos);
                    G_TempEntity(pos, color, MakeLittleShort(tr.plane.dir, 8));
                }

                // change bullet's course when it enters water
                VectorSubtract(end, start, dir);
                vectoangles(dir, dir);
                AngleVectors(dir, forward, right, up);
                r = crandom() * hspread * 2;
                u = crandom() * vspread * 2;
                VectorMA(water_start, 8192, forward, end);
                VectorMA(end, r, right, end);
                VectorMA(end, u, up, end);
            }

            // re-trace ignoring water this time
            trap_Trace(&tr, water_start, NULL, NULL, end, self->s.number, MASK_PROJECTILE);
        }
    }

    edict_t *hit = &g_edicts[tr.entnum];

    // send gun puff / flash
    if (!(tr.surface_flags & SURF_SKY)) {
        if (tr.fraction < 1.0f) {
            if (hit->takedamage) {
                T_Damage(hit, self, self, aimdir, tr.endpos, tr.plane.dir, damage, kick, DAMAGE_BULLET, (mod_t) { MOD_BLASTOFF });
            } else {
                G_SnapVectorTowards(tr.endpos, start, pos);
                G_TempEntity(pos, te_impact, tr.plane.dir);

                if (self->client)
                    PlayerNoise(self, tr.endpos, PNOISE_IMPACT);
            }
        }
    }

    // if went through water, determine where the end and make a bubble trail
    if (water) {
        VectorSubtract(tr.endpos, water_start, dir);
        VectorNormalize(dir);
        VectorMA(tr.endpos, -2, dir, pos);
        if (trap_PointContents(pos) & MASK_WATER)
            VectorCopy(pos, tr.endpos);
        else
            trap_Trace(&tr, pos, NULL, NULL, water_start, hit->s.number, MASK_WATER);

        G_SpawnTrail(water_start, tr.endpos, EV_BUBBLETRAIL);
    }
}

static void fly_vertical(edict_t *self)
{
    int    i;
    vec3_t v;
    vec3_t forward, right, up;
    vec3_t start;
    vec3_t tempvec;

    VectorSubtract(self->goalentity->s.origin, self->s.origin, v);
    self->ideal_yaw = vectoyaw(v);
    M_ChangeYaw(self);

    if (self->s.frame == FRAME_landing_58 || self->s.frame == FRAME_takeoff_16) {
        self->goalentity->nextthink = level.time + FRAME_TIME;
        self->goalentity->think = G_FreeEdict;
        M_SetAnimation(self, &fixbot_move_stand);
        self->goalentity = self->enemy = NULL;
    }

    // kick up some particles
    VectorCopy(self->s.angles, tempvec);
    tempvec[PITCH] += 90;

    AngleVectors(tempvec, forward, right, up);
    VectorCopy(self->s.origin, start);

    for (i = 0; i < 10; i++)
        blastoff(self, start, forward, 2, 1, EV_SHOTGUN, DEFAULT_SHOTGUN_HSPREAD, DEFAULT_SHOTGUN_VSPREAD);

    // needs sound
}

static void fly_vertical2(edict_t *self)
{
    vec3_t v;
    float  len;

    VectorSubtract(self->goalentity->s.origin, self->s.origin, v);
    len = VectorLength(v);
    self->ideal_yaw = vectoyaw(v);
    M_ChangeYaw(self);

    if (len < 32) {
        self->goalentity->nextthink = level.time + FRAME_TIME;
        self->goalentity->think = G_FreeEdict;
        M_SetAnimation(self, &fixbot_move_stand);
        self->goalentity = self->enemy = NULL;
    }

    // needs sound
}

static const mframe_t fixbot_frames_landing[] = {
    { ai_move },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },

    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },

    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },

    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },

    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },

    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 },
    { ai_move, 0, fly_vertical2 }
};
const mmove_t MMOVE_T(fixbot_move_landing) = { FRAME_landing_01, FRAME_landing_58, fixbot_frames_landing, NULL };

/*
    generic ambient stand
*/
static const mframe_t fixbot_frames_stand[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },

    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move, 0, change_to_roam }

};
const mmove_t MMOVE_T(fixbot_move_stand) = { FRAME_ambient_01, FRAME_ambient_19, fixbot_frames_stand, NULL };

static const mframe_t fixbot_frames_stand2[] = {
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },

    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand },
    { ai_stand, 0, change_to_roam }
};
const mmove_t MMOVE_T(fixbot_move_stand2) = { FRAME_ambient_01, FRAME_ambient_19, fixbot_frames_stand2, NULL };

#if 0
/*
    will need the pickup offset for the front pincers
    object will need to stop forward of the object
    and take the object with it ( this may require a variant of liftoff and landing )
*/
static const mframe_t fixbot_frames_pickup[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },

    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },

    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }

};
const mmove_t MMOVE_T(fixbot_move_pickup) = { FRAME_pickup_01, FRAME_pickup_27, fixbot_frames_pickup, NULL };
#endif

/*
    generic frame to move bot
*/
static const mframe_t fixbot_frames_roamgoal[] = {
    { ai_move, 0, roam_goal }
};
const mmove_t MMOVE_T(fixbot_move_roamgoal) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_roamgoal, NULL };

static void ai_facing(edict_t *self, float dist)
{
    if (!self->goalentity) {
        fixbot_stand(self);
        return;
    }

    if (infront(self, self->goalentity))
        M_SetAnimation(self, &fixbot_move_forward);
    else {
        vec3_t v;
        VectorSubtract(self->goalentity->s.origin, self->s.origin, v);
        self->ideal_yaw = vectoyaw(v);
        M_ChangeYaw(self);
    }
};

static const mframe_t fixbot_frames_turn[] = {
    { ai_facing }
};
const mmove_t MMOVE_T(fixbot_move_turn) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_turn, NULL };

/*
    takeoff
*/
static const mframe_t fixbot_frames_takeoff[] = {
    { ai_move, 0.01f, fly_vertical },
    { ai_move, 0.01f, fly_vertical },
    { ai_move, 0.01f, fly_vertical },
    { ai_move, 0.01f, fly_vertical },
    { ai_move, 0.01f, fly_vertical },
    { ai_move, 0.01f, fly_vertical },
    { ai_move, 0.01f, fly_vertical },
    { ai_move, 0.01f, fly_vertical },
    { ai_move, 0.01f, fly_vertical },
    { ai_move, 0.01f, fly_vertical },

    { ai_move, 0.01f, fly_vertical },
    { ai_move, 0.01f, fly_vertical },
    { ai_move, 0.01f, fly_vertical },
    { ai_move, 0.01f, fly_vertical },
    { ai_move, 0.01f, fly_vertical },
    { ai_move, 0.01f, fly_vertical }
};
const mmove_t MMOVE_T(fixbot_move_takeoff) = { FRAME_takeoff_01, FRAME_takeoff_16, fixbot_frames_takeoff, NULL };

/* findout what this is */
static const mframe_t fixbot_frames_paina[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(fixbot_move_paina) = { FRAME_paina_01, FRAME_paina_06, fixbot_frames_paina, fixbot_run };

/* findout what this is */
static const mframe_t fixbot_frames_painb[] = {
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move },
    { ai_move }
};
const mmove_t MMOVE_T(fixbot_move_painb) = { FRAME_painb_01, FRAME_painb_08, fixbot_frames_painb, fixbot_run };

/*
    backup from pain
    call a generic painsound
    some spark effects
*/
static const mframe_t fixbot_frames_pain3[] = {
    { ai_move, -1 }
};
const mmove_t MMOVE_T(fixbot_move_pain3) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_pain3, fixbot_run };

#if 0
/*
    bot has compleated landing
    and is now on the grownd
    ( may need second land if the bot is releasing jib into jib vat )
*/
static const mframe_t fixbot_frames_land[] = {
    { ai_move }
};
const mmove_t MMOVE_T(fixbot_move_land) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_land, NULL };
#endif

void M_MoveToGoal(edict_t *ent, float dist);

static void ai_movetogoal(edict_t *self, float dist)
{
    M_MoveToGoal(self, dist);
}
/*

*/
static const mframe_t fixbot_frames_forward[] = {
    { ai_movetogoal, 5, use_scanner }
};
const mmove_t MMOVE_T(fixbot_move_forward) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_forward, NULL };

/*

*/
static const mframe_t fixbot_frames_walk[] = {
    { ai_walk, 5 }
};
const mmove_t MMOVE_T(fixbot_move_walk) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_walk, NULL };

/*

*/
static const mframe_t fixbot_frames_run[] = {
    { ai_run, 10 }
};
const mmove_t MMOVE_T(fixbot_move_run) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_run, NULL };

#if 0
/*
    raf
    note to self
    they could have a timer that will cause
    the bot to explode on countdown
*/
static const mframe_t fixbot_frames_death1[] = {
    { ai_move }
};
const mmove_t MMOVE_T(fixbot_move_death1) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_death1, fixbot_dead };

//
static const mframe_t fixbot_frames_backward[] = {
    { ai_move }
};
const mmove_t MMOVE_T(fixbot_move_backward) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_backward, NULL };
#endif

//
static const mframe_t fixbot_frames_start_attack[] = {
    { ai_charge }
};
const mmove_t MMOVE_T(fixbot_move_start_attack) = { FRAME_freeze_01, FRAME_freeze_01, fixbot_frames_start_attack, fixbot_attack };

#if 0
/*
    TBD:
    need to get laser attack anim
    attack with the laser blast
*/
static const mframe_t fixbot_frames_attack1[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge, -10, fixbot_fire_blaster }
};
const mmove_t MMOVE_T(fixbot_move_attack1) = { FRAME_shoot_01, FRAME_shoot_06, fixbot_frames_attack1, NULL };
#endif

void abortHeal(edict_t *self, bool gib, bool mark);
bool finishHeal(edict_t *self);

void PRETHINK(fixbot_laser_update)(edict_t *laser)
{
    edict_t *self = &g_edicts[laser->r.ownernum];

    vec3_t start, dir;
    AngleVectors(self->s.angles, dir, NULL, NULL);
    VectorMA(self->s.origin, 16, dir, start);

    if (self->enemy && self->health > 0) {
        vec3_t point;
        VectorAvg(self->enemy->r.absmin, self->enemy->r.absmax, point);
        if (self->monsterinfo.aiflags & AI_MEDIC)
            point[0] += sinf(TO_SEC(level.time)) * 8;
        VectorSubtract(point, self->s.origin, dir);
        VectorNormalize(dir);
    }

    G_SnapVector(start, laser->s.origin);
    VectorCopy(dir, laser->movedir);
    trap_LinkEntity(laser);
    dabeam_update(laser, true);
}

static void fixbot_fire_laser(edict_t *self)
{
    // critter dun got blown up while bein' fixed
    if (!self->enemy || !self->enemy->r.inuse || self->enemy->health <= self->enemy->gib_health) {
        M_SetAnimation(self, &fixbot_move_stand);
        self->monsterinfo.aiflags &= ~AI_MEDIC;
        return;
    }

    // fire the beam until they'return within res range
    bool firedLaser = false;

    if (self->enemy->health < (self->enemy->mass / 10)) {
        firedLaser = true;
        monster_fire_dabeam(self, -1, false, fixbot_laser_update);
    }

    if (self->enemy->health >= (self->enemy->mass / 10)) {
        // we have enough health now; if we didn't fire
        // a laser, just make a fake one
        if (!firedLaser)
            monster_fire_dabeam(self, 0, false, fixbot_laser_update);
        else
            self->monsterinfo.fly_position_time = 0;

        // change our fly parameter slightly so we back away
        self->monsterinfo.fly_min_distance = self->monsterinfo.fly_max_distance = 200;

        // don't revive if we are too close
        if (Distance(self->s.origin, self->enemy->s.origin) > 86) {
            finishHeal(self);
            M_SetAnimation(self, &fixbot_move_stand);
        }
    } else
        self->enemy->monsterinfo.aiflags |= AI_RESURRECTING;
}

static const mframe_t fixbot_frames_laserattack[] = {
    { ai_charge, 0, fixbot_fire_laser },
    { ai_charge, 0, fixbot_fire_laser },
    { ai_charge, 0, fixbot_fire_laser },
    { ai_charge, 0, fixbot_fire_laser },
    { ai_charge, 0, fixbot_fire_laser },
    { ai_charge, 0, fixbot_fire_laser }
};
const mmove_t MMOVE_T(fixbot_move_laserattack) = { FRAME_shoot_01, FRAME_shoot_06, fixbot_frames_laserattack, NULL };

/*
    need to get forward translation data
    for the charge attack
*/
static const mframe_t fixbot_frames_attack2[] = {
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },

    { ai_charge, -10 },
    { ai_charge, -10 },
    { ai_charge, -10 },
    { ai_charge, -10 },
    { ai_charge, -10 },
    { ai_charge, -10 },
    { ai_charge, -10 },
    { ai_charge, -10 },
    { ai_charge, -10 },
    { ai_charge, -10 },

    { ai_charge, 0, fixbot_fire_blaster },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },
    { ai_charge },

    { ai_charge }
};
const mmove_t MMOVE_T(fixbot_move_attack2) = { FRAME_charging_01, FRAME_charging_31, fixbot_frames_attack2, fixbot_run };

static void weldstate(edict_t *self)
{
    if (self->s.frame == FRAME_weldstart_10)
        M_SetAnimation(self, &fixbot_move_weld);
    else if (self->goalentity && self->s.frame == FRAME_weldmiddle_07) {
        if (self->goalentity->health <= 0) {
            self->enemy->r.ownernum = ENTITYNUM_NONE;
            M_SetAnimation(self, &fixbot_move_weld_end);
        } else if (!(self->spawnflags & SPAWNFLAG_MONSTER_SCENIC))
            self->goalentity->health -= 10;
    } else {
        self->goalentity = self->enemy = NULL;
        M_SetAnimation(self, &fixbot_move_stand);
    }
}

static void ai_move2(edict_t *self, float dist)
{
    if (!self->goalentity) {
        fixbot_stand(self);
        return;
    }

    M_walkmove(self, self->s.angles[YAW], dist);

    vec3_t v;
    VectorSubtract(self->goalentity->s.origin, self->s.origin, v);
    self->ideal_yaw = vectoyaw(v);
    M_ChangeYaw(self);
};

static const mframe_t fixbot_frames_weld_start[] = {
    { ai_move2, 0 },
    { ai_move2, 0 },
    { ai_move2, 0 },
    { ai_move2, 0 },
    { ai_move2, 0 },
    { ai_move2, 0 },
    { ai_move2, 0 },
    { ai_move2, 0 },
    { ai_move2, 0 },
    { ai_move2, 0, weldstate }
};
const mmove_t MMOVE_T(fixbot_move_weld_start) = { FRAME_weldstart_01, FRAME_weldstart_10, fixbot_frames_weld_start, NULL };

static const mframe_t fixbot_frames_weld[] = {
    { ai_move2, 0, fixbot_fire_welder },
    { ai_move2, 0, fixbot_fire_welder },
    { ai_move2, 0, fixbot_fire_welder },
    { ai_move2, 0, fixbot_fire_welder },
    { ai_move2, 0, fixbot_fire_welder },
    { ai_move2, 0, fixbot_fire_welder },
    { ai_move2, 0, weldstate }
};
const mmove_t MMOVE_T(fixbot_move_weld) = { FRAME_weldmiddle_01, FRAME_weldmiddle_07, fixbot_frames_weld, NULL };

static const mframe_t fixbot_frames_weld_end[] = {
    { ai_move2, -2 },
    { ai_move2, -2 },
    { ai_move2, -2 },
    { ai_move2, -2 },
    { ai_move2, -2 },
    { ai_move2, -2 },
    { ai_move2, -2, weldstate }
};
const mmove_t MMOVE_T(fixbot_move_weld_end) = { FRAME_weldend_01, FRAME_weldend_07, fixbot_frames_weld_end, NULL };

static void fixbot_fire_welder(edict_t *self)
{
    vec3_t start;
    vec3_t forward, right, up;
    static const vec3_t vec = { 24.0f, -0.8f, -10.0f };
    float  r;

    if (!self->enemy)
        return;

    if (self->spawnflags & SPAWNFLAG_MONSTER_SCENIC) {
        if (self->timestamp >= level.time)
            return;
        self->timestamp = level.time + random_time_sec(0.45f, 1.5f);
    }

    AngleVectors(self->s.angles, forward, right, up);
    M_ProjectFlashSource(self, vec, forward, right, start);

    G_AddEvent(self, EV_WELDING_SPARKS, MakeLittleLong(0, irandom2(0xe0, 0xe8), 10, 0));

    if (frandom() > 0.8f) {
        r = frandom();

        if (r < 0.33f)
            G_StartSound(self, CHAN_VOICE, sound_weld1, 1, ATTN_IDLE);
        else if (r < 0.66f)
            G_StartSound(self, CHAN_VOICE, sound_weld2, 1, ATTN_IDLE);
        else
            G_StartSound(self, CHAN_VOICE, sound_weld3, 1, ATTN_IDLE);
    }
}

static void fixbot_fire_blaster(edict_t *self)
{
    vec3_t start;
    vec3_t forward, right, up;
    vec3_t end;
    vec3_t dir;

    if (!visible(self, self->enemy))
        M_SetAnimation(self, &fixbot_move_run);

    AngleVectors(self->s.angles, forward, right, up);
    M_ProjectFlashSource(self, monster_flash_offset[MZ2_HOVER_BLASTER_1], forward, right, start);

    VectorCopy(self->enemy->s.origin, end);
    end[2] += self->enemy->viewheight;
    VectorSubtract(end, start, dir);
    VectorNormalize(dir);

    monster_fire_blaster(self, start, dir, 15, 1000, MZ2_HOVER_BLASTER_1, EF_BLASTER);
}

void MONSTERINFO_STAND(fixbot_stand)(edict_t *self)
{
    M_SetAnimation(self, &fixbot_move_stand);
}

void MONSTERINFO_RUN(fixbot_run)(edict_t *self)
{
    if (self->monsterinfo.aiflags & AI_STAND_GROUND)
        M_SetAnimation(self, &fixbot_move_stand);
    else
        M_SetAnimation(self, &fixbot_move_run);
}

void MONSTERINFO_WALK(fixbot_walk)(edict_t *self)
{
    if (self->goalentity
        && strcmp(self->goalentity->classname, "object_repair") == 0
        && Distance(self->s.origin, self->goalentity->s.origin) < 32)
        M_SetAnimation(self, &fixbot_move_weld_start);
    else
        M_SetAnimation(self, &fixbot_move_walk);
}

void MONSTERINFO_ATTACK(fixbot_attack)(edict_t *self)
{
    if (self->monsterinfo.aiflags & AI_MEDIC) {
        if (!visible(self, self->enemy))
            return;
        if (Distance(self->s.origin, self->enemy->s.origin) > 128)
            return;
        M_SetAnimation(self, &fixbot_move_laserattack);
    } else {
        fixbot_set_fly_parameters(self, FB_NONE);
        M_SetAnimation(self, &fixbot_move_attack2);
    }
}

void PAIN(fixbot_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (level.time < self->pain_debounce_time)
        return;

    fixbot_set_fly_parameters(self, FB_NONE);
    self->pain_debounce_time = level.time + SEC(3);
    G_StartSound(self, CHAN_VOICE, sound_pain1, 1, ATTN_NORM);

    if (damage <= 10)
        M_SetAnimation(self, &fixbot_move_pain3);
    else if (damage <= 25)
        M_SetAnimation(self, &fixbot_move_painb);
    else
        M_SetAnimation(self, &fixbot_move_paina);

    abortHeal(self, false, false);
}

#if 0
static void fixbot_dead(edict_t *self)
{
    VectorSet(self->r.mins, -16, -16, -24);
    VectorSet(self->r.maxs, 16, 16, -8);
    self->movetype = MOVETYPE_TOSS;
    self->r.svflags |= SVF_DEADMONSTER;
    self->nextthink = 0;
    trap_LinkEntity(self);
}
#endif

void DIE(fixbot_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, const vec3_t point, mod_t mod)
{
    G_StartSound(self, CHAN_VOICE, sound_die, 1, ATTN_NORM);
    BecomeExplosion1(self);

    // shards
}

static void fixbot_precache(void)
{
    sound_pain1 = G_SoundIndex("flyer/flypain1.wav");
    sound_die = G_SoundIndex("flyer/flydeth1.wav");

    sound_weld1 = G_SoundIndex("misc/welder1.wav");
    sound_weld2 = G_SoundIndex("misc/welder2.wav");
    sound_weld3 = G_SoundIndex("misc/welder3.wav");
}

/*QUAKED monster_fixbot (1 .5 0) (-32 -32 -24) (32 32 24) Ambush Trigger_Spawn Fixit Takeoff Landing
 */
void SP_monster_fixbot(edict_t *self)
{
    if (!M_AllowSpawn(self)) {
        G_FreeEdict(self);
        return;
    }

    G_AddPrecache(fixbot_precache);

    self->s.modelindex = G_ModelIndex("models/monsters/fixbot/tris.md2");

    VectorSet(self->r.mins, -32, -32, -24);
    VectorSet(self->r.maxs, 32, 32, 24);

    self->movetype = MOVETYPE_STEP;
    self->r.solid = SOLID_BBOX;

    self->health = 150 * st.health_multiplier;
    self->mass = 150;

    self->pain = fixbot_pain;
    self->die = fixbot_die;

    self->monsterinfo.stand = fixbot_stand;
    self->monsterinfo.walk = fixbot_walk;
    self->monsterinfo.run = fixbot_run;
    self->monsterinfo.attack = fixbot_attack;

    trap_LinkEntity(self);

    M_SetAnimation(self, &fixbot_move_stand);
    self->monsterinfo.scale = MODEL_SCALE;
    self->monsterinfo.aiflags |= AI_ALTERNATE_FLY;
    fixbot_set_fly_parameters(self, FB_NONE);

    flymonster_start(self);
}
