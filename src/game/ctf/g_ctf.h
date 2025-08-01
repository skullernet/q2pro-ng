// Copyright (c) ZeniMax Media Inc.
// Licensed under the GNU General Public License 2.0.

#define CTF_VERSION 1.52
#define CTF_VSTRING2(x) #x
#define CTF_VSTRING(x) CTF_VSTRING2(x)
#define CTF_STRING_VERSION CTF_VSTRING(CTF_VERSION)

typedef enum {
    CTF_NOTEAM,
    CTF_TEAM1,
    CTF_TEAM2
} ctfteam_t;

typedef enum {
    CTF_GRAPPLE_STATE_FLY,
    CTF_GRAPPLE_STATE_PULL,
    CTF_GRAPPLE_STATE_HANG
} ctfgrapplestate_t;

typedef struct {
    char netname[MAX_NETNAME];
    int  number;

    // stats
    int deaths;
    int kills;
    int caps;
    int basedef;
    int carrierdef;

    int      code;  // ghost code
    ctfteam_t        team;  // team
    int      score; // frags at time of disconnect
    edict_t *ent;
} ghost_t;

extern vm_cvar_t ctf;
extern vm_cvar_t g_teamplay_force_join;
extern vm_cvar_t teamplay;

#define CTF_TEAM1_SKIN "ctf_r"
#define CTF_TEAM2_SKIN "ctf_b"

#define CTF_CAPTURE_BONUS 15     // what you get for capture
#define CTF_TEAM_BONUS 10        // what your team gets for capture
#define CTF_RECOVERY_BONUS 1     // what you get for recovery
#define CTF_FLAG_BONUS 0         // what you get for picking up enemy flag
#define CTF_FRAG_CARRIER_BONUS 2 // what you get for fragging enemy flag carrier
#define CTF_FLAG_RETURN_TIME SEC(40)  // seconds until auto return

#define CTF_CARRIER_DANGER_PROTECT_BONUS 2 // bonus for fraggin someone who has recently hurt your flag carrier
#define CTF_CARRIER_PROTECT_BONUS 1        // bonus for fraggin someone while either you or your target are near your flag carrier
#define CTF_FLAG_DEFENSE_BONUS 1           // bonus for fraggin someone while either you or your target are near your flag
#define CTF_RETURN_FLAG_ASSIST_BONUS 1     // awarded for returning a flag that causes a capture to happen almost immediately
#define CTF_FRAG_CARRIER_ASSIST_BONUS 2    // award for fragging a flag carrier if a capture happens almost immediately

#define CTF_TARGET_PROTECT_RADIUS 400   // the radius around an object being defended where a target will be worth extra frags
#define CTF_ATTACKER_PROTECT_RADIUS 400 // the radius around an object being defended where an attacker will get extra frags when making kills

#define CTF_CARRIER_DANGER_PROTECT_TIMEOUT SEC(8)
#define CTF_FRAG_CARRIER_ASSIST_TIMEOUT SEC(10)
#define CTF_RETURN_FLAG_ASSIST_TIMEOUT SEC(10)

#define CTF_AUTO_FLAG_RETURN_TIMEOUT SEC(30) // number of seconds before dropped flag auto-returns

#define CTF_TECH_TIMEOUT SEC(60) // seconds before techs spawn again

#define CTF_DEFAULT_GRAPPLE_SPEED 650      // speed of grapple in flight
#define CTF_DEFAULT_GRAPPLE_PULL_SPEED 650 // speed player is pulled at

void CTFInit(void);
void CTFSpawn(void);
void CTFPrecache(void);
bool G_TeamplayEnabled(void);
void G_AdjustTeamScore(ctfteam_t team, int offset);

void SP_info_player_team1(edict_t *self);
void SP_info_player_team2(edict_t *self);

const char *CTFTeamName(ctfteam_t team);
const char *CTFOtherTeamName(ctfteam_t team);
void        CTFAssignSkin(edict_t *ent, const char *s);
void        CTFAssignTeam(gclient_t *who);
edict_t    *SelectCTFSpawnPoint(edict_t *ent, bool force_spawn);
bool        CTFPickup_Flag(edict_t *ent, edict_t *other);
void        CTFDrop_Flag(edict_t *ent, const gitem_t *item);
void        CTFEffects(edict_t *player);
void        CTFCalcScores(void);
void        CTFCalcRankings(int player_ranks[MAX_CLIENTS]); // [Paril-KEX]
void        CheckEndTDMLevel(void); // [Paril-KEX]
void        SetCTFStats(edict_t *ent);
void        CTFDeadDropFlag(edict_t *self);
void        CTFScoreboardMessage(edict_t *ent, edict_t *killer, bool reliable);
void        CTFTeam_f(edict_t *ent);
void        CTFID_f(edict_t *ent);
void        CTFSay_Team(edict_t *who);
void        CTFFlagSetup(edict_t *ent);
void        CTFResetFlag(ctfteam_t ctf_team);
void        CTFFragBonuses(edict_t *targ, edict_t *inflictor, edict_t *attacker);
void        CTFCheckHurtCarrier(edict_t *targ, edict_t *attacker);
void        CTFDirtyTeamMenu(void);

// GRAPPLE
void CTFWeapon_Grapple(edict_t *ent);
void CTFPlayerResetGrapple(edict_t *ent);
void CTFGrapplePull(edict_t *self);
void CTFResetGrapple(edict_t *self);

// TECH
const gitem_t *CTFWhat_Tech(edict_t *ent);
bool     CTFPickup_Tech(edict_t *ent, edict_t *other);
void     CTFDrop_Tech(edict_t *ent, const gitem_t *item);
void     CTFDeadDropTech(edict_t *ent);
void     CTFSetupTechSpawn(void);
int      CTFApplyResistance(edict_t *ent, int dmg);
int      CTFApplyStrength(edict_t *ent, int dmg);
bool     CTFApplyStrengthSound(edict_t *ent);
bool     CTFApplyHaste(edict_t *ent);
void     CTFApplyHasteSound(edict_t *ent);
void     CTFApplyRegeneration(edict_t *ent);
bool     CTFHasRegeneration(edict_t *ent);
void     CTFRespawnTech(edict_t *ent);
void     CTFResetTech(void);

void CTFOpenJoinMenu(edict_t *ent);
bool CTFStartClient(edict_t *ent);
void CTFVoteYes(edict_t *ent);
void CTFVoteNo(edict_t *ent);
void CTFReady(edict_t *ent);
void CTFNotReady(edict_t *ent);
bool CTFNextMap(void);
bool CTFMatchSetup(void);
bool CTFMatchOn(void);
void CTFGhost(edict_t *ent);
void CTFAdmin(edict_t *ent);
bool CTFInMatch(void);
void CTFStats(edict_t *ent);
void CTFWarp(edict_t *ent);
void CTFBoot(edict_t *ent);
void CTFPlayerList(edict_t *ent);

bool CTFCheckRules(void);

void SP_misc_ctf_banner(edict_t *ent);
void SP_misc_ctf_small_banner(edict_t *ent);

void UpdateChaseCam(edict_t *ent);
void ChaseNext(edict_t *ent);
void ChasePrev(edict_t *ent);

void CTFObserver(edict_t *ent);

void SP_trigger_teleport(edict_t *ent);
void SP_info_teleport_destination(edict_t *ent);

void CTFSetPowerUpEffect(edict_t *ent, effects_t def);
