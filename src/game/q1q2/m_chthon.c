#include "g_local.h"
#include "m_chthon.h"

static int sound_out1;
static int sound_sight;
static int sound_throw;
static int sound_pain;
static int sound_death;

void rocket_touch(edict_t *ent, edict_t *other, const trace_t *tr, bool other_touching_self);
void chthon_attack(edict_t *self);

static void fire_lavaball(edict_t *self, vec3_t start, vec3_t dir, int damage, int speed, float damage_radius, int radius_damage)
{
    edict_t *lavaball;

    lavaball = G_SpawnMissile(self, start, dir, speed);
    lavaball->avelocity = Vec3_Fill(300);
    lavaball->flags |= FL_DODGE;
    lavaball->s.effects |= EF_ROCKET;
    lavaball->s.modelindex = G_ModelIndex("models/objects/lavaball/tris.md2");
    lavaball->s.sound = G_SoundIndex("weapons/rockfly.wav");
    lavaball->touch = rocket_touch;
    lavaball->nextthink = level.time + SEC(8000.0f / speed);
    lavaball->think = G_FreeEdict;
    lavaball->dmg = damage;
    lavaball->radius_dmg = radius_damage;
    lavaball->dmg_radius = damage_radius;
    lavaball->classname = "lavaball";
    trap_LinkEntity(lavaball);

    G_CheckMissileImpact(self, lavaball);
}

static void chthon_fire_missile_offset(edict_t *self, vec3_t offset)
{
    if (!self->enemy || !self->enemy->r.inuse)
        return;

    vec3_t forward, right;
    AngleVectors(self->s.angles, &forward, &right, NULL);

    vec3_t start = M_ProjectFlashSource(self, offset, forward, right);

    vec3_t target = self->enemy->s.origin;
    target.z += self->enemy->viewheight;

    vec3_t dir = Vec3_Direction(target, start);

    fire_lavaball(self, start, dir, 60, 600, 120, 60);
    G_StartSound(self, CHAN_WEAPON, sound_throw, 1, ATTN_NONE);
}

static void chthon_attack_fire1(edict_t *self)
{
    chthon_fire_missile_offset(self, Vec3(100.0f, 100.0f, 200.0f));
}

static void chthon_attack_fire2(edict_t *self)
{
    chthon_fire_missile_offset(self, Vec3(100.0f, -100.0f, 200.0f));
}

static const mframe_t chthon_frames_attack[] = {
    { ai_charge }, // FRAME_attack1
    { ai_charge }, // FRAME_attack2
    { ai_charge }, // FRAME_attack3
    { ai_charge }, // FRAME_attack4
    { ai_charge }, // FRAME_attack5
    { ai_charge }, // FRAME_attack6
    { ai_charge }, // FRAME_attack7
    { ai_charge }, // FRAME_attack8
    { ai_charge, 0, chthon_attack_fire1 }, // FRAME_attack9 – first missile
    { ai_charge }, // FRAME_attack10
    { ai_charge }, // FRAME_attack11
    { ai_charge }, // FRAME_attack12
    { ai_charge }, // FRAME_attack13
    { ai_charge }, // FRAME_attack14
    { ai_charge }, // FRAME_attack15
    { ai_charge }, // FRAME_attack16
    { ai_charge }, // FRAME_attack17
    { ai_charge }, // FRAME_attack18
    { ai_charge }, // FRAME_attack19
    { ai_charge, 0, chthon_attack_fire2 }, // FRAME_attack20 – second missile
    { ai_charge }, // FRAME_attack21
    { ai_charge }, // FRAME_attack22
    { ai_charge }  // FRAME_attack23
};
const mmove_t MMOVE_T(chthon_move_attack) = { FRAME_attack1, FRAME_attack23, chthon_frames_attack, chthon_attack };

void MONSTERINFO_ATTACK(chthon_attack)(edict_t *self)
{
    M_SetAnimation(self, &chthon_move_attack);
}

// Shock A
static const mframe_t chthon_frames_painA[] = {
    { ai_move }, // FRAME_shocka1
    { ai_move }, // FRAME_shocka2
    { ai_move }, // FRAME_shocka3
    { ai_move }, // FRAME_shocka4
    { ai_move }, // FRAME_shocka5
    { ai_move }, // FRAME_shocka6
    { ai_move }, // FRAME_shocka7
    { ai_move }, // FRAME_shocka8
    { ai_move }, // FRAME_shocka9
    { ai_move }  // FRAME_shocka10
};
const mmove_t MMOVE_T(chthon_move_painA) = { FRAME_shocka1, FRAME_shocka10, chthon_frames_painA, chthon_attack };

// Shock B
static const mframe_t chthon_frames_painB[] = {
    { ai_move }, // FRAME_shockb1
    { ai_move }, // FRAME_shockb2
    { ai_move }, // FRAME_shockb3
    { ai_move }, // FRAME_shockb4
    { ai_move }, // FRAME_shockb5
    { ai_move }, // FRAME_shockb6
    { ai_move }, // FRAME_shockb7
    { ai_move }, // FRAME_shockb8
    { ai_move }, // FRAME_shockb9
    { ai_move }  // FRAME_shockb10
};
const mmove_t MMOVE_T(chthon_move_painB) = { FRAME_shockb1, FRAME_shockb10, chthon_frames_painB, chthon_attack };

#if 0
// Shock C
static const mframe_t chthon_frames_painC[] = {
    { ai_move }, // FRAME_shockc1
    { ai_move }, // FRAME_shockc2
    { ai_move }, // FRAME_shockc3
    { ai_move }, // FRAME_shockc4
    { ai_move }, // FRAME_shockc5
    { ai_move }, // FRAME_shockc6
    { ai_move }, // FRAME_shockc7
    { ai_move }, // FRAME_shockc8
    { ai_move }, // FRAME_shockc9
    { ai_move }  // FRAME_shockc10
};
const mmove_t MMOVE_T(chthon_move_painC) = { FRAME_shockc1, FRAME_shockc10, chthon_frames_painC, chthon_attack };

static const mframe_t chthon_frames_idle[] = {
    { ai_stand }, // FRAME_walk1
    { ai_stand }, // FRAME_walk2
    { ai_stand }, // FRAME_walk3
    { ai_stand }, // FRAME_walk4
    { ai_stand }, // FRAME_walk5
    { ai_stand }, // FRAME_walk6
    { ai_stand }, // FRAME_walk7
    { ai_stand }, // FRAME_walk8
    { ai_stand }, // FRAME_walk9
    { ai_stand }, // FRAME_walk10
    { ai_stand }, // FRAME_walk11
    { ai_stand }, // FRAME_walk12
    { ai_stand }, // FRAME_walk13
    { ai_stand }, // FRAME_walk14
    { ai_stand }, // FRAME_walk15
    { ai_stand }, // FRAME_walk16
    { ai_stand }, // FRAME_walk17
    { ai_stand }, // FRAME_walk18
    { ai_stand }, // FRAME_walk19
    { ai_stand }, // FRAME_walk20
    { ai_stand }, // FRAME_walk21
    { ai_stand }, // FRAME_walk22
    { ai_stand }, // FRAME_walk23
    { ai_stand }, // FRAME_walk24
    { ai_stand }, // FRAME_walk25
    { ai_stand }, // FRAME_walk26
    { ai_stand }, // FRAME_walk27
    { ai_stand }, // FRAME_walk28
    { ai_stand }, // FRAME_walk29
    { ai_stand }, // FRAME_walk30
    { ai_stand }  // FRAME_walk31
};
const mmove_t MMOVE_T(chthon_move_idle) = { FRAME_walk1, FRAME_walk31, chthon_frames_idle, NULL };

void MONSTERINFO_IDLE(chthon_idle)(edict_t *self)
{
    M_SetAnimation(self, &chthon_move_idle);
}
#endif

static void chthon_rise3_think(edict_t *self)
{
    G_StartSound(self, CHAN_BODY, sound_out1, 0.5f, ATTN_NONE);
    self->s.renderfx &= ~RF_OLD_FRAME_LERP;
}

static const mframe_t chthon_frames_rise[] = {
    { ai_turn, 0, chthon_rise3_think }, // FRAME_rise3
    { ai_turn }, // FRAME_rise4
    { ai_turn }, // FRAME_rise5
    { ai_turn }, // FRAME_rise6
    { ai_turn }, // FRAME_rise7
    { ai_turn }, // FRAME_rise8
    { ai_turn }, // FRAME_rise9
    { ai_turn }, // FRAME_rise10
    { ai_turn }, // FRAME_rise11
    { ai_turn }, // FRAME_rise12
    { ai_turn }, // FRAME_rise13
    { ai_turn }, // FRAME_rise14
    { ai_turn }, // FRAME_rise15
    { ai_turn }, // FRAME_rise16
    { ai_turn }  // FRAME_rise17
};
const mmove_t MMOVE_T(chthon_move_rise) = { FRAME_rise3, FRAME_rise17, chthon_frames_rise, chthon_attack };

static void chthon_dead(edict_t *self)
{
    self->r.svflags |= SVF_DEADMONSTER;
    self->nextthink = level.time + FRAME_TIME;
    self->think = G_FreeEdict;
}

static const mframe_t chthon_frames_death[] = {
    { ai_move }, // FRAME_death1
    { ai_move }, // FRAME_death2
    { ai_move }, // FRAME_death3
    { ai_move }, // FRAME_death4
    { ai_move }, // FRAME_death5
    { ai_move }, // FRAME_death6
    { ai_move }, // FRAME_death7
    { ai_move }, // FRAME_death8
    { ai_move }, // FRAME_death9
};
const mmove_t MMOVE_T(chthon_move_death) = { FRAME_death1, FRAME_death9, chthon_frames_death, chthon_dead };

void MONSTERINFO_STAND(chthon_stand)(edict_t *self)
{
    G_StartSound(self, CHAN_VOICE, sound_sight, 1, ATTN_NONE);
    M_SetAnimation(self, &chthon_move_rise);
    self->s.old_frame = FRAME_rise1;
    self->s.frame = FRAME_rise2;
    self->s.renderfx |= RF_OLD_FRAME_LERP;
}

void MONSTERINFO_WALK(chthon_walk)(edict_t *self)
{
    //M_SetAnimation(self, &chthon_move_rise);
}

void MONSTERINFO_RUN(chthon_run)(edict_t *self)
{
    //M_SetAnimation(self, &chthon_move_rise);
}

void DIE(chthon_die)(edict_t *self, edict_t *inflictor, edict_t *attacker, int damage, vec3_t point, mod_t mod)
{
    if (self->deadflag)
        return;

    G_StartSound(self, CHAN_VOICE, sound_death, 1, ATTN_NONE);
    self->deadflag = true;
    M_SetAnimation(self, &chthon_move_death);
}

void PAIN(chthon_pain)(edict_t *self, edict_t *other, float kick, int damage, mod_t mod)
{
    if (level.time < self->pain_debounce_time)
        return;

    self->pain_debounce_time = level.time + SEC(1.0f);

    G_StartSound(self, CHAN_VOICE, sound_pain, 1, ATTN_NONE);

    if (self->health > 16)
        M_SetAnimation(self, &chthon_move_painA);
    else
        M_SetAnimation(self, &chthon_move_painB);
}

void PR_monster_chthon(void)
{
    sound_out1 = G_SoundIndex("chthon/out1.wav");
    sound_sight = G_SoundIndex("chthon/sight1.wav");
    sound_throw = G_SoundIndex("chthon/throw.wav");
    sound_pain = G_SoundIndex("chthon/pain.wav");
    sound_death = G_SoundIndex("chthon/death.wav");
}

void SP_monster_chthon(edict_t *self)
{
    self->flags |= FL_STATIONARY;
    self->movetype = MOVETYPE_STEP;
    self->r.box = Box3_FromSize(128, -24, 256);
    self->r.solid = SOLID_BBOX;
    self->s.modelindex = G_ModelIndex("models/monsters/chthon/tris.md2");

    if (skill.integer == 0)
        self->health = 16;
    else
        self->health = 50;

    self->gib_health = -1;
    self->mass = 1000;

    self->pain = chthon_pain;
    self->die = chthon_die;
    self->monsterinfo.stand = chthon_stand;
    self->monsterinfo.walk = chthon_walk;
    self->monsterinfo.run = chthon_run;
    self->monsterinfo.attack = chthon_attack;

    self->monsterinfo.scale = MODEL_SCALE;

    walkmonster_start(self);

    self->takedamage = false;
}
