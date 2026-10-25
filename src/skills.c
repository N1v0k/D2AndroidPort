#include "skills.h"

#include "audio.h"
#include "fmt/formats.h"
#include "fmt/fs.h"
#include "strings.h"
#include "lang.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NUM_CLASSES 8

static SkillDef s_skills[NUM_CLASSES][MAX_CLASS_SKILLS];
static int      s_count[NUM_CLASSES];

/* Todas las filas de skills.txt (por *Id) */
#define MAX_GLOBAL_SKILLS 512
typedef struct {
    char        key[32];
    const char *name;
    int         cls, reqlevel, local;
} GlobalSkill;
static GlobalSkill s_global[MAX_GLOBAL_SKILLS];
static int         s_nglobal;

static const char *const k_class_code[NUM_CLASSES] = { "bar", "ama", "sor", "pal", "nec", "ass", "dru", "war" };
static const char *const k_class_abbr[NUM_CLASSES] = { "Ba", "Am", "So", "Pa", "Ne", "As", "Dr", "Wa" };

static const struct { const char *key; int kind; } k_kinds[] = {
    /* Brujo */
    { "Summon Goatman", SK_SUMMON_GOATMAN },
    { "Demonic Mastery", SK_DEMONIC_MASTERY },
    { "Death Mark", SK_DEATH_MARK },
    { "Summon Tainted", SK_SUMMON_TAINTED },
    { "Summon Defiler", SK_SUMMON_DEFILER },
    { "Blood Oath", SK_BLOOD_OATH },
    { "Engorge", SK_ENGORGE },
    { "Blood Boil", SK_BLOOD_BOIL },
    { "Consume", SK_CONSUME },
    { "Bind Demon", SK_BIND_DEMON },
    { "Levitate", SK_LEVITATE },
    { "Eldritch Blast", SK_ELDRITCH_BLAST },
    { "Hex Bane", SK_HEX_BANE },
    { "Hex Siphon", SK_HEX_SIPHON },
    { "Psychic Ward", SK_PSYCHIC_WARD },
    { "Echoing Strike", SK_ECHOING_STRIKE },
    { "Hex Purge", SK_HEX_PURGE },
    { "Blade Warp", SK_BLADE_WARP },
    { "Cleave", SK_CLEAVE },
    { "Mirrored Blades", SK_MIRRORED_BLADES },
    { "Sigil Lethargy", SK_SIGIL_LETHARGY },
    { "Ring of Fire", SK_RING_OF_FIRE },
    { "Miasma Bolt", SK_MIASMA_BOLT },
    { "Sigil Rancor", SK_SIGIL_RANCOR },
    { "Enhanced Entropy", SK_ENHANCED_ENTROPY },
    { "Flame Wave", SK_FLAME_WAVE },
    { "Miasma Chains", SK_MIASMA_CHAINS },
    { "Sigil Death", SK_SIGIL_DEATH },
    { "Apocalypse", SK_APOCALYPSE },
    { "Abyss", SK_ABYSS },
    /* Druida */
    { "Raven", SK_RAVEN },
    { "Plague Poppy", SK_POISON_CREEPER },
    { "Wearwolf", SK_WEREWOLF },
    { "Shape Shifting", SK_LYCANTHROPY },
    { "Firestorm", SK_FIRESTORM },
    { "Oak Sage", SK_OAK_SAGE },
    { "Summon Spirit Wolf", SK_SPIRIT_WOLF },
    { "Wearbear", SK_WEREBEAR },
    { "Molten Boulder", SK_MOLTEN_BOULDER },
    { "Arctic Blast", SK_ARCTIC_BLAST },
    { "Cycle of Life", SK_CARRION_VINE },
    { "Feral Rage", SK_FERAL_RAGE },
    { "Maul", SK_MAUL },
    { "Eruption", SK_FISSURE },
    { "Cyclone Armor", SK_CYCLONE_ARMOR },
    { "Heart of Wolverine", SK_HEART_WOLVERINE },
    { "Summon Fenris", SK_DIRE_WOLF },
    { "Rabies", SK_RABIES },
    { "Fire Claws", SK_FIRE_CLAWS },
    { "Twister", SK_TWISTER },
    { "Vines", SK_SOLAR_CREEPER },
    { "Hunger", SK_HUNGER },
    { "Shock Wave", SK_SHOCK_WAVE },
    { "Volcano", SK_VOLCANO },
    { "Tornado", SK_TORNADO },
    { "Spirit of Barbs", SK_SPIRIT_BARBS },
    { "Summon Grizzly", SK_GRIZZLY },
    { "Fury", SK_FURY },
    { "Armageddon", SK_ARMAGEDDON },
    { "Hurricane", SK_HURRICANE },
    /* Asesina */
    { "Fire Trauma", SK_FIRE_TRAUMA },
    { "Claw Mastery", SK_CLAW_MASTERY },
    { "Psychic Hammer", SK_PSYCHIC_HAMMER },
    { "Tiger Strike", SK_TIGER_STRIKE },
    { "Dragon Talon", SK_DRAGON_TALON },
    { "Shock Field", SK_SHOCK_FIELD },
    { "Blade Sentinel", SK_BLADE_SENTINEL },
    { "Quickness", SK_QUICKNESS },
    { "Fists of Fire", SK_FISTS_OF_FIRE },
    { "Dragon Claw", SK_DRAGON_CLAW },
    { "Charged Bolt Sentry", SK_CB_SENTRY },
    { "Wake of Fire Sentry", SK_WAKE_OF_FIRE },
    { "Weapon Block", SK_WEAPON_BLOCK },
    { "Cloak of Shadows", SK_CLOAK_SHADOWS },
    { "Cobra Strike", SK_COBRA_STRIKE },
    { "Blade Fury", SK_BLADE_FURY },
    { "Fade", SK_FADE },
    { "Shadow Warrior", SK_SHADOW_WARRIOR },
    { "Claws of Thunder", SK_CLAWS_THUNDER },
    { "Dragon Tail", SK_DRAGON_TAIL },
    { "Lightning Sentry", SK_LIGHTNING_SENTRY },
    { "Inferno Sentry", SK_INFERNO_SENTRY },
    { "Mind Blast", SK_MIND_BLAST },
    { "Blades of Ice", SK_BLADES_ICE },
    { "Dragon Flight", SK_DRAGON_FLIGHT },
    { "Death Sentry", SK_DEATH_SENTRY },
    { "Blade Shield", SK_BLADE_SHIELD },
    { "Venom", SK_VENOM },
    { "Shadow Master", SK_SHADOW_MASTER },
    { "Royal Strike", SK_ROYAL_STRIKE },
    /* Nigromante */
    { "Amplify Damage", SK_AMPLIFY_DAMAGE },
    { "Dim Vision", SK_DIM_VISION },
    { "Weaken", SK_WEAKEN },
    { "Iron Maiden", SK_IRON_MAIDEN },
    { "Terror", SK_TERROR },
    { "Confuse", SK_CONFUSE },
    { "Life Tap", SK_LIFE_TAP },
    { "Attract", SK_ATTRACT },
    { "Decrepify", SK_DECREPIFY },
    { "Lower Resist", SK_LOWER_RESIST },
    { "Teeth", SK_TEETH },
    { "Bone Armor", SK_BONE_ARMOR },
    { "Poison Dagger", SK_POISON_DAGGER },
    { "Corpse Explosion", SK_CORPSE_EXPLOSION },
    { "Bone Wall", SK_BONE_WALL },
    { "Poison Explosion", SK_POISON_EXPLOSION },
    { "Bone Spear", SK_BONE_SPEAR },
    { "Bone Prison", SK_BONE_PRISON },
    { "Poison Nova", SK_POISON_NOVA },
    { "Bone Spirit", SK_BONE_SPIRIT },
    { "Skeleton Mastery", SK_SKELETON_MASTERY },
    { "Raise Skeleton", SK_RAISE_SKELETON },
    { "Clay Golem", SK_CLAY_GOLEM },
    { "Golem Mastery", SK_GOLEM_MASTERY },
    { "Raise Skeletal Mage", SK_RAISE_MAGE },
    { "BloodGolem", SK_BLOOD_GOLEM },
    { "Summon Resist", SK_SUMMON_RESIST },
    { "IronGolem", SK_IRON_GOLEM },
    { "FireGolem", SK_FIRE_GOLEM },
    { "Revive", SK_REVIVE },
    /* Paladín */
    { "Sacrifice", SK_SACRIFICE },
    { "Smite", SK_SMITE },
    { "Holy Bolt", SK_HOLY_BOLT },
    { "Zeal", SK_ZEAL },
    { "Charge", SK_CHARGE },
    { "Vengeance", SK_VENGEANCE },
    { "Blessed Hammer", SK_BLESSED_HAMMER },
    { "Conversion", SK_CONVERSION },
    { "Holy Shield", SK_HOLY_SHIELD },
    { "Fist of the Heavens", SK_FIST_HEAVENS },
    { "Might", SK_MIGHT },
    { "Holy Fire", SK_HOLY_FIRE },
    { "Thorns", SK_THORNS },
    { "Blessed Aim", SK_BLESSED_AIM },
    { "Concentration", SK_CONCENTRATION },
    { "Holy Freeze", SK_HOLY_FREEZE },
    { "Holy Shock", SK_HOLY_SHOCK },
    { "Sanctuary", SK_SANCTUARY },
    { "Fanaticism", SK_FANATICISM },
    { "Conviction", SK_CONVICTION },
    { "Prayer", SK_PRAYER },
    { "Resist Fire", SK_RESIST_FIRE },
    { "Defiance", SK_DEFIANCE },
    { "Resist Cold", SK_RESIST_COLD },
    { "Cleansing", SK_CLEANSING },
    { "Resist Lightning", SK_RESIST_LIGHTNING },
    { "Vigor", SK_VIGOR },
    { "Meditation", SK_MEDITATION },
    { "Redemption", SK_REDEMPTION },
    { "Salvation", SK_SALVATION },
    /* Hechicera */
    { "Fire Bolt", SK_FIRE_BOLT },
    { "Warmth", SK_WARMTH },
    { "Inferno", SK_INFERNO },
    { "Blaze", SK_BLAZE },
    { "Fire Ball", SK_FIRE_BALL },
    { "Fire Wall", SK_FIRE_WALL },
    { "Enchant", SK_ENCHANT },
    { "Meteor", SK_METEOR },
    { "Fire Mastery", SK_FIRE_MASTERY },
    { "Hydra", SK_HYDRA },
    { "Charged Bolt", SK_CHARGED_BOLT },
    { "Static Field", SK_STATIC_FIELD },
    { "Telekinesis", SK_TELEKINESIS },
    { "Nova", SK_NOVA },
    { "Lightning", SK_LIGHTNING },
    { "Chain Lightning", SK_CHAIN_LIGHTNING },
    { "Teleport", SK_TELEPORT },
    { "Thunder Storm", SK_THUNDER_STORM },
    { "Energy Shield", SK_ENERGY_SHIELD },
    { "Lightning Mastery", SK_LIGHTNING_MASTERY },
    { "Ice Bolt", SK_ICE_BOLT },
    { "Frozen Armor", SK_FROZEN_ARMOR },
    { "Frost Nova", SK_FROST_NOVA },
    { "Ice Blast", SK_ICE_BLAST },
    { "Shiver Armor", SK_SHIVER_ARMOR },
    { "Glacial Spike", SK_GLACIAL_SPIKE },
    { "Blizzard", SK_BLIZZARD },
    { "Chilling Armor", SK_CHILLING_ARMOR },
    { "Frozen Orb", SK_FROZEN_ORB },
    { "Cold Mastery", SK_COLD_MASTERY },
    { "Bash", SK_BASH },
    { "Stun", SK_STUN },
    { "Concentrate", SK_CONCENTRATE },
    { "Howl", SK_HOWL },
    { "Shout", SK_SHOUT },
    { "Taunt", SK_TAUNT },
    { "Battle Cry", SK_BATTLE_CRY },
    { "Find Potion", SK_FIND_POTION },
    { "Find Item", SK_FIND_ITEM },
    { "Blade Mastery", SK_MASTERY },
    { "Axe Mastery", SK_MASTERY },
    { "Mace Mastery", SK_MASTERY },
    { "Pole Arm Mastery", SK_MASTERY },
    { "Spear Mastery", SK_MASTERY },
    { "Increased Stamina", SK_INC_STAMINA },
    { "Iron Skin", SK_IRON_SKIN },
    { "Jab", SK_JAB },
    { "Impale", SK_IMPALE },
    { "Power Strike", SK_POWER_STRIKE },
    { "Inner Sight", SK_INNER_SIGHT },
    { "Critical Strike", SK_CRITICAL_STRIKE },
    { "Dodge", SK_DODGE },
    { "Penetrate", SK_PENETRATE },
    { "Throwing Mastery", SK_MASTERY },
    { "Magic Arrow", SK_MAGIC_ARROW },
    { "Fire Arrow", SK_FIRE_ARROW },
    { "Cold Arrow", SK_COLD_ARROW },
    { "Multiple Shot", SK_MULTIPLE_SHOT },
    { "Exploding Arrow", SK_EXPLODING_ARROW },
    { "Ice Arrow", SK_ICE_ARROW },
    { "Guided Arrow", SK_GUIDED_ARROW },
    { "Poison Javelin", SK_POISON_JAVELIN },
    { "Lightning Bolt", SK_LIGHTNING_BOLT },
    { "Plague Javelin", SK_PLAGUE_JAVELIN },
    { "Leap", SK_LEAP },
    { "Double Swing", SK_DOUBLE_SWING },
    { "Double Throw", SK_DOUBLE_THROW },
    { "Leap Attack", SK_LEAP_ATTACK },
    { "Frenzy", SK_FRENZY },
    { "Whirlwind", SK_WHIRLWIND },
    { "Berserk", SK_BERSERK },
    { "Increased Speed", SK_INC_SPEED },
    { "Natural Resistance", SK_NAT_RESIST },
    { "Battle Orders", SK_BATTLE_ORDERS },
    { "Grim Ward", SK_GRIM_WARD },
    { "War Cry", SK_WAR_CRY },
    { "Battle Command", SK_BATTLE_COMMAND },
    { "Charged Strike", SK_CHARGED_STRIKE },
    { "Fend", SK_FEND },
    { "Lightning Strike", SK_LIGHTNING_STRIKE },
    { "Lightning Fury", SK_LIGHTNING_FURY },
    { "Slow Missiles", SK_SLOW_MISSILES },
    { "Avoid", SK_AVOID },
    { "Dopplezon", SK_DECOY },
    { "Evade", SK_EVADE },
    { "Valkyrie", SK_VALKYRIE },
    { "Pierce", SK_PIERCE },
    { "Strafe", SK_STRAFE },
    { "Immolation Arrow", SK_IMMOLATION_ARROW },
    { "Freezing Arrow", SK_FREEZING_ARROW },
};

/* Las columnas de cada fórmula (en el orden de SC_*) */
static const char *const k_formula_cols[SC_COUNT] = {
    "calc1", "calc2", "calc3", "calc4", "auralencalc", "aurarangecalc", "aurastatcalc1", "aurastatcalc2",
    "aurastatcalc3", "passivecalc1", "passivecalc2", "passivecalc3", "EDmgSymPerCalc", "ELenSymPerCalc",
    "DmgSymPerCalc", "passivecalc4", "passivecalc5", "aurastatcalc4",
};

static int load_table(TxtTable *t, const char *name)
{
    char path[128];
    snprintf(path, sizeof path, "data/global/excel/%s.txt", name);
    size_t n;
    uint8_t *d = fs_read(path, &n);
    int r = d ? txt_load(d, n, t) : -1;
    free(d);
    if (r != 0)
        fprintf(stderr, "falta %s\n", path);
    return r;
}

static void copy_lower(char *dst, size_t cap, const char *src)
{
    size_t i = 0;
    for (; src[i] && i + 1 < cap; i++)
        dst[i] = (char)tolower((unsigned char)src[i]);
    dst[i] = '\0';
}

int skills_init(void)
{
    TxtTable sk = { 0 }, desc = { 0 };
    if (load_table(&sk, "skills") != 0 || load_table(&desc, "skilldesc") != 0) {
        txt_free(&sk);
        txt_free(&desc);
        return -1;
    }
    char reqs[NUM_CLASSES][MAX_CLASS_SKILLS][3][32];
    memset(reqs, 0, sizeof reqs);
    s_nglobal = 0;
    for (int r = 1; r < sk.rows; r++) {
        const char *cc = txt_get(&sk, r, txt_col(&sk, "charclass"));
        int cls = -1;
        for (int c = 0; c < NUM_CLASSES; c++)
            if (strcmp(cc, k_class_code[c]) == 0)
                cls = c;
        { /* la fila global (por *Id) */
            int gid = txt_get_int(&sk, r, "*Id", r - 1);
            if (gid >= 0 && gid < MAX_GLOBAL_SKILLS) {
                GlobalSkill *g = &s_global[gid];
                snprintf(g->key, sizeof g->key, "%s", txt_get(&sk, r, txt_col(&sk, "skill")));
                int dr = txt_find_row(&desc, 0, txt_get(&sk, r, txt_col(&sk, "skilldesc")));
                g->name = dr >= 0 ? strdup(str_get(txt_get(&desc, dr, txt_col(&desc, "str name")))) : strdup(g->key);
                g->cls = cls;
                g->reqlevel = txt_get_int(&sk, r, "reqlevel", 1);
                g->local = -1;
                if (gid + 1 > s_nglobal)
                    s_nglobal = gid + 1;
            }
        }
        if (cls < 0 || s_count[cls] >= MAX_CLASS_SKILLS)
            continue;
        /* Solo las que tienen lugar en el árbol */
        int drow = txt_find_row(&desc, 0, txt_get(&sk, r, txt_col(&sk, "skilldesc")));
        if (drow < 0 || txt_get_int(&desc, drow, "SkillPage", 0) <= 0)
            continue;
        int idx = s_count[cls]++;
        SkillDef *s = &s_skills[cls][idx];
        memset(s, 0, sizeof *s);
        s->global_id = txt_get_int(&sk, r, "*Id", r - 1);
        if (s->global_id >= 0 && s->global_id < MAX_GLOBAL_SKILLS)
            s_global[s->global_id].local = idx;
        copy_lower(s->etype, sizeof s->etype, txt_get(&sk, r, txt_col(&sk, "EType")));
        snprintf(s->key, sizeof s->key, "%s", txt_get(&sk, r, txt_col(&sk, "skill")));
        s->name = strdup(str_get(txt_get(&desc, drow, txt_col(&desc, "str name"))));
        s->flavor = strdup(str_get(txt_get(&desc, drow, txt_col(&desc, "str long"))));
        s->page = txt_get_int(&desc, drow, "SkillPage", 1);
        s->row = txt_get_int(&desc, drow, "SkillRow", 1);
        s->col = txt_get_int(&desc, drow, "SkillColumn", 1);
        s->icon = txt_get_int(&desc, drow, "IconCel", 0);
        s->passive = txt_get_int(&sk, r, "passive", 0);
        s->reqlevel = txt_get_int(&sk, r, "reqlevel", 1);
        s->mana = txt_get_int(&sk, r, "mana", 0);
        s->lvlmana = txt_get_int(&sk, r, "lvlmana", 0);
        s->manashift = txt_get_int(&sk, r, "manashift", 8);
        s->minmana = txt_get_int(&sk, r, "minmana", 0);
        s->tohit = txt_get_int(&sk, r, "ToHit", 0);
        s->levtohit = txt_get_int(&sk, r, "LevToHit", 0);
        s->emin = txt_get_int(&sk, r, "EMin", 0);
        s->emax = txt_get_int(&sk, r, "EMax", 0);
        for (int k = 0; k < 5; k++) {
            char col[16];
            snprintf(col, sizeof col, "EMinLev%d", k + 1);
            s->eminlev[k] = txt_get_int(&sk, r, col, 0);
            snprintf(col, sizeof col, "EMaxLev%d", k + 1);
            s->emaxlev[k] = txt_get_int(&sk, r, col, 0);
        }
        s->hitshift = txt_get_int(&sk, r, "HitShift", 8);
        s->elen = txt_get_int(&sk, r, "ELen", 0);
        s->mindam = txt_get_int(&sk, r, "MinDam", 0);
        s->maxdam = txt_get_int(&sk, r, "MaxDam", 0);
        s->srcdam = txt_get_int(&sk, r, "SrcDam", 128);
        s->decquant = txt_get_int(&sk, r, "decquant", 0);
        for (int k = 0; k < 5; k++) {
            char col[16];
            snprintf(col, sizeof col, "MinLevDam%d", k + 1);
            s->minlevdam[k] = txt_get_int(&sk, r, col, 0);
            snprintf(col, sizeof col, "MaxLevDam%d", k + 1);
            s->maxlevdam[k] = txt_get_int(&sk, r, col, 0);
            if (k < 3) {
                snprintf(col, sizeof col, "ELevLen%d", k + 1);
                s->elevlen[k] = txt_get_int(&sk, r, col, 0);
            }
        }
        const char *mis = txt_get(&sk, r, txt_col(&sk, "srvmissile"));
        if (!mis[0])
            mis = txt_get(&sk, r, txt_col(&sk, "srvmissilea"));
        snprintf(s->missile, sizeof s->missile, "%s", mis);
        snprintf(s->missile_b, sizeof s->missile_b, "%s", txt_get(&sk, r, txt_col(&sk, "srvmissileb")));
        snprintf(s->missile_c, sizeof s->missile_c, "%s", txt_get(&sk, r, txt_col(&sk, "srvmissilec")));
        for (int k = 0; k < SC_COUNT; k++) {
            /* Las que tienen comas, como "min(lvl*par7,85)", vienen entre comillas */
            const char *f = txt_get(&sk, r, txt_col(&sk, k_formula_cols[k]));
            size_t len = strlen(f);
            if (len >= 2 && f[0] == '"' && f[len - 1] == '"')
                snprintf(s->formula[k], sizeof s->formula[k], "%.*s", (int)(len - 2), f + 1);
            else
                snprintf(s->formula[k], sizeof s->formula[k], "%s", f);
        }
        for (int k = 0; k < 5; k++) {
            char col[24];
            if (k < 4) {
                snprintf(col, sizeof col, "aurastat%d", k + 1);
                snprintf(s->aurastat[k], sizeof s->aurastat[k], "%s", txt_get(&sk, r, txt_col(&sk, col)));
            }
            snprintf(col, sizeof col, "passivestat%d", k + 1);
            snprintf(s->passivestat[k], sizeof s->passivestat[k], "%s", txt_get(&sk, r, txt_col(&sk, col)));
        }
        s->aura = txt_get_int(&sk, r, "aura", 0);
        s->restrict_form = txt_get_int(&sk, r, "restrict", 0);
        snprintf(s->state1, sizeof s->state1, "%s", txt_get(&sk, r, txt_col(&sk, "State1")));
        snprintf(s->state2, sizeof s->state2, "%s", txt_get(&sk, r, txt_col(&sk, "State2")));
        s->passive_always = txt_get(&sk, r, txt_col(&sk, "passivestate"))[0] != '\0';
        s->perdelay = txt_get_int(&sk, r, "perdelay", 0);
        snprintf(s->anim, sizeof s->anim, "%s", txt_get(&sk, r, txt_col(&sk, "anim")));
        s->seqnum = txt_get_int(&sk, r, "seqnum", 0);
        snprintf(s->castoverlay, sizeof s->castoverlay, "%s", txt_get(&sk, r, txt_col(&sk, "castoverlay")));
        snprintf(s->srvoverlay, sizeof s->srvoverlay, "%s", txt_get(&sk, r, txt_col(&sk, "srvoverlay")));
        snprintf(s->summon, sizeof s->summon, "%s", txt_get(&sk, r, txt_col(&sk, "summon")));
        if (!s->summon[0] && !strcmp(txt_get(&sk, r, txt_col(&sk, "pettype")), "revive"))
            snprintf(s->summon, sizeof s->summon, "revive"); /* Revivir: es de las invocaciones (el monstruo es el cadáver) */
        {
            const char *f = txt_get(&sk, r, txt_col(&sk, "petmax"));
            size_t len = strlen(f);
            if (len >= 2 && f[0] == '"' && f[len - 1] == '"')
                snprintf(s->petmax, sizeof s->petmax, "%.*s", (int)(len - 2), f + 1);
            else
                snprintf(s->petmax, sizeof s->petmax, "%s", f);
            f = txt_get(&sk, r, txt_col(&sk, "sumsk1calc"));
            len = strlen(f);
            if (len >= 2 && f[0] == '"' && f[len - 1] == '"')
                snprintf(s->sumskcalc, sizeof s->sumskcalc, "%.*s", (int)(len - 2), f + 1);
            else
                snprintf(s->sumskcalc, sizeof s->sumskcalc, "%s", f);
        }
        s->intown = txt_get_int(&sk, r, "InTown", 0);
        s->delay = txt_get_int(&sk, r, "localdelay", 0);
        s->gdelay = txt_get_int(&sk, r, "globaldelay", 0);
        s->st_sound = audio_find(txt_get(&sk, r, txt_col(&sk, "stsound")));
        s->st_sound_class = audio_find(txt_get(&sk, r, txt_col(&sk, "stsoundclass")));
        s->weapon_sound = txt_get_int(&sk, r, "weaponsnd", 0) == 1;
        audio_preload(s->st_sound);
        audio_preload(s->st_sound_class);
        for (int k = 1; k <= 14; k++) {
            char col[16];
            snprintf(col, sizeof col, "Param%d", k);
            s->param[k] = txt_get_int(&sk, r, col, 0);
        }
        copy_lower(s->passive_itype, sizeof s->passive_itype, txt_get(&sk, r, txt_col(&sk, "passiveitype")));
        for (int k = 0; k < 3; k++) {
            char col[16];
            snprintf(col, sizeof col, "itypea%d", k + 1);
            copy_lower(s->weapon_itype[k], sizeof s->weapon_itype[k], txt_get(&sk, r, txt_col(&sk, col)));
            if (k == 0)
                copy_lower(s->weapon_itypeb, sizeof s->weapon_itypeb, txt_get(&sk, r, txt_col(&sk, "itypeb1")));
            snprintf(col, sizeof col, "reqskill%d", k + 1);
            snprintf(reqs[cls][idx][k], sizeof reqs[cls][idx][k], "%s", txt_get(&sk, r, txt_col(&sk, col)));
        }
        for (unsigned k = 0; k < sizeof k_kinds / sizeof k_kinds[0]; k++)
            if (strcmp(s->key, k_kinds[k].key) == 0)
                s->kind = k_kinds[k].kind;
    }
    /* Los requisitos se resuelven al final: pueden estar más abajo en la tabla */
    for (int c = 0; c < NUM_CLASSES; c++)
        for (int i = 0; i < s_count[c]; i++)
            for (int k = 0; k < 3; k++)
                s_skills[c][i].req[k] = reqs[c][i][k][0] ? skills_find(c, reqs[c][i][k]) : -1;
    txt_free(&sk);
    txt_free(&desc);
    return 0;
}

int skills_global_find(const char *name)
{
    if (!name || !name[0])
        return -1;
    if (isdigit((unsigned char)name[0]))
        return atoi(name);
    for (int i = 0; i < s_nglobal; i++)
        if (strcasecmp(s_global[i].key, name) == 0)
            return i;
    return -1;
}

const char *skills_global_name(int id)
{
    return id >= 0 && id < s_nglobal && s_global[id].name ? s_global[id].name : "";
}

int skills_global_class(int id)
{
    return id >= 0 && id < s_nglobal ? s_global[id].cls : -1;
}

int skills_global_reqlevel(int id)
{
    return id >= 0 && id < s_nglobal ? s_global[id].reqlevel : 1;
}

int skills_global_local(int id)
{
    return id >= 0 && id < s_nglobal && s_global[id].cls >= 0 ? s_global[id].local : -1;
}

int skills_count(int cls)
{
    return cls >= 0 && cls < NUM_CLASSES ? s_count[cls] : 0;
}

const SkillDef *skills_get(int cls, int i)
{
    return &s_skills[cls][i];
}

int skills_find(int cls, const char *key)
{
    for (int i = 0; i < skills_count(cls); i++)
        if (strcmp(s_skills[cls][i].key, key) == 0)
            return i;
    return -1;
}

/* La solapa 1 del árbol es la categoría 3 de los textos, y así */
const char *skills_tab_name(int cls, int page)
{
    static char key[32];
    /* (las del Brujo van al derecho: Wa1 es la primera solapa) */
    snprintf(key, sizeof key, "SkillCategory%s%d", k_class_abbr[cls], cls == 7 ? page : 4 - page);
    return str_get(key);
}

const char *skills_icon_file(int cls)
{
    static const char *const files[NUM_CLASSES] = { "baskillicon", "amskillicon", "soskillicon", "paskillicon",
                                                    "neskillicon", "asskillicon", "drskillicon", "waskillicon" };
    return files[cls >= 0 && cls < NUM_CLASSES ? cls : 0];
}

int skill_ln(const SkillDef *s, int a, int b, int lvl)
{
    return s->param[a] + (lvl - 1) * s->param[b];
}

int skill_dm(const SkillDef *s, int a, int b, int lvl)
{
    if (lvl < 1)
        return s->param[a];
    return s->param[a] + (s->param[b] - s->param[a]) * (110 * lvl / (lvl + 6)) / 100;
}

/* El maná está en 1/256 de punto: (mana + lvlmana * (nivel - 1)) << manashift, con el mínimo en
   minmana << 8 (SKILLS_GetManaCosts y D2Game). Con multiplicación: las que cuestan menos por nivel
   (Teletransportar...) dan negativo a nivel alto, y desplazar un negativo no está definido en C */
int skill_mana(const SkillDef *s, int lvl)
{
    int m = (s->mana + s->lvlmana * (lvl - 1)) * (1 << s->manashift), min = s->minmana * 256;
    return (m < min ? min : m) / 256;
}

int skill_tohit(const SkillDef *s, int lvl)
{
    return s->tohit + s->levtohit * (lvl - 1);
}

/* El daño elemental sube distinto según el tramo de niveles: 2-8, 9-16,
   17-22, 23-28 y 29 en adelante */
static const int k_from[5] = { 2, 9, 17, 23, 29 }, k_to[5] = { 8, 16, 22, 28, 99 };

static void by_level(int lvl, int base_min, int base_max, const int *lev_min, const int *lev_max, int tranches,
                     int *min, int *max)
{
    *min = base_min;
    *max = base_max;
    for (int k = 0; k < tranches; k++) {
        int n = (lvl < k_to[k] ? lvl : k_to[k]) - k_from[k] + 1;
        if (n <= 0)
            break;
        *min += n * lev_min[k];
        *max += n * lev_max[k];
    }
}

/* El veneno (hitshift 0) queda por tick en 1/256: lo convierte skill_poison_total */
void skill_elem(const SkillDef *s, int lvl, int *min, int *max)
{
    by_level(lvl, s->emin, s->emax, s->eminlev, s->emaxlev, 5, min, max);
    if (s->hitshift > 0 && s->hitshift != 8) {
        *min = (*min << s->hitshift) >> 8;
        *max = (*max << s->hitshift) >> 8;
    }
}

void skill_phys(const SkillDef *s, int lvl, int *min, int *max)
{
    by_level(lvl, s->mindam, s->maxdam, s->minlevdam, s->maxlevdam, 5, min, max);
}

int skill_elen(const SkillDef *s, int lvl)
{
    int a, b;
    by_level(lvl, s->elen, s->elen, s->elevlen, s->elevlen, 3, &a, &b);
    return a;
}

void skill_poison_total(const SkillDef *s, int lvl, int *min, int *max)
{
    int lo, hi, len = skill_elen(s, lvl);
    by_level(lvl, s->emin, s->emax, s->eminlev, s->emaxlev, 5, &lo, &hi);
    /* (valor << hitshift) es el veneno por tick en 1/256 de vida */
    *min = (lo << s->hitshift) * len / 256;
    *max = (hi << s->hitshift) * len / 256;
    if (*min < 1) *min = 1;
    if (*max < *min) *max = *min;
}

int skill_missiles(const SkillDef *s, int lvl)
{
    if (s->kind != SK_MULTIPLE_SHOT)
        return 1;
    int n = skill_ln(s, 1, 2, lvl);
    return n < 1 ? 1 : n > 24 ? 24 : n;
}

/* ---- Las fórmulas de skills.txt ----
   Enteros como en el original: lnXY y dmXY, parN, lvl, toht (la puntería de
   la habilidad), edmn (el daño elemental mínimo), skill('Nombre'.blvl) (los
   puntos puestos en otra habilidad: las sinergias), min y max, + - * / y
   paréntesis. stat(...) (los objetos con "ignora inmunidad") da 0. */
typedef struct {
    const char     *p;
    const SkillDef *s;
    int             lvl;
    const SkillCtx *c;
} Calc;

static int calc_expr(Calc *k);

/* edns / enms (y edxs / exms): el daño elemental en 1/256 (sin el HitShift),
   con las sinergias: Plegaria cura "edns" (hitpoints va en 1/256) y Fuego
   sagrado le suma "enms * 6 / 256" de fuego al arma */
static int elem_shifted(const SkillDef *s, int lvl, const SkillCtx *c, int max)
{
    int lo, hi;
    by_level(lvl, s->emin, s->emax, s->eminlev, s->emaxlev, 5, &lo, &hi);
    int v = (max ? hi : lo) << s->hitshift;
    int pct = c ? skill_calc(s, SC_EDMGSYM, lvl, c) : 0;
    return v * (100 + pct) / 100;
}

static void calc_space(Calc *k)
{
    while (*k->p == ' ' || *k->p == '\t')
        k->p++;
}

static int calc_accept(Calc *k, char ch)
{
    calc_space(k);
    if (*k->p != ch)
        return 0;
    k->p++;
    return 1;
}

static int calc_ident(Calc *k, const char *name, size_t len)
{
    const SkillDef *s = k->s;
    if (len == 4 && (strncmp(name, "ln", 2) == 0 || strncmp(name, "dm", 2) == 0) && isdigit((unsigned char)name[2]) &&
        isdigit((unsigned char)name[3])) {
        int a = name[2] - '0', b = name[3] - '0';
        if (a == 9 && b == 1) /* las de D2R: Param9 y Param10 */
            b = 10;
        else if (a == 2 && b == 1) { /* ln21: Param11 y Param12 (el Atiborrarse, la velocidad de los demonios) */
            a = 11;
            b = 12;
        } else if (a == 4 && b == 3) { /* ln43: Param13 y Param14 (el daño fijo del demonio vinculado) */
            a = 13;
            b = 14;
        }
        if (a < 1 || a > 14 || b < 1 || b > 14)
            return 0;
        return name[0] == 'l' ? skill_ln(s, a, b, k->lvl) : skill_dm(s, a, b, k->lvl);
    }
    if (len == 4 && strncmp(name, "par", 3) == 0 && isdigit((unsigned char)name[3]))
        return name[3] >= '1' && name[3] <= '9' ? s->param[name[3] - '0'] : 0;
    if (len == 4 && strncmp(name, "pa1", 3) == 0 && name[3] >= '0' && name[3] <= '4') /* pa10..pa14 */
        return s->param[10 + name[3] - '0'];
    if (len == 3 && strncmp(name, "lvl", 3) == 0)
        return k->lvl;
    if (len == 4 && strncmp(name, "toht", 4) == 0)
        return skill_tohit(s, k->lvl);
    if (len == 4 && strncmp(name, "ulvl", 4) == 0) /* el nivel del personaje */
        return k->c ? k->c->ulvl : 0;
    if (len == 4 && strncmp(name, "edln", 4) == 0) /* la duración del frío o el veneno */
        return skill_elen(s, k->lvl);
    if (len == 4 && (strncmp(name, "edmn", 4) == 0 || strncmp(name, "edmx", 4) == 0)) {
        int lo, hi;
        skill_elem(s, k->lvl, &lo, &hi);
        return name[3] == 'n' ? lo : hi;
    }
    if (len == 4 && (strncmp(name, "edns", 4) == 0 || strncmp(name, "edxs", 4) == 0 || strncmp(name, "enms", 4) == 0 ||
                     strncmp(name, "exms", 4) == 0))
        return elem_shifted(s, k->lvl, k->c, name[1] == 'x' || name[2] == 'x');
    return 0;
}

static int calc_primary(Calc *k)
{
    calc_space(k);
    if (calc_accept(k, '-'))
        return -calc_primary(k);
    if (calc_accept(k, '(')) {
        int v = calc_expr(k);
        calc_accept(k, ')');
        return v;
    }
    if (isdigit((unsigned char)*k->p)) {
        int v = 0;
        while (isdigit((unsigned char)*k->p))
            v = v * 10 + (*k->p++ - '0');
        return v;
    }
    const char *name = k->p;
    while (isalnum((unsigned char)*k->p) || *k->p == '_')
        k->p++;
    size_t len = (size_t)(k->p - name);
    if (len == 0) { /* algo que no se entiende: se corta ahí */
        if (*k->p)
            k->p++;
        return 0;
    }
    if (calc_accept(k, '(')) {
        /* skill('Nombre'.blvl) y stat('...'.accr): el texto entre comillas */
        calc_space(k);
        if (*k->p == '\'') {
            const char *q = ++k->p;
            while (*k->p && *k->p != '\'')
                k->p++;
            char ref[40];
            size_t n = (size_t)(k->p - q) < sizeof ref - 1 ? (size_t)(k->p - q) : sizeof ref - 1;
            memcpy(ref, q, n);
            ref[n] = '\0';
            if (*k->p)
                k->p++;
            char field[8] = "";
            if (*k->p == '.') { /* .blvl / .lvl / .accr / .edns */
                const char *f = ++k->p;
                while (isalnum((unsigned char)*k->p))
                    k->p++;
                snprintf(field, sizeof field, "%.*s", (int)(k->p - f), f);
            }
            while (*k->p && *k->p != ')')
                k->p++;
            calc_accept(k, ')');
            if (len == 5 && strncmp(name, "skill", 5) == 0 && k->c && k->c->blvl) {
                int i = skills_find(k->c->cls, ref);
                if (i < 0)
                    return 0;
                if (strcmp(field, "edns") == 0 || strcmp(field, "edxs") == 0) /* Purificación: lo que cura Plegaria */
                    return k->c->blvl[i] > 0 ? elem_shifted(&s_skills[k->c->cls][i], k->c->blvl[i], k->c, field[2] == 'x') : 0;
                if (strncmp(field, "par", 3) == 0 || strncmp(field, "ln", 2) == 0 || strncmp(field, "dm", 2) == 0 ||
                    strncmp(field, "edm", 3) == 0 || strncmp(field, "pa1", 3) == 0) { /* skill('Skeleton Mastery'.par1), skill('Golem Mastery'.ln12) */
                    Calc o = { field, &s_skills[k->c->cls][i], k->c->blvl[i], k->c };
                    return calc_ident(&o, field, strlen(field));
                }
                return k->c->blvl[i];
            }
            return 0;
        }
        int a = calc_expr(k), b = 0, two = calc_accept(k, ',');
        if (two)
            b = calc_expr(k);
        calc_accept(k, ')');
        if (len == 3 && strncmp(name, "min", 3) == 0)
            return two && b < a ? b : a;
        if (len == 3 && strncmp(name, "max", 3) == 0)
            return two && b > a ? b : a;
        return a;
    }
    return calc_ident(k, name, len);
}

static int calc_term(Calc *k)
{
    int v = calc_primary(k);
    for (;;) {
        if (calc_accept(k, '*')) {
            v *= calc_primary(k);
        } else if (calc_accept(k, '/')) {
            int d = calc_primary(k);
            v = d ? v / d : 0;
        } else {
            return v;
        }
    }
}

static int calc_sum(Calc *k)
{
    int v = calc_term(k);
    for (;;) {
        if (calc_accept(k, '+'))
            v += calc_term(k);
        else if (calc_accept(k, '-'))
            v -= calc_term(k);
        else
            return v;
    }
}

/* < > <= >= == != (dan 1 o 0) */
static int calc_cmp(Calc *k)
{
    int v = calc_sum(k);
    for (;;) {
        calc_space(k);
        char a = k->p[0], b = a ? k->p[1] : 0;
        if ((a == '<' || a == '>') && b == '=') {
            k->p += 2;
            int w = calc_sum(k);
            v = a == '<' ? v <= w : v >= w;
        } else if (a == '<' || a == '>') {
            k->p++;
            int w = calc_sum(k);
            v = a == '<' ? v < w : v > w;
        } else if ((a == '=' || a == '!') && b == '=') {
            k->p += 2;
            int w = calc_sum(k);
            v = a == '=' ? v == w : v != w;
        } else {
            return v;
        }
    }
}

static int calc_expr(Calc *k)
{
    int c = calc_cmp(k);
    if (!calc_accept(k, '?'))
        return c;
    int a = calc_expr(k);
    calc_accept(k, ':');
    int b = calc_expr(k);
    return c ? a : b;
}

int skill_eval(const SkillDef *s, const char *expr, int lvl, const SkillCtx *c)
{
    if (!expr || !expr[0])
        return 0;
    Calc k = { expr, s, lvl, c };
    return calc_expr(&k);
}

int skill_has(const SkillDef *s, int which)
{
    return which >= 0 && which < SC_COUNT && s->formula[which][0];
}

int skill_calc(const SkillDef *s, int which, int lvl, const SkillCtx *c)
{
    return skill_has(s, which) ? skill_eval(s, s->formula[which], lvl, c) : 0;
}

void skill_elem_syn(const SkillDef *s, int lvl, const SkillCtx *c, int *min, int *max)
{
    skill_elem(s, lvl, min, max);
    int pct = skill_calc(s, SC_EDMGSYM, lvl, c);
    *min = *min * (100 + pct) / 100;
    *max = *max * (100 + pct) / 100;
}

int skill_elen_syn(const SkillDef *s, int lvl, const SkillCtx *c)
{
    return skill_elen(s, lvl) * (100 + skill_calc(s, SC_ELENSYM, lvl, c)) / 100;
}

void skill_phys_syn(const SkillDef *s, int lvl, const SkillCtx *c, int *min, int *max)
{
    skill_phys(s, lvl, min, max);
    int pct = skill_calc(s, SC_DMGSYM, lvl, c);
    *min = *min * (100 + pct) / 100;
    *max = *max * (100 + pct) / 100;
}

void skill_elem_rate(const SkillDef *s, int lvl, const SkillCtx *c, int frames, int *min, int *max)
{
    int lo, hi;
    by_level(lvl, s->emin, s->emax, s->eminlev, s->emaxlev, 5, &lo, &hi);
    int shift = s->hitshift > 0 ? s->hitshift : 8, pct = skill_calc(s, SC_EDMGSYM, lvl, c);
    *min = (int)((int64_t)(lo << shift) * frames / 256 * (100 + pct) / 100);
    *max = (int)((int64_t)(hi << shift) * frames / 256 * (100 + pct) / 100);
    if (*max < *min)
        *max = *min;
}

void skill_poison_syn(const SkillDef *s, int lvl, const SkillCtx *c, int *min, int *max)
{
    skill_poison_total(s, lvl, min, max);
    int pct = skill_calc(s, SC_EDMGSYM, lvl, c);
    *min = *min * (100 + pct) / 100;
    *max = *max * (100 + pct) / 100;
}

const char *skill_effect_text(const SkillDef *s, int lvl, const SkillCtx *c, char *buf, size_t cap)
{
    int lo, hi;
#define CALC(k) skill_calc(s, (k), lvl, c)
    switch (s->kind) {
    case SK_BASH:
        snprintf(buf, cap, tr("+%d%% de daño, +%d de daño, empuja"), CALC(SC_CALC1), CALC(SC_CALC2));
        break;
    case SK_STUN:
        snprintf(buf, cap, tr("+%d%% de puntería, +%d%% de daño, aturde %.1f s"), skill_ln(s, 1, 2, lvl), CALC(SC_CALC1),
                 skill_elen_syn(s, lvl, c) / 25.0f);
        break;
    case SK_CONCENTRATE:
        snprintf(buf, cap, tr("+%d%% de daño, +%d%% de puntería, +%d%% de defensa"), CALC(SC_CALC1), skill_tohit(s, lvl),
                 CALC(SC_AURASTAT1));
        break;
    case SK_HOWL:
        snprintf(buf, cap, tr("Los monstruos huyen %d s (radio %d)"), CALC(SC_CALC3) / 25, CALC(SC_CALC2) * 2 / 3);
        break;
    case SK_SHOUT:
        snprintf(buf, cap, tr("+%d%% de defensa al grupo por %d s"), CALC(SC_AURASTAT1), CALC(SC_AURALEN) / 25);
        break;
    case SK_BATTLE_CRY:
        snprintf(buf, cap, tr("Monstruos: %d%% de defensa y %d%% de daño por %d s"), CALC(SC_AURASTAT1), CALC(SC_AURASTAT2),
                 CALC(SC_AURALEN) / 25);
        break;
    case SK_TAUNT:
        snprintf(buf, cap, tr("Un monstruo: %d%% de puntería y %d%% de daño"), CALC(SC_AURASTAT1), CALC(SC_AURASTAT2));
        break;
    case SK_FIND_POTION:
        snprintf(buf, cap, tr("%d%% de encontrar una poción en un cadáver"), CALC(SC_CALC1));
        break;
    case SK_FIND_ITEM:
        snprintf(buf, cap, tr("%d%% de encontrar un objeto en un cadáver"), CALC(SC_CALC1));
        break;
    case SK_MASTERY:
        snprintf(buf, cap, tr("+%d%% de daño, +%d%% de puntería, %d%% de golpe crítico"), CALC(SC_PASSIVE2),
                 CALC(SC_PASSIVE1), CALC(SC_PASSIVE3));
        break;
    case SK_INC_STAMINA:
        snprintf(buf, cap, tr("+%d%% de estamina"), CALC(SC_PASSIVE1));
        break;
    case SK_IRON_SKIN:
        snprintf(buf, cap, tr("+%d%% de defensa"), CALC(SC_PASSIVE1));
        break;
    case SK_INC_SPEED:
        snprintf(buf, cap, tr("+%d%% de velocidad al caminar y correr"), CALC(SC_PASSIVE1));
        break;
    case SK_NAT_RESIST:
        snprintf(buf, cap, tr("+%d%% a las resistencias de fuego, frío y rayo"), CALC(SC_PASSIVE1));
        break;
    case SK_LEAP:
        snprintf(buf, cap, tr("Salta hasta %d subtiles y empuja a los de alrededor"), CALC(SC_AURARANGE));
        break;
    case SK_LEAP_ATTACK:
        snprintf(buf, cap, tr("Cae sobre el monstruo: +%d%% de daño, +%d%% de puntería"), CALC(SC_CALC1),
                 skill_tohit(s, lvl));
        break;
    case SK_DOUBLE_SWING:
        snprintf(buf, cap, tr("Dos armas: un golpe con cada una, +%d%% de daño, +%d%% de puntería"), CALC(SC_CALC1),
                 skill_tohit(s, lvl));
        break;
    case SK_DOUBLE_THROW:
        snprintf(buf, cap, tr("Lanza las dos armas: +%d%% de daño, +%d%% de puntería"), CALC(SC_CALC1),
                 skill_tohit(s, lvl));
        break;
    case SK_FRENZY:
        snprintf(buf, cap, tr("Dos armas: +%d%% de daño; cada acierto, hasta +%d%% de velocidad y +%d%% de ataque"),
                 CALC(SC_CALC1), CALC(SC_AURASTAT1), CALC(SC_AURASTAT2));
        break;
    case SK_WHIRLWIND:
        snprintf(buf, cap, tr("Gira pegándole a todos: +%d%% de daño, +%d%% de puntería"), CALC(SC_CALC1),
                 skill_tohit(s, lvl));
        break;
    case SK_BERSERK:
        snprintf(buf, cap, tr("+%d%% de daño mágico, +%d%% de puntería; defensa 0 por %.1f s"), CALC(SC_CALC1),
                 skill_tohit(s, lvl), CALC(SC_CALC2) / 25.0f);
        break;
    case SK_BATTLE_ORDERS:
        snprintf(buf, cap, tr("+%d%% de vida, maná y estamina al grupo por %d s"), CALC(SC_AURASTAT1),
                 CALC(SC_AURALEN) / 25);
        break;
    case SK_BATTLE_COMMAND:
        snprintf(buf, cap, tr("+%d a todas las habilidades del grupo por %d s"), CALC(SC_AURASTAT1), CALC(SC_AURALEN) / 25);
        break;
    case SK_GRIM_WARD:
        snprintf(buf, cap, tr("Tótem en un cadáver: espanta a %d subtiles por %d s"), CALC(SC_CALC2), CALC(SC_CALC1) / 25);
        break;
    case SK_WAR_CRY:
        skill_phys_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%d-%d de daño alrededor, aturde %.1f s"), lo, hi, CALC(SC_CALC4) / 25.0f);
        break;
    case SK_JAB:
        snprintf(buf, cap, tr("Golpes seguidos con %+d%% de daño, +%d%% de puntería"), CALC(SC_CALC1), skill_tohit(s, lvl));
        break;
    case SK_IMPALE:
        snprintf(buf, cap, tr("+%d%% de daño, frena al monstruo; ataque lento"), CALC(SC_CALC1));
        break;
    case SK_FEND:
        snprintf(buf, cap, tr("Le pega a todos los de alrededor: +%d%% de daño, +%d%% de puntería"), CALC(SC_CALC2),
                 skill_tohit(s, lvl));
        break;
    case SK_POWER_STRIKE:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Agrega %d-%d de daño de rayo, +%d%% de puntería"), lo, hi, skill_tohit(s, lvl));
        break;
    case SK_CHARGED_STRIKE:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Suelta %d rayos cargados de %d-%d de daño de rayo"), CALC(SC_CALC1), lo, hi);
        break;
    case SK_LIGHTNING_STRIKE:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Rayo en cadena a %d monstruos: %d-%d de daño de rayo"), CALC(SC_CALC2), lo, hi);
        break;
    case SK_INNER_SIGHT:
        skill_elem(s, lvl, &lo, &hi);
        snprintf(buf, cap, tr("-%d de defensa a los monstruos por %d s"), lo, CALC(SC_AURALEN) / 25);
        break;
    case SK_SLOW_MISSILES:
        snprintf(buf, cap, tr("Los proyectiles de los monstruos van al %d%% por %d s"), CALC(SC_AURASTAT1),
                 CALC(SC_AURALEN) / 25);
        break;
    case SK_DECOY:
        snprintf(buf, cap, tr("Una copia que distrae a los monstruos por %d s"), CALC(SC_CALC2) / 25);
        break;
    case SK_VALKYRIE:
        snprintf(buf, cap, tr("Invoca una valquiria (+%d%% de vida, +%d%% de puntería)"), CALC(SC_CALC1),
                 CALC(SC_CALC2));
        break;
    case SK_CRITICAL_STRIKE:
        snprintf(buf, cap, tr("%d%% de probabilidad de daño doble"), CALC(SC_PASSIVE1));
        break;
    case SK_DODGE:
        snprintf(buf, cap, tr("%d%% de esquivar un golpe quieto"), CALC(SC_PASSIVE1));
        break;
    case SK_AVOID:
        snprintf(buf, cap, tr("%d%% de esquivar un proyectil quieto"), CALC(SC_PASSIVE1));
        break;
    case SK_EVADE:
        snprintf(buf, cap, tr("%d%% de esquivar golpes y proyectiles al moverse"), CALC(SC_PASSIVE1));
        break;
    case SK_PENETRATE:
        snprintf(buf, cap, tr("+%d%% de puntería"), CALC(SC_PASSIVE1));
        break;
    case SK_PIERCE:
        snprintf(buf, cap, tr("%d%% de que el proyectil atraviese al monstruo"), CALC(SC_PASSIVE1));
        break;
    case SK_MAGIC_ARROW:
        skill_phys(s, lvl, &lo, &hi);
        snprintf(buf, cap, tr("Flecha mágica: +%d de daño, +%d%% de puntería, no gasta flechas"), lo, skill_tohit(s, lvl));
        break;
    case SK_FIRE_ARROW:
    case SK_EXPLODING_ARROW:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%s %d-%d de daño de fuego, +%d%% de puntería"),
                 s->kind == SK_EXPLODING_ARROW ? tr("Explota con") : tr("Agrega"), lo, hi, skill_tohit(s, lvl));
        break;
    case SK_IMMOLATION_ARROW:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Explota con %d-%d de daño de fuego y deja el piso ardiendo"), lo, hi);
        break;
    case SK_COLD_ARROW:
    case SK_ICE_ARROW:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Agrega %d-%d de daño de frío, %s %.1f s"), lo, hi,
                 s->kind == SK_ICE_ARROW ? tr("congela") : tr("enfría"), skill_elen_syn(s, lvl, c) / 25.0f);
        break;
    case SK_FREEZING_ARROW:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Explota con %d-%d de daño de frío y congela %.1f s"), lo, hi,
                 skill_elen_syn(s, lvl, c) / 25.0f);
        break;
    case SK_MULTIPLE_SHOT:
        snprintf(buf, cap, tr("Dispara %d flechas con el 75%% del daño"), skill_missiles(s, lvl));
        break;
    case SK_STRAFE:
        snprintf(buf, cap, tr("Una flecha a cada uno de %d monstruos, +%d%% de daño"), CALC(SC_CALC1), CALC(SC_CALC2));
        break;
    case SK_GUIDED_ARROW:
        snprintf(buf, cap, tr("Busca al monstruo y no falla, +%d%% de daño"), CALC(SC_CALC1));
        break;
    case SK_POISON_JAVELIN:
    case SK_PLAGUE_JAVELIN:
        skill_poison_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%d-%d de daño de veneno en %d s"), lo, hi, skill_elen(s, lvl) / 25);
        break;
    case SK_LIGHTNING_BOLT:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Rayo que atraviesa: %d-%d de daño de rayo"), lo, hi);
        break;
    case SK_LIGHTNING_FURY:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Al chocar suelta %d rayos de %d-%d de daño de rayo"), CALC(SC_CALC1), lo, hi);
        break;
    /* Hechicera */
    case SK_FIRE_BOLT:
    case SK_FIRE_BALL:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%s%d-%d de daño de fuego"), s->kind == SK_FIRE_BALL ? tr("Explota con ") : "", lo, hi);
        break;
    case SK_ICE_BOLT:
    case SK_ICE_BLAST:
    case SK_FROST_NOVA:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%s%d-%d de daño de frío, %s %.1f s"), s->kind == SK_FROST_NOVA ? tr("Nova de ") : "", lo, hi,
                 s->kind == SK_ICE_BLAST ? tr("congela") : tr("enfría"), skill_elen_syn(s, lvl, c) / 25.0f);
        break;
    case SK_GLACIAL_SPIKE:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Explota con %d-%d de daño de frío y congela %.1f s"), lo, hi, CALC(SC_AURALEN) / 25.0f);
        break;
    case SK_CHARGED_BOLT:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%d chispas de %d-%d de daño de rayo"), CALC(SC_CALC1), lo, hi);
        break;
    case SK_LIGHTNING:
    case SK_NOVA:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%s%d-%d de daño de rayo"), s->kind == SK_NOVA ? tr("Nova de ") : tr("Rayo que atraviesa: "), lo, hi);
        break;
    case SK_CHAIN_LIGHTNING:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%d-%d de daño de rayo, salta %d veces"), lo, hi, CALC(SC_CALC1));
        break;
    case SK_FROZEN_ORB:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Escupe descargas de %d-%d de daño de frío"), lo, hi);
        break;
    case SK_STATIC_FIELD:
        snprintf(buf, cap, tr("Le saca el %d%% de la vida a los de alrededor (radio %d)"), CALC(SC_CALC1), CALC(SC_AURARANGE));
        break;
    case SK_TELEKINESIS:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%d-%d de daño de rayo; a veces empuja y aturde"), lo, hi);
        break;
    case SK_TELEPORT:
        snprintf(buf, cap, "%s", tr("Salta hacia donde mira"));
        break;
    case SK_FIRE_WALL:
    case SK_INFERNO:
    case SK_BLAZE:
        skill_elem_rate(s, lvl, c, 25, &lo, &hi);
        if (s->kind == SK_BLAZE)
            snprintf(buf, cap, tr("Deja fuego por donde camina: %d-%d por segundo, %d s"), lo, hi, CALC(SC_AURALEN) / 25);
        else
            snprintf(buf, cap, tr("%s: %d-%d de daño de fuego por segundo"), s->kind == SK_INFERNO ? tr("Llamas") : tr("Muro"), lo, hi);
        break;
    case SK_METEOR:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Cae a los 2,4 s: %d-%d de daño de fuego y deja fuego"), lo, hi);
        break;
    case SK_BLIZZARD:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Trozos de hielo de %d-%d de daño de frío"), lo, hi);
        break;
    case SK_HYDRA:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Tres cabezas por %d s que escupen %d-%d de fuego"),
                 (s->param[1] + (lvl - 1) * s->param[2]) / 25, lo, hi);
        break;
    case SK_FROZEN_ARMOR:
        snprintf(buf, cap, tr("+%d%% de defensa por %d s; congela %.1f s al que le pega"), CALC(SC_AURASTAT1),
                 CALC(SC_AURALEN) / 25, CALC(SC_CALC1) / 25.0f);
        break;
    case SK_SHIVER_ARMOR:
    case SK_CHILLING_ARMOR:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("+%d%% de defensa por %d s; %d-%d de frío al que %s"), CALC(SC_AURASTAT1), CALC(SC_AURALEN) / 25,
                 lo, hi, s->kind == SK_SHIVER_ARMOR ? tr("le pega") : tr("le dispara"));
        break;
    case SK_ENERGY_SHIELD:
        snprintf(buf, cap, tr("El maná absorbe el %d%% del daño por %d s"), CALC(SC_CALC1), CALC(SC_AURALEN) / 25);
        break;
    case SK_THUNDER_STORM:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Un rayo de %d-%d cada 1,6 s por %d s"), lo, hi, CALC(SC_AURALEN) / 25);
        break;
    case SK_ENCHANT:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("+%d-%d de fuego en los golpes por %d s"), lo, hi, CALC(SC_AURALEN) / 25);
        break;
    case SK_WARMTH:
        snprintf(buf, cap, tr("+%d%% de recuperación de maná"), CALC(SC_PASSIVE1));
        break;
    case SK_FIRE_MASTERY:
    case SK_LIGHTNING_MASTERY:
        snprintf(buf, cap, tr("+%d%% de daño de %s"), CALC(SC_PASSIVE1), s->kind == SK_FIRE_MASTERY ? tr("fuego") : tr("rayo"));
        break;
    case SK_COLD_MASTERY:
        snprintf(buf, cap, tr("-%d%% a la resistencia al frío de los monstruos"), CALC(SC_PASSIVE1));
        break;
    /* Paladín */
    case SK_SACRIFICE:
        snprintf(buf, cap, tr("+%d%% de daño, +%d%% de puntería; te lastima el %d%% del daño"), CALC(SC_CALC1),
                 skill_tohit(s, lvl), CALC(SC_CALC2));
        break;
    case SK_SMITE:
        snprintf(buf, cap, tr("Golpe con el escudo que no falla: +%d%% de daño, aturde %.1f s"), CALC(SC_CALC1),
                 CALC(SC_CALC2) / 25.0f);
        break;
    case SK_HOLY_BOLT:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%d-%d de daño mágico a no muertos y demonios; cura %d-%d a los aliados"), lo, hi,
                 CALC(SC_CALC1), CALC(SC_CALC2));
        break;
    case SK_ZEAL:
        snprintf(buf, cap, tr("%d golpes seguidos: +%d%% de daño, +%d%% de puntería"), CALC(SC_CALC1), CALC(SC_CALC2),
                 skill_tohit(s, lvl));
        break;
    case SK_CHARGE:
        snprintf(buf, cap, tr("Corre hasta el monstruo: +%d%% de daño, +%d%% de puntería"), CALC(SC_CALC1),
                 skill_tohit(s, lvl));
        break;
    case SK_VENGEANCE:
        snprintf(buf, cap, tr("Del daño del arma: +%d%% de fuego, +%d%% de frío, +%d%% de rayo"), CALC(SC_CALC1), CALC(SC_CALC2),
                 CALC(SC_CALC3));
        break;
    case SK_BLESSED_HAMMER:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Martillo en espiral: %d-%d de daño mágico"), lo, hi);
        break;
    case SK_CONVERSION:
        snprintf(buf, cap, tr("%d%% de que el monstruo pelee de tu lado %d s"), CALC(SC_CALC1), CALC(SC_AURALEN) / 25);
        break;
    case SK_HOLY_SHIELD:
        skill_phys(s, lvl, &lo, &hi);
        snprintf(buf, cap, tr("+%d%% de defensa, +%d%% de bloqueo, Escarmiento +%d-%d, por %d s"), CALC(SC_CALC1),
                 CALC(SC_AURASTAT1), lo, hi, CALC(SC_AURALEN) / 25);
        break;
    case SK_FIST_HEAVENS:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Rayo de %d-%d y hasta %d rayos sagrados"), lo, hi, CALC(SC_CALC4));
        break;
    case SK_MIGHT:
    case SK_CONCENTRATION:
        snprintf(buf, cap, tr("Aura: +%d%% de daño"), CALC(SC_AURASTAT1));
        break;
    case SK_HOLY_FIRE:
    case SK_HOLY_FREEZE:
    case SK_HOLY_SHOCK:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Aura: %d-%d de %s a los de alrededor; +%d-%d en tus golpes"), lo, hi,
                 s->kind == SK_HOLY_FIRE ? tr("fuego") : s->kind == SK_HOLY_FREEZE ? tr("frío (y los frena)") : tr("rayo"),
                 CALC(SC_PASSIVE1), CALC(SC_PASSIVE2));
        break;
    case SK_THORNS:
        snprintf(buf, cap, tr("Aura: el que te pega recibe el %d%% del daño"), CALC(SC_AURASTAT2));
        break;
    case SK_BLESSED_AIM:
        snprintf(buf, cap, tr("Aura: +%d%% de puntería"), CALC(SC_AURASTAT1));
        break;
    case SK_SANCTUARY:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Aura: %d-%d de daño mágico a los no muertos y los empuja"), lo, hi);
        break;
    case SK_FANATICISM:
        snprintf(buf, cap, tr("Aura: +%d%% de velocidad de ataque, +%d%% de puntería, +%d%% de daño"), CALC(SC_AURASTAT1),
                 CALC(SC_AURASTAT2), CALC(SC_PASSIVE1));
        break;
    case SK_CONVICTION:
        snprintf(buf, cap, tr("Aura: %d%% de defensa y -%d%% de resistencias a los monstruos"), CALC(SC_AURASTAT1),
                 -CALC(SC_AURASTAT2));
        break;
    case SK_PRAYER:
        snprintf(buf, cap, tr("Aura: cura %d de vida cada 2 s"), CALC(SC_AURASTAT1) >> 8);
        break;
    case SK_RESIST_FIRE:
    case SK_RESIST_COLD:
    case SK_RESIST_LIGHTNING:
        snprintf(buf, cap, tr("Aura: +%d%% de resistencia al %s"), CALC(SC_AURASTAT1),
                 s->kind == SK_RESIST_FIRE ? tr("fuego") : s->kind == SK_RESIST_COLD ? tr("frío") : tr("rayo"));
        break;
    case SK_DEFIANCE:
        snprintf(buf, cap, tr("Aura: +%d%% de defensa"), CALC(SC_AURASTAT1));
        break;
    case SK_CLEANSING:
        snprintf(buf, cap, tr("Aura: el veneno y las maldiciones duran el %d%%"), CALC(SC_AURASTAT1));
        break;
    case SK_VIGOR:
        snprintf(buf, cap, tr("Aura: +%d%% de velocidad, +%d%% de estamina"), CALC(SC_AURASTAT3), CALC(SC_AURASTAT2));
        break;
    case SK_MEDITATION:
        snprintf(buf, cap, tr("Aura: +%d%% de recuperación de maná"), CALC(SC_AURASTAT1));
        break;
    case SK_REDEMPTION:
        snprintf(buf, cap, tr("Aura: %d%% de llevarse un cadáver por %d de vida y maná"), CALC(SC_CALC1), CALC(SC_CALC2));
        break;
    case SK_SALVATION:
        snprintf(buf, cap, tr("Aura: +%d%% a todas las resistencias"), CALC(SC_AURASTAT1));
        break;
    /* Nigromante: las maldiciones */
    case SK_AMPLIFY_DAMAGE:
        snprintf(buf, cap, tr("Reciben +%d%% de daño físico"), -CALC(SC_AURASTAT1));
        break;
    case SK_DIM_VISION:
        snprintf(buf, cap, "%s", tr("Solo ven al que tienen al lado (los jefes no)"));
        break;
    case SK_WEAKEN:
        snprintf(buf, cap, tr("Pegan %d%% menos"), -CALC(SC_AURASTAT1));
        break;
    case SK_IRON_MAIDEN:
        snprintf(buf, cap, tr("Se les vuelve el %d%% del daño cuerpo a cuerpo que hacen"), CALC(SC_CALC1));
        break;
    case SK_TERROR:
        snprintf(buf, cap, "%s", tr("Huyen (los jefes no)"));
        break;
    case SK_CONFUSE:
        snprintf(buf, cap, "%s", tr("Se atacan entre ellos (los jefes no)"));
        break;
    case SK_LIFE_TAP:
        snprintf(buf, cap, tr("El que les pega cuerpo a cuerpo se cura el %d%% del daño"), CALC(SC_CALC1));
        break;
    case SK_ATTRACT:
        snprintf(buf, cap, "%s", tr("Los de alrededor atacan al maldito"));
        break;
    case SK_DECREPIFY:
        snprintf(buf, cap, tr("%d%% de velocidad, de daño y de resistencia física"), CALC(SC_AURASTAT1));
        break;
    case SK_LOWER_RESIST:
        snprintf(buf, cap, tr("-%d%% a las resistencias (a los inmunes, un quinto)"), -CALC(SC_AURASTAT1));
        break;
    /* Huesos y veneno */
    case SK_TEETH:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%d dientes de %d-%d de daño mágico"), CALC(SC_CALC1) < 1 ? 1 : CALC(SC_CALC1), lo, hi);
        break;
    case SK_BONE_ARMOR:
        snprintf(buf, cap, tr("Absorbe %d de daño cuerpo a cuerpo"), CALC(SC_AURASTAT1) / 256);
        break;
    case SK_POISON_DAGGER:
    case SK_POISON_EXPLOSION:
    case SK_POISON_NOVA:
        skill_poison_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%s%d-%d de daño de veneno en %d s"), s->kind == SK_POISON_DAGGER ? tr("Con daga: ") : "", lo, hi,
                 skill_elen(s, lvl) / 25);
        break;
    case SK_CORPSE_EXPLOSION:
        snprintf(buf, cap, tr("Del %d al %d%% de la vida del cadáver, mitad fuego (radio %d)"), CALC(SC_CALC1),
                 CALC(SC_CALC2), (CALC(SC_AURARANGE) + 1) / 2);
        break;
    case SK_BONE_WALL:
    case SK_BONE_PRISON:
        snprintf(buf, cap, tr("Huesos con +%d%% de vida que tapan el paso, %d s"), CALC(SC_CALC1), s->param[2] / 25);
        break;
    case SK_BONE_SPEAR:
    case SK_BONE_SPIRIT:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%d-%d de daño mágico; %s"), lo, hi,
                 s->kind == SK_BONE_SPEAR ? tr("atraviesa") : tr("busca al monstruo"));
        break;
    /* Invocaciones */
    case SK_SKELETON_MASTERY:
        snprintf(buf, cap, tr("Esqueletos: +%d de vida y +%d de daño; revividos: +%d%% de vida y +%d%% de daño"),
                 lvl * s->param[1], lvl * s->param[2], lvl * s->param[3], lvl * s->param[4]);
        break;
    case SK_RAISE_SKELETON:
    case SK_RAISE_MAGE:
    case SK_REVIVE: {
        int most = s->petmax[0] ? skill_eval(s, s->petmax, lvl, c) : 1;
        if (s->kind == SK_REVIVE)
            snprintf(buf, cap, tr("Hasta %d por %d s, con +%d%% de vida (no los jefes)"), most, CALC(SC_CALC2) / 25,
                     CALC(SC_CALC1));
        else
            snprintf(buf, cap, tr("Hasta %d (de un cadáver), con +%d%% de vida"), most, CALC(SC_CALC1));
        break;
    }
    case SK_CLAY_GOLEM:
        snprintf(buf, cap, tr("+%d%% de vida; frena %d%% al que golpea"), CALC(SC_CALC1), CALC(SC_AURASTAT1));
        break;
    case SK_BLOOD_GOLEM:
        snprintf(buf, cap, tr("+%d%% de vida; roba el %d%% del daño y te pasa el %d%%"), CALC(SC_CALC1), CALC(SC_CALC2),
                 CALC(SC_CALC3));
        break;
    case SK_IRON_GOLEM:
        snprintf(buf, cap, tr("De un arma o armadura del piso; devuelve %d de daño"), CALC(SC_AURASTAT1));
        break;
    case SK_FIRE_GOLEM:
        skill_elem(s, lvl, &lo, &hi);
        snprintf(buf, cap, tr("%d-%d de fuego en sus golpes y Fuego sagrado de nivel %d"), lo, hi,
                 s->sumskcalc[0] ? skill_eval(s, s->sumskcalc, lvl, c) : lvl);
        break;
    case SK_GOLEM_MASTERY:
        snprintf(buf, cap, tr("Gólems: +%d%% de vida, +%d%% de velocidad y +%d de puntería"), skill_ln(s, 1, 2, lvl),
                 skill_dm(s, 3, 4, lvl), skill_ln(s, 5, 6, lvl));
        break;
    case SK_SUMMON_RESIST:
        snprintf(buf, cap, tr("Esbirros: +%d%% a las resistencias"), CALC(SC_PASSIVE1));
        break;
    /* Asesina: las de carga (cada golpe suma una, hasta 3; un golpe final las libera) */
    case SK_TIGER_STRIKE:
        snprintf(buf, cap, tr("Carga: +%d%% de daño por carga al golpe final"), CALC(SC_CALC1));
        break;
    case SK_COBRA_STRIKE:
        snprintf(buf, cap, tr("Carga: roba el %d%% en vida (con 2, también maná; con 3, el doble)"),
                 s->param[1] + (lvl - 1) * s->param[2]);
        break;
    case SK_FISTS_OF_FIRE:
    case SK_CLAWS_THUNDER:
    case SK_BLADES_ICE:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Carga: %d-%d de %s; con 2, %s; con 3, %s"), lo, hi,
                 s->kind == SK_FISTS_OF_FIRE ? tr("fuego") : s->kind == SK_CLAWS_THUNDER ? tr("rayo") : tr("frío"),
                 s->kind == SK_CLAWS_THUNDER ? tr("una nova") : tr("una explosión"),
                 s->kind == SK_FISTS_OF_FIRE ? tr("fuego en el piso") : s->kind == SK_CLAWS_THUNDER ? tr("centellas") : tr("congela"));
        break;
    case SK_ROYAL_STRIKE:
        snprintf(buf, cap, "%s", tr("Carga: con 1, un meteoro; con 2, rayos en ronda; con 3, hielo alrededor"));
        break;
    /* Druida: invocaciones */
    case SK_RAVEN: {
        int most = s->petmax[0] ? skill_eval(s, s->petmax, lvl, c) : 1;
        skill_phys_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Hasta %d cuervo%s de %d-%d de daño que pegan %d veces"), most, most > 1 ? "s" : "", lo, hi,
                 skill_ln(s, 5, 6, lvl));
        break;
    }
    case SK_POISON_CREEPER:
        skill_poison_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Una liana que envenena: %d-%d en %d s; +%d%% de vida"), lo, hi, skill_elen(s, lvl) / 25,
                 CALC(SC_CALC1));
        break;
    case SK_CARRION_VINE:
    case SK_SOLAR_CREEPER:
        snprintf(buf, cap, tr("Come cadáveres: cada uno te devuelve el %d%% de%s"), skill_ln(s, 5, 6, lvl),
                 s->kind == SK_CARRION_VINE ? tr(" la vida") : tr("l maná"));
        break;
    case SK_SPIRIT_WOLF:
    case SK_DIRE_WOLF:
    case SK_GRIZZLY: {
        int most = s->petmax[0] ? skill_eval(s, s->petmax, lvl, c) : 1;
        if (s->kind == SK_SPIRIT_WOLF) {
            skill_elem(s, lvl, &lo, &hi);
            snprintf(buf, cap, tr("Hasta %d lobo%s con %d-%d de frío, +%d de puntería y +%d%% de vida"), most,
                     most > 1 ? "s" : "", lo, hi, CALC(SC_PASSIVE2), CALC(SC_CALC1));
        } else {
            int pct = CALC(SC_PASSIVE3); /* damagepercent (el del oso pardo) */
            skill_phys_syn(s, lvl, c, &lo, &hi);
            lo = lo * (100 + pct) / 100;
            hi = hi * (100 + pct) / 100;
            if (s->kind == SK_GRIZZLY)
                snprintf(buf, cap, tr("Un oso con %d-%d de daño y +%d%% de vida"), lo, hi, CALC(SC_CALC1));
            else
                snprintf(buf, cap, tr("Hasta %d lobo%s con %d-%d de daño y +%d%% de vida"), most, most > 1 ? "s" : "", lo, hi,
                         CALC(SC_CALC1));
        }
        break;
    }
    case SK_OAK_SAGE:
        snprintf(buf, cap, tr("Un espíritu que da +%d%% de vida a los de alrededor"), skill_ln(s, 1, 2, lvl));
        break;
    case SK_HEART_WOLVERINE:
        snprintf(buf, cap, tr("Un espíritu que da +%d%% de daño y +%d%% de puntería"), skill_ln(s, 5, 6, lvl),
                 skill_ln(s, 3, 4, lvl));
        break;
    case SK_SPIRIT_BARBS:
        skill_phys_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Un espíritu: el que te pega recibe %d de daño"), lo);
        break;
    /* Formas */
    case SK_WEREWOLF:
        snprintf(buf, cap, tr("Lobo por %d s: +%d%% de velocidad de ataque, +%d%% de puntería, +%d%% de vida"),
                 CALC(SC_AURALEN) / 25, CALC(SC_AURASTAT2), CALC(SC_AURASTAT3), CALC(SC_AURASTAT4));
        break;
    case SK_WEREBEAR:
        snprintf(buf, cap, tr("Oso por %d s: +%d%% de daño, +%d%% de defensa, +%d%% de vida"), CALC(SC_AURALEN) / 25,
                 CALC(SC_AURASTAT1), CALC(SC_AURASTAT2), CALC(SC_AURASTAT3));
        break;
    case SK_LYCANTHROPY:
        snprintf(buf, cap, tr("Transformado: +%d%% de vida y %d s más"), skill_ln(s, 3, 4, lvl), skill_ln(s, 1, 2, lvl) / 25);
        break;
    case SK_FERAL_RAGE:
        snprintf(buf, cap, tr("+%d%% de daño; cada golpe suma una carga (hasta %d): más velocidad y robo de vida"),
                 CALC(SC_CALC1), CALC(SC_CALC2));
        break;
    case SK_MAUL:
        snprintf(buf, cap, tr("Cada golpe suma una carga (hasta %d): +%d%% de daño cada una, y aturde"), CALC(SC_CALC2),
                 s->param[3]);
        break;
    case SK_RABIES:
        skill_poison_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Un mordisco que envenena (%d-%d en %d s) y contagia"), lo, hi, skill_elen(s, lvl) / 25);
        break;
    case SK_FIRE_CLAWS:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("+%d-%d de fuego en el golpe"), lo, hi);
        break;
    case SK_HUNGER:
        snprintf(buf, cap, tr("Un mordisco que roba %d%% de vida y %d%% de maná (%d%% de daño)"), CALC(SC_CALC2),
                 CALC(SC_CALC3), CALC(SC_CALC1));
        break;
    case SK_FURY:
        snprintf(buf, cap, tr("%d golpes seguidos con +%d%% de daño"), CALC(SC_CALC1), CALC(SC_CALC2));
        break;
    case SK_SHOCK_WAVE:
        skill_phys_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Una onda de %d-%d de daño que aturde %.1f s"), lo, hi, CALC(SC_CALC4) / 25.0f);
        break;
    /* Elementales */
    case SK_FIRESTORM:
        skill_elem_rate(s, lvl, c, 25, &lo, &hi);
        snprintf(buf, cap, tr("%d llamas que dejan fuego: %d-%d por segundo"), CALC(SC_CALC1), lo, hi);
        break;
    case SK_MOLTEN_BOULDER:
    case SK_VOLCANO:
    case SK_ARMAGEDDON: {
        int plo, phi;
        skill_phys_syn(s, lvl, c, &plo, &phi);
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, s->kind == SK_MOLTEN_BOULDER ? tr("Rueda y empuja: %d-%d de daño y %d-%d de fuego")
                           : s->kind == SK_VOLCANO     ? tr("Escupe rocas de %d-%d de daño y %d-%d de fuego")
                                                       : tr("Caen rocas alrededor: %d-%d de daño y %d-%d de fuego"),
                 plo, phi, lo, hi);
        break;
    }
    case SK_FISSURE:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Grietas de fuego: %d-%d de daño"), lo, hi);
        break;
    case SK_ARCTIC_BLAST:
        skill_elem_rate(s, lvl, c, 12, &lo, &hi);
        snprintf(buf, cap, tr("Un chorro de frío: %d-%d por segundo (alcance %d)"), lo, hi, CALC(SC_AURARANGE));
        break;
    case SK_CYCLONE_ARMOR:
        snprintf(buf, cap, tr("Absorbe %d de daño de fuego, frío y rayo"), CALC(SC_AURASTAT1) / 256);
        break;
    case SK_TWISTER:
        skill_phys_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%d remolinos de %d-%d de daño que aturden %.1f s"), CALC(SC_CALC1), lo, hi,
                 CALC(SC_CALC2) / 25.0f);
        break;
    case SK_TORNADO:
        skill_phys_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Un tornado de %d-%d de daño"), lo, hi);
        break;
    case SK_HURRICANE:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Frío alrededor por %d s: %d-%d"), CALC(SC_AURALEN) / 25, lo, hi);
        break;
    /* El Brujo (los números, como los calcula skillwar.inc) */
    case SK_SUMMON_GOATMAN:
    case SK_SUMMON_TAINTED:
    case SK_SUMMON_DEFILER: {
        int most = s->petmax[0] ? skill_eval(s, s->petmax, lvl, c) : 1;
        const char *what = s->kind == SK_SUMMON_GOATMAN ? tr("Un hombre cabra que pega cuerpo a cuerpo")
                         : s->kind == SK_SUMMON_TAINTED ? tr("Un mancillado que tira bolas de fuego")
                                                        : tr("Un profanador que une las almas de los enemigos");
        snprintf(buf, cap, tr("%s; +%d%% de vida (hasta %d demonio%s)"), what, CALC(SC_CALC1), most, most > 1 ? "s" : "");
        break;
    }
    case SK_DEMONIC_MASTERY:
        snprintf(buf, cap, tr("Tus demonios: +%d%% de velocidad, +%d de puntería, +%d%% de daño; hasta %d"),
                 skill_dm(s, 9, 10, lvl), skill_ln(s, 5, 6, lvl), skill_ln(s, 11, 12, lvl),
                 lvl >= 10 ? 3 : lvl >= 5 ? 2 : 1);
        break;
    case SK_DEATH_MARK:
        snprintf(buf, cap, tr("Tu demonio salta al enemigo y le baja %d de defensa por %d s"), -skill_ln(s, 5, 6, lvl),
                 skill_ln(s, 3, 4, lvl) / 25);
        break;
    case SK_BLOOD_OATH:
        snprintf(buf, cap, tr("El %d%% del daño que recibís va a tu demonio; él: +%d%% de resistencias"),
                 skill_dm(s, 1, 2, lvl), skill_dm(s, 7, 8, lvl));
        break;
    case SK_ENGORGE:
        snprintf(buf, cap, tr("Tu demonio come un cadáver y se cura el %d%% de la vida"), skill_ln(s, 3, 4, lvl));
        break;
    case SK_BLOOD_BOIL: {
        int flo, fhi;
        skill_phys_syn(s, lvl, c, &lo, &hi);
        skill_elem_syn(s, lvl, c, &flo, &fhi);
        snprintf(buf, cap, tr("Tus demonios estallan: %d-%d de daño y %d-%d de fuego"), lo, hi, flo, fhi);
        break;
    }
    case SK_CONSUME:
        snprintf(buf, cap, tr("Sacrificás un demonio: +%d%% de vida máxima y +%d%% de velocidad por %d s"),
                 skill_ln(s, 3, 4, lvl), skill_dm(s, 5, 6, lvl), skill_ln(s, 1, 2, lvl) / 25);
        break;
    case SK_BIND_DEMON:
        snprintf(buf, cap, tr("%d%% de quedarte con un demonio herido (+%d%% de vida y de daño)"), skill_dm(s, 3, 4, lvl),
                 skill_ln(s, 9, 10, lvl));
        break;
    case SK_LEVITATE:
        snprintf(buf, cap, tr("Con un arma: +%d%% de daño, +%d%% de puntería y %d%% de golpe crítico"),
                 skill_ln(s, 3, 4, lvl), skill_ln(s, 5, 6, lvl), skill_dm(s, 1, 2, lvl));
        break;
    case SK_ELDRITCH_BLAST:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Por %d s: %d-%d mágico a los embrujados de alrededor; roba %d%%"), s->param[3] / 25, lo, hi,
                 skill_ln(s, 1, 2, lvl));
        break;
    case SK_HEX_BANE:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("El arma: +%d-%d mágico; a los que golpea, -%d%% de defensa"), lo, hi,
                 skill_ln(s, 5, 6, lvl) < 50 ? skill_ln(s, 5, 6, lvl) : 50);
        break;
    case SK_HEX_SIPHON:
        snprintf(buf, cap, tr("El arma: +%d de vida y +%d de maná por muerte; los golpeados hacen -33%% de daño"),
                 skill_ln(s, 1, 2, lvl), skill_ln(s, 5, 6, lvl));
        break;
    case SK_HEX_PURGE:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("El arma: +%d%% de velocidad; los golpeados pueden estallar (%d-%d mágico)"),
                 skill_ln(s, 5, 6, lvl) < 30 ? skill_ln(s, 5, 6, lvl) : 30, lo, hi);
        break;
    case SK_PSYCHIC_WARD:
        snprintf(buf, cap, tr("Absorbe %d de daño; al que te pega de cerca lo aturde"), skill_ln(s, 3, 4, lvl));
        break;
    case SK_ECHOING_STRIKE:
        snprintf(buf, cap, tr("Ecos del arma que atraviesan a todos: +%d%% de daño"), skill_ln(s, 1, 2, lvl));
        break;
    case SK_BLADE_WARP:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Tirás un arma astral y aparecés donde pega: %d-%d mágico"), lo, hi);
        break;
    case SK_CLEAVE:
        snprintf(buf, cap, tr("Golpea a todos en un arco de %d grados: +%d%% de daño"), (lvl >= 20 ? 20 : lvl + 10) * 12,
                 skill_ln(s, 1, 2, lvl));
        break;
    case SK_MIRRORED_BLADES: {
        int n = 2 + lvl / 5;
        snprintf(buf, cap, tr("%d reflejos del arma con el %d%% del daño"), n > 5 ? 5 : n, skill_dm(s, 1, 2, lvl));
        break;
    }
    case SK_SIGIL_LETHARGY:
        snprintf(buf, cap, tr("Un sigilo por %d s: frena a la mitad y baja %d%% la defensa"), (100 + lvl * 25) / 25,
                 -skill_ln(s, 5, 6, lvl));
        break;
    case SK_SIGIL_RANCOR:
        snprintf(buf, cap, tr("Un sigilo por %d s: los de adentro se pelean entre ellos"), (100 + lvl * 25) / 25);
        break;
    case SK_SIGIL_DEATH:
        snprintf(buf, cap, tr("Un sigilo por %d s: los de menos del %d%% de vida mueren y estallan"), (100 + lvl * 25) / 25,
                 skill_ln(s, 1, 2, lvl));
        break;
    case SK_RING_OF_FIRE:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%d llamas alrededor: %d-%d de fuego; empujan"), lvl >= 10 ? 32 : 16, lo, hi);
        break;
    case SK_MIASMA_BOLT:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%d-%d mágico y una nube de miasma"), lo, hi);
        break;
    case SK_MIASMA_CHAINS: {
        int n = 5 + lvl / 4;
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Cadenas hasta %d enemigos: %d-%d mágico y miasma"), n > 10 ? 10 : n, lo, hi);
        break;
    }
    case SK_ENHANCED_ENTROPY:
        snprintf(buf, cap, tr("La miasma: +%d%% de daño, +%d%% de radio y de duración; el Abismo: +%d%%"),
                 skill_ln(s, 1, 2, lvl), skill_ln(s, 3, 4, lvl), skill_ln(s, 7, 8, lvl));
        break;
    case SK_FLAME_WAVE:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Una ola de fuego que avanza: %d-%d y deja el piso ardiendo"), lo, hi);
        break;
    case SK_APOCALYPSE:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Fuego infernal alrededor: %d-%d; -%d%% de resistencia al fuego"), lo, hi,
                 skill_ln(s, 1, 2, lvl) < 40 ? skill_ln(s, 1, 2, lvl) : 40);
        break;
    case SK_ABYSS:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Una grieta que atrae a los enemigos y estalla: %d-%d mágico"), lo, hi);
        break;
    /* Los golpes finales */
    case SK_DRAGON_TALON:
        snprintf(buf, cap, tr("%d patada%s con +%d%% de daño; la última empuja"), CALC(SC_CALC1), CALC(SC_CALC1) > 1 ? "s" : "",
                 s->param[1] + (lvl - 1) * s->param[2]);
        break;
    case SK_DRAGON_CLAW:
        snprintf(buf, cap, tr("Con dos garras: +%d%% de daño"), CALC(SC_CALC1));
        break;
    case SK_DRAGON_TAIL:
        snprintf(buf, cap, tr("Patada con +%d%% de daño que explota en fuego (radio %d)"), CALC(SC_CALC1), CALC(SC_AURARANGE) / 2);
        break;
    case SK_DRAGON_FLIGHT:
        snprintf(buf, cap, tr("Salta al lado del monstruo y patea con +%d%% de daño"), CALC(SC_CALC1));
        break;
    /* Sombras */
    case SK_CLAW_MASTERY:
        snprintf(buf, cap, tr("Con garras: +%d%% de puntería, +%d%% de daño, %d%% de golpe crítico"), CALC(SC_PASSIVE1),
                 CALC(SC_PASSIVE2), CALC(SC_PASSIVE3));
        break;
    case SK_WEAPON_BLOCK:
        snprintf(buf, cap, tr("Con dos garras: %d%% de bloquear"), CALC(SC_PASSIVE1));
        break;
    case SK_PSYCHIC_HAMMER:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("%d-%d de daño mágico y lo empuja"), lo, hi);
        break;
    case SK_QUICKNESS:
        snprintf(buf, cap, tr("+%d%% de velocidad y +%d%% de velocidad de ataque por %d s"), CALC(SC_AURASTAT1),
                 CALC(SC_AURASTAT2), CALC(SC_AURALEN) / 25);
        break;
    case SK_FADE:
        snprintf(buf, cap, tr("+%d%% a las resistencias por %d s"), CALC(SC_AURASTAT1), CALC(SC_AURALEN) / 25);
        break;
    case SK_VENOM:
        skill_poison_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Golpes con %d-%d de veneno, por %d s"), lo, hi, CALC(SC_AURALEN) / 25);
        break;
    case SK_CLOAK_SHADOWS:
        snprintf(buf, cap, tr("Ciega a los de alrededor y les baja %d%% la defensa, %d s"), -CALC(SC_AURASTAT1),
                 CALC(SC_AURALEN) / 25);
        break;
    case SK_MIND_BLAST:
        snprintf(buf, cap, tr("Aturde a los de alrededor; %d%% de convertirlos (no a los jefes)"), CALC(SC_CALC1));
        break;
    case SK_SHADOW_WARRIOR:
    case SK_SHADOW_MASTER:
        snprintf(buf, cap, tr("Un clon oscuro con tu equipo (+%d de puntería)"), CALC(SC_AURASTAT1));
        break;
    /* Trampas */
    case SK_FIRE_TRAUMA:
    case SK_SHOCK_FIELD:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, s->kind == SK_FIRE_TRAUMA ? tr("Una bomba de %d-%d de fuego") : tr("Rayos en el piso: %d-%d por segundo"),
                 lo, hi);
        break;
    case SK_BLADE_SENTINEL:
    case SK_BLADE_FURY:
    case SK_BLADE_SHIELD:
        skill_phys(s, lvl, &lo, &hi);
        snprintf(buf, cap, tr("%d-%d de daño más el %d%% del arma%s"), lo, hi, (s->srcdam > 0 ? s->srcdam : 128) * 100 / 128,
                 s->kind == SK_BLADE_SENTINEL ? tr(", va y viene") : s->kind == SK_BLADE_SHIELD ? tr(", a los de al lado") : "");
        break;
    case SK_CB_SENTRY:
    case SK_LIGHTNING_SENTRY:
    case SK_WAKE_OF_FIRE:
    case SK_INFERNO_SENTRY:
    case SK_DEATH_SENTRY:
        skill_elem_syn(s, lvl, c, &lo, &hi);
        snprintf(buf, cap, tr("Trampa: %d tiros de %d-%d de %s%s"), s->kind == SK_CB_SENTRY ? CALC(SC_CALC4) : s->param[1], lo, hi,
                 s->kind == SK_WAKE_OF_FIRE || s->kind == SK_INFERNO_SENTRY ? tr("fuego") : tr("rayo"),
                 s->kind == SK_DEATH_SENTRY ? tr("; hace explotar cadáveres") : "");
        break;
    default:
        snprintf(buf, cap, "%s", tr("Todavía no está en este port"));
        break;
    }
    if (s->aura) { /* el alcance de las auras */
        size_t len = strlen(buf);
        snprintf(buf + len, cap > len ? cap - len : 0, tr(" (radio %d)"), CALC(SC_AURARANGE));
    }
    if (s->kind >= SK_AMPLIFY_DAMAGE && s->kind <= SK_LOWER_RESIST) { /* las maldiciones: cuánto y dónde */
        size_t len = strlen(buf);
        snprintf(buf + len, cap > len ? cap - len : 0, tr(", %d s (radio %d)"), CALC(SC_AURALEN) / 25, CALC(SC_AURARANGE));
    }
#undef CALC
    return buf;
}
