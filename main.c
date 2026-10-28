/* Copyright (C) 2026 Fedor Melnikov
 * Rascal v1.0
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see <https://www.gnu.org/licenses/>. */

#include <conio.h>
#include <apple2enh.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "level.h"
#include "rng.h"
#include "game_shared.h"

pt_t player;
unsigned char g_depth				= 1;
unsigned char g_stairs_inverted		= 0;
unsigned char g_has_signet			= 0;
unsigned char g_ring_found_depth	= 0; /* depth at which the ring was found, for HoF points */
unsigned char g_max_depth			= 1; /* duplicates the local 'depth' variable in 'main()' - needed by separate functions like 'player_die' */
unsigned char g_level_has_shop		= 0; /* spawn_shop() is called upon successful placement */
unsigned char g_level_has_treasure 	= 0; /* spawn_treasure_room() is called upon successful placement */
unsigned char g_poison_turns 		= 0; /* how many turns the poison lasts? 1d3 damage per turn */
unsigned char g_ring_sleep_active 	= 0; /* recursion protection: calls end_turn() from within end_turn(); not static, taken from rare_ov10.c */
unsigned char g_dex_bonus 			= 0; /* permanent accuracy bonus from the Potion of Dexterity, for both melee and ranged combat */
unsigned char g_dmg_bonus 			= 0; /* permanent damage bonus from the Potion of Damage, for both melee and ranged combat */
unsigned char g_pending_identify 	= 0; /* identification scroll has been read, target selection will be initiated as a separate step from main() */
unsigned char g_player_class 		= 0; /* 0=strongman, 1=trickster, 2=intellectual, select in menu_new_game() */
ground_item_t g_shop_offer[5];
unsigned char g_shop_price[5];
unsigned char g_seen[MAP_H][MAP_W / 8];	/* 1 bit per cell instead of 1 byte - saves ~1.4 KB; see is_seen()/set_seen() */
unsigned char g_map[MAP_H][MAP_W];		/* map itself - the data; the generator is in the overlay */
zone_t g_zones[NUM_ZONES];

/* used in 2 places - cc65 doesn't collapse identical string literals
 * on its own, so a shared constant here actually saves space. */
static const char msg_no_merchant[]  = "No merchant here";

#define GAME_VOLUME "/RASCAL/" /* disk volume name, see ensure_prefix() */
#define FOV_RADIUS 1

monster_t g_monsters[MAX_MONSTERS];
unsigned char g_monster_count;

player_stats_t pstat;
char player_name[16]; /* always overwritten before the first play - either by entering a name
                       * (empty names are not allowed) or by loading a saved game */
static unsigned int g_quiet_turns;					/* consecutive turns without taking damage, for regeneration */
unsigned long xp_threshold(unsigned char level);	/* forward, needed in draw_status */
void player_die(const char *cause);					/* resident, required by try_move/monster_attack/end_turn */
static unsigned char load_start_overlay(void);		/* forward, needed in player_die */
static unsigned char load_end_overlay(void);		/* forward, needed for help/legend/instructions */
static unsigned char load_overlay10_safe(void);		/* forward, needed in player_die */
void handle_player_death(const char *cause);		/* forward (defined in rare_ov10.c), needed in player_die */
void handle_player_win(void);						/* defined in rare_ov10.c */
void show_hall_of_fame(void);						/* defined in rare_ov10.c */
static unsigned char load_spawn_overlay(void);		/* forward, needed in load_spawn_overlay_safe */
void queue_message(const char *msg);				/* forward, needed in apply_potion_effect */
static void ensure_prefix(void);					/* forward, needed in load_overlay_file */

/* items (types and limits in game_shared.h, shared with the menu overlay) */
ground_item_t g_ground_items[MAX_GROUND_ITEMS];
unsigned char g_ground_count;
inv_item_t g_inventory[MAX_INVENTORY];
unsigned char g_inv_count;
/* indices in g_inventory for the character's doll; -1 = slot is empty */
signed char g_eq_armor	= -1;					/* armor */
signed char g_eq_hand_l	= -1, g_eq_hand_r = -1; /* melee weapon/shield */
signed char g_eq_ranged = -1;					/* ranged weapons */
signed char g_eq_ring_l = -1, g_eq_ring_r = -1;	/* rings */

#pragma rodata-name (push, "LC")
static const char st_str0[] = "Scroll: 'ACUBAR'";
static const char st_str1[] = "Scroll: 'DUBALLA'";
static const char st_str2[] = "Scroll: 'FROOMP'";
static const char st_str3[] = "Scroll: 'XYZZY'";
static const char st_str4[] = "Scroll: 'NAGOOB'";
static const char st_str5[] = "Scroll: 'PELGRIN'";
static const char se_str0[] = "Scroll of Mapping";
static const char se_str1[] = "Scroll of Sleep";
static const char se_str2[] = "Scroll of Create Food";
static const char se_str3[] = "Scroll of Aggravate Monster";
static const char se_str4[] = "Scroll of Identify";
static const char se_str5[] = "Scroll of Remove Curse";
#pragma rodata-name (pop)
static const char *const scroll_titles[NUM_SCROLL_KINDS] = {
    st_str0, st_str1, st_str2, st_str3, st_str4, st_str5
};
static const char *const scroll_effect_names[NUM_SCROLL_KINDS] = {
    se_str0, se_str1, se_str2, se_str3, se_str4, se_str5
};

const char *item_name(item_type_t t, unsigned char dice_n, unsigned char dice_s)
{
    switch (t) {
        case ITEM_WEAPON_MELEE:
            /* single dice_s value is not enough to determine the type of weapon - 
			 * some types share the same die (Short Sword/Rapier - d6, Dagger/Sickle -
			 * d4) and differ only in the number of dice (dice_n). */
            if (dice_n == 1) {
                switch (dice_s) {
                    case 3:  return "Knife";
                    case 4:  return "Dagger";
                    case 6:  return "Short Sword";
                    case 7:  return "Warhammer";
                    case 8:  return "Mace";
                    case 9:  return "Short Spear";
                    case 10: return "Long Sword";
                    case 12: return "Pike";
                    default: return "Greatsword"; /* 20 */
                }
            } else {
                switch (dice_s) {
                    case 3:  return "Quarterstaff";
                    case 4:  return "Sickle";
                    case 5:  return "Saber";
                    default: return "Rapier"; /* 6 */
                }
            }
        case ITEM_WEAPON_RANGED:
            switch (dice_s) {
                case 4:  return "Short Bow";
                case 6:  return "Long Bow";
                case 10: return "Composite Bow";
                default: return "Elf Bow"; /* 12 */
            }
        case ITEM_ARMOR:
            /* for armor, dice_s doesn't carry a damage die (item_stat_str doesn't print
			 * it), reused as a kind index instead: 1..7. */
            switch (dice_s) {
                case 1:  return "Robe";
                case 2:  return "Leather Armor";
                case 3:  return "Studded Leather";
                case 4:  return "Scale Mail";
                case 5:  return "Chainmail";
                case 6:  return "Half-Plate";
                default: return "Full Plate"; /* 7 */
            }
        case ITEM_SHIELD:
            /* same as with armor, the shield's dice_s slot is also empty. */
            switch (dice_s) {
                case 1:  return "Buckler";
                case 2:  return "Small Shield";
                case 3:  return "Large Shield";
                default: return "Tower Shield"; /* 4 */
            }
        case ITEM_SCROLL_MAPPING: case ITEM_SCROLL_SLEEP:
        case ITEM_SCROLL_CREATE_FOOD: case ITEM_SCROLL_AGGRAVATE:
        case ITEM_SCROLL_IDENTIFY: case ITEM_SCROLL_REMOVE_CURSE: {
            unsigned char sidx = (unsigned char)(t - ITEM_SCROLL_MAPPING);
            if (!scroll_identified[sidx]) return scroll_titles[scroll_title_for[sidx]];
            return scroll_effect_names[sidx];
        }
        case ITEM_FOOD:           return "Food ration";
        case ITEM_AMMO:           return "Arrows";
        case ITEM_QUEST_RING:     return "Ring of the Apple Signet";
        default:                  return "Gold";
    }
}

static char item_glyph(item_type_t t)
{
    /* the order of the characters must match the item_type_t in game_shared.h:
     * GOLD, WEAPON_MELEE, WEAPON_RANGED, ARMOR, 6x SCROLL, FOOD, POTION,
     * SHIELD, RING, WAND, AMMO, QUEST_RING, TREASURE. */
    return "$)}]~~~~~~%!(=/\\=*"[t];
}

/* potions */
unsigned char potion_effect[NUM_POTION_KINDS];
unsigned char potion_identified[NUM_POTION_KINDS];

#pragma rodata-name (push, "LC")
static const char pc_str0[] = "Red potion"; /* LC */
static const char pc_str1[] = "Blue potion";
static const char pc_str2[] = "Clear potion";
static const char pc_str3[] = "Green potion";
static const char pc_str4[] = "Yellow potion";
static const char pc_str5[] = "Purple potion";
static const char pc_str6[] = "Black potion";
#pragma rodata-name (pop)
static const char pc_str7[] = "Orange potion"; /* new ones, outside the LC (no more space), regular resident */
static const char pc_str8[] = "Pink potion";
static const char *const potion_colors[NUM_POTION_KINDS] = {
    pc_str0, pc_str1, pc_str2, pc_str3, pc_str4, pc_str5, pc_str6, pc_str7, pc_str8
};
static const char *potion_color_name(unsigned char kind)
{
    return potion_colors[kind];
}

#pragma rodata-name (push, "LC")
static const char pe_str0[] = "Potion of Healing"; /* LC */
static const char pe_str1[] = "Potion of Strength";
static const char pe_str2[] = "Potion of Poison";
static const char pe_str3[] = "Potion of Water";
static const char pe_str4[] = "Potion of Digestion";
static const char pe_str5[] = "Potion of Dexterity";
static const char pe_str6[] = "Potion of Damage";
#pragma rodata-name (pop)
static const char pe_str7[] = "Zajecarsko Beer"; /* new ones, outside the LC, regular resident */
static const char pe_str8[] = "Rakija";
static const char *const potion_effect_names[NUM_POTION_KINDS] = {
    pe_str0, pe_str1, pe_str2, pe_str3, pe_str4, pe_str5, pe_str6, pe_str7, pe_str8
};
static const char *potion_effect_name(unsigned char effect)
{
    return potion_effect_names[effect];
}

/* display the color if the potion type has not yet been identified;
 * otherwise, display its actual name. */
const char *potion_display_name(unsigned char kind)
{
    if (kind >= NUM_POTION_KINDS) kind = 0;
    if (potion_identified[kind]) return potion_effect_name(potion_effect[kind]);
    return potion_color_name(kind);
}

/* three types of arrows, visible at a glance. 0=standard,
 * 1=high-quality (+1 to damage), 2=magical (+3 to damage). */
const char *ammo_display_name(unsigned char kind)
{
    static const char *const names[NUM_AMMO_KINDS] = {"Arrows", "Quality Arrows", "Magic Arrows"};
    if (kind >= NUM_AMMO_KINDS) kind = 0;
    return names[kind];
}

/* damage bonus from arrow quality; a single entry to avoid duplication
 * in 'menu_fire()' and in cases where it's used in multiple places. */
signed char ammo_damage_bonus(unsigned char kind)
{
    if (kind == AMMO_QUALITY) return 1;
    if (kind == AMMO_MAGIC) return 3;
    return 0;
}

unsigned char scroll_identified[NUM_SCROLL_KINDS];
unsigned char scroll_title_for[NUM_SCROLL_KINDS]; /* which title was assigned to each scroll kind in this game */

/* general function for all 'shuffle material/effect once per game' operations,
 * called only when creating a character from rare_ov10.c, along with
 * give_starting_gear() and related functions. */
void shuffle_kind(unsigned char *arr, unsigned char *identified, unsigned char n);

/* rings */
unsigned char ring_effect[NUM_RING_KINDS];
unsigned char ring_identified[NUM_RING_KINDS];

#pragma rodata-name (push, "LC")
static const char rm_str0[]  = "Wooden ring";
static const char rm_str1[]  = "Silver ring";
static const char rm_str2[]  = "Golden ring";
static const char rm_str3[]  = "Copper ring";
static const char rm_str4[]  = "Iron ring";
static const char rm_str5[]  = "Ivory ring";
static const char rm_str6[]  = "Jade ring";
static const char rm_str7[]  = "Bone ring";
static const char rm_str8[]  = "Onyx ring";
static const char rm_str9[]  = "Glass ring";
static const char rm_str10[] = "Coral ring";
static const char rm_str11[] = "Tin ring";
#pragma rodata-name (pop)
static const char *const ring_materials[NUM_RING_KINDS] = {
    rm_str0, rm_str1, rm_str2, rm_str3, rm_str4, rm_str5,
    rm_str6, rm_str7, rm_str8, rm_str9, rm_str10, rm_str11
};
static const char *ring_material_name(unsigned char kind)
{
    return ring_materials[kind];
}

#pragma rodata-name (push, "LC")
static const char re_str0[]  = "Ring of Protection";
static const char re_str1[]  = "Ring of Regeneration";
static const char re_str2[]  = "Ring of Strength";
static const char re_str3[]  = "";
static const char re_str4[]  = "Ring of Slow Digestion";
static const char re_str5[]  = "Ring of Searching";
static const char re_str6[]  = "Ring of Dexterity";
static const char re_str7[]  = "Ring of Damage";
static const char re_str8[]  = "Ring of Poison";
static const char re_str9[]  = "Ring of Sleep";
static const char re_str10[] = "Ring of Fast Digestion";
static const char re_str11[] = "Ring of Aggravate Monster";
#pragma rodata-name (pop)
static const char *const ring_effect_names[NUM_RING_KINDS] = {
    re_str0, re_str1, re_str2, re_str3, re_str4, re_str5,
    re_str6, re_str7, re_str8, re_str9, re_str10, re_str11
};
static const char *ring_effect_name(unsigned char effect)
{
    return ring_effect_names[effect];
}

const char *ring_display_name(unsigned char kind)
{
    if (kind >= NUM_RING_KINDS) kind = 0;
    /* ring without an effect does not receive a separate 'magical' name -
     * even when identified, it simply displays the material. */
    if (ring_identified[kind] && ring_effect[kind] != RING_EFFECT_NONE)
        return ring_effect_name(ring_effect[kind]);
    return ring_material_name(kind);
}

/* ring name for lists + CURSED marker - since the curse on the ring does not
 * depend on the effect (it is a separate property of a SPECIFIC instance,
 * mod2==1), it would not fit into ring_display_name() on its own
 * (that function only takes kind, while mod2 is an instance property). force_reveal -
 * ignore type identification (for the shop, where there's always complete
 * information). buf needs at least 34 bytes with some leeway. Resident (LC) -
 * used by many overlays (backpack/drop/sell/remove/equip). */
#pragma code-name (push, "LC")
void ring_name_str(char *buf, unsigned char kind, signed char mod2, unsigned char force_reveal)
{
    strcpy(buf, ring_display_name(kind));
    if (mod2 == 1 && (force_reveal || ring_identified[kind]))
        strcat(buf, " CURSED");
}
#pragma code-name (pop)

/* how many of the two rings being worn have an effect (0/1/2): now in
 * ring_worn_effect_asm.s, the most frequently called function among all those
 * related to rings (13+ call sites, including every combat calculation). */
unsigned char ring_worn_effect(unsigned char effect);

/* strength, taking the Ring of Power into account: now in effective_strength_asm.s,
 * and is called for virtually every hit calculation in combat. */
unsigned char effective_strength(void);

/* wands */
unsigned char wand_effect[NUM_WAND_KINDS];
unsigned char wand_identified[NUM_WAND_KINDS];

#pragma rodata-name (push, "LC")
static const char wm_str0[] = "Oak wand"; /* LC */
static const char wm_str1[] = "Iron wand";
static const char wm_str2[] = "Bone wand";
static const char wm_str3[] = "Glass wand";
static const char wm_str4[] = "Copper wand";
#pragma rodata-name (pop)
static const char wm_str5[] = "Silver wand"; /* new ones, outside the LC, regular resident */
static const char wm_str6[] = "Ebony wand";
static const char wm_str7[] = "Ivory wand";
static const char *const wand_materials[NUM_WAND_KINDS] = {
    wm_str0, wm_str1, wm_str2, wm_str3, wm_str4, wm_str5, wm_str6, wm_str7
};
static const char *wand_material_name(unsigned char kind)
{
    return wand_materials[kind];
}

#pragma rodata-name (push, "LC")
static const char we_str0[] = "Wand of Magic Missile"; /* LC */
static const char we_str1[] = "Wand of Paralysis";
static const char we_str2[] = "Wand of Drain Life";
static const char we_str3[] = "Wand of Identify";
static const char we_str4[] = "Wand of Sleep";
#pragma rodata-name (pop)
static const char we_str5[] = "Wand of Force"; /* new ones, outside the LC, regular resident */
static const char we_str6[] = "Wand of Power";
static const char we_str7[] = "Wand of Ruin";
static const char *const wand_effect_names[NUM_WAND_KINDS] = {
    we_str0, we_str1, we_str2, we_str3, we_str4, we_str5, we_str6, we_str7
};
static const char *wand_effect_name(unsigned char effect)
{
    return wand_effect_names[effect];
}

const char *wand_display_name(unsigned char kind)
{
    if (kind >= NUM_WAND_KINDS) kind = 0;
    if (wand_identified[kind]) return wand_effect_name(wand_effect[kind]);
    return wand_material_name(kind);
}

/* treasures: for sale only, with no properties and no identification; the name is immediately visible. */
static const char *const treasure_names[NUM_TREASURE_KINDS] = {
    "Small gem", "Large gem", "Crystal set"
};
const char *treasure_display_name(unsigned char kind)
{
    if (kind >= NUM_TREASURE_KINDS) kind = 0;
    return treasure_names[kind];
}

/* weapon and armor stats: (hit, NdS+bonus) for weapons, [AC,PV] for armor/shields.
 * A second block was once planned for weapons [DV,PV] - a defense penalty for being
 * awkward to wield - but there was no actual mechanics behind it (the weapon's def1
 * was guaranteed to be 0), so it was removed when effective_defense() was ported to
 * assembly. def1 is now reserved for the 'identified' flag. Resident, needed by both
 * the view group and the command menu screen group. Placed in the Language Card. */
#pragma code-name (push, "LC")
void item_stat_str(char *buf, item_type_t t, signed char mod1, signed char mod2,
                    unsigned char dn, unsigned char ds, signed char def1, signed char def2)
{
    char s1;
    unsigned char a1;
    (void)def2; /* no longer displayed - left in the signature for the sake of a consistent calling interface */
    if ((t == ITEM_WEAPON_MELEE || t == ITEM_WEAPON_RANGED || t == ITEM_ARMOR || t == ITEM_SHIELD) && !def1) {
        buf[0] = '\0'; /* not identified (never worn) - specifications are completely hidden */
        return;
    }
    s1 = (mod1 < 0) ? '-' : '+';
    a1 = (unsigned char)((mod1 < 0) ? -mod1 : mod1);
    if (t == ITEM_WEAPON_MELEE || t == ITEM_WEAPON_RANGED) {
        if (mod2 > 0)
            sprintf(buf, "(%c%u, %ud%u+%d)", s1, a1, dn, ds, mod2);
        else if (mod2 < 0)
            sprintf(buf, "(%c%u, %ud%u-%u)", s1, a1, dn, ds, (unsigned char)(-mod2));
        else
            sprintf(buf, "(%c%u, %ud%u)", s1, a1, dn, ds);
        if (mod1 <= -2) strcat(buf, " CURSED");
    } else if (t == ITEM_ARMOR || t == ITEM_SHIELD) {
        if (mod2 < 0)
            sprintf(buf, "[AC:%u, PV:-%u]", (unsigned char)mod1, (unsigned char)(-mod2));
        else
            sprintf(buf, "[AC:%u, PV:%u]", (unsigned char)mod1, (unsigned char)mod2);
        if (mod2 < 0) strcat(buf, " CURSED");
    } else {
        buf[0] = '\0';
    }
}
#pragma code-name (pop)

/* more efficient version of 'item_stat_str()' for cases where "data is retrieved
 * directly from 'g_inventory[idx]'" - these cases make up the majority (7 out of 8),
 * and passing 8 separate fields on every call is expensive on the 6502. The
 * index is a single parameter instead of eight. */
void item_stat_str_inv(char *buf, unsigned char idx)
{
    inv_item_t *it = &g_inventory[idx];
    item_stat_str(buf, (item_type_t)it->type, it->mod1, it->mod2,
                  it->dice_n, it->dice_s, it->def1, it->def2);
}

/* read the selection character for the menu. Resident, required for four of the six menu screens. */
char read_choice(void)
{
    return cgetc();
}

/* general infrastructure for page-by-page lists (discard/sell/backpack/
 * known items). Placed in Language Card (#pragma code-name LC) - the
 * actual code bytes reside in bank $D400, not in regular memory, so
 * residency here costs the heap almost nothing. */
#pragma code-name (push, "LC")

/* exact size of the current page based on the total count, without
 * a separate counting loop, since the count is already known in its entirety. */
unsigned char page_size(unsigned char count, unsigned char per_page, unsigned char page)
{
    unsigned char page_start = (unsigned char)(page * per_page);
    unsigned char remaining = (count > page_start) ? (unsigned char)(count - page_start) : 0;
    return (remaining > per_page) ? per_page : remaining;
}

/* general implementation for both prompts below - mode 0: list is empty,
 * only 'Press any key' without a final 'clear_message()'; mode 1:
 * navigation menu ('has_more' toggles the text, with 'clear_message' at
 * the end); mode 2: character selection (Enter/Esc/character). */
static unsigned char read_page_common(unsigned char has_more, unsigned char mode, char *out_sel)
{
    clear_message();
    gotoxy(0, 0);
    if (mode == 0) {
        cputs("Press any key");
        cgetc();
        return 0;
    }
    if (mode == 1) {
        cputs(has_more ? "Press any key for more" : "Press any key");
        cgetc();
        clear_message();
        return 0;
    }
    cputs(has_more ? "Enter or Esc" : "Esc=cancel");
    *out_sel = read_choice();
    if (has_more && (*out_sel == 13 || *out_sel == ' ')) return 1;
    if (*out_sel == 27) { clear_message(); return 0; }
    clear_message();
    return 2;
}

/* hint + read selection for the 'Letter Selection' menu (discard, sell).
 * Returns: 0 = cancel/list is empty (key has already been read and processed),
 * 1 = continue (Enter/space), 2 = something else - the character itself is
 * stored in out_sel for further verification by the caller. */
unsigned char read_page_nav(unsigned char has_more, unsigned char empty, char *out_sel)
{
    return read_page_common(has_more, empty ? 0 : 2, out_sel);
}

/* hint + press ANY key for menus intended solely for viewing (backpack,
 * known items), no letter selection. */
void read_page_continue(unsigned char has_more)
{
    char dummy;
    read_page_common(has_more, 1, &dummy);
}

static const char msg_curse_lift[] = "You feel the curse lift";

/* item curse type: 0=not cursed, 1=weapon, 2=armor/shield, 3=ring.
 * The general helper for slot_is_cursed()/uncurse_slot() is below -
 * this avoids duplicating the same type comparison chain. */
static unsigned char curse_kind(inv_item_t *it)
{
    item_type_t t = (item_type_t)it->type;
    if ((t == ITEM_WEAPON_MELEE || t == ITEM_WEAPON_RANGED) && it->mod1 <= -2) return 1;
    if ((t == ITEM_ARMOR || t == ITEM_SHIELD) && it->mod2 < 0) return 2;
    if (t == ITEM_RING && it->mod2 == 1) return 3;
    return 0;
}

/* is the item in equipment slot slot cursed? (-1 = slot is empty).
 * Resident (LC), shared between the check when equipping/unequipping
 * (menus_action1.c) and the curse-removal scroll (menus_start.c). */
unsigned char slot_is_cursed(signed char slot)
{
    if (slot < 0) return 0;
    return curse_kind(&g_inventory[(unsigned char)slot]) != 0;
}

/* remove the curse from ONE specific equipment slot - weapon
 * -2->-1, armor/shield -1->0 (it is no longer bound, but is not
 * upgraded), ring - simply removes the flag; the effect remains
 * the same. Does nothing if the slot is not cursed. */
void uncurse_slot(signed char slot)
{
    inv_item_t *it;
    if (slot < 0) return;
    it = &g_inventory[(unsigned char)slot];
    switch (curse_kind(it)) {
        case 1: it->mod1 = -1; break;
        case 2: it->mod2 = 0; break;
        case 3: it->mod2 = 0; break;
    }
}

#pragma code-name (pop)

/* put the monsters that have woken up in cell (px, py) and all eight adjacent
 * ones (within a radius of one cell) back to sleep - a separate 50% roll for
 * each one. Returns 1 if at least one falls asleep. Standard resident (NOT LC). */
unsigned char sleep_nearby_monsters(unsigned char px, unsigned char py)
{
    signed char dx, dy;
    unsigned char any = 0;
    for (dy = -1; dy <= 1; dy++) {
        for (dx = -1; dx <= 1; dx++) {
            monster_t *m = monster_at((unsigned char)(px + dx), (unsigned char)(py + dy));
            if (m && m->alive && m->awake && rnd(2) == 0) {
                m->awake = 0;
                any = 1;
            }
        }
    }
    return any;
}

#pragma code-name (push, "LC")

/* entire effect of the 'Remove Curse' scroll consists of scanning the equipped
 * slots, making a selection (if there is more than one cursed item), and actually
 * removing the curse. Resident (LC), otherwise, the scroll reading screen
 * (OVERLAY5) wouldn't be able to fit all of this UI. */
void remove_curse_scroll_effect(unsigned char depth)
{
    signed char cslots[6];
    unsigned char cn = 0, ci;
    {
        signed char *eq_slots[6] = { &g_eq_armor, &g_eq_hand_l, &g_eq_hand_r,
                                      &g_eq_ranged, &g_eq_ring_l, &g_eq_ring_r };
        unsigned char si;
        for (si = 0; si < 6; si++)
            if (slot_is_cursed(*eq_slots[si])) cslots[cn++] = *eq_slots[si];
    }
    if (cn == 0) {
        queue_message("Nothing happens");
    } else if (cn == 1) {
        uncurse_slot(cslots[0]);
        queue_message(msg_curse_lift);
    } else {
        unsigned char row;
        char csel;
        open_window("Remove curse from?", cn);
        row = (unsigned char)(WIN_Y0 + 1);
        for (ci = 0; ci < cn; ci++) {
            inv_item_t *it = &g_inventory[(unsigned char)cslots[ci]];
            const char *nm = (it->type == ITEM_RING)
                ? ring_display_name((unsigned char)it->mod1)
                : item_name((item_type_t)it->type, it->dice_n, it->dice_s);
            gotoxy(WIN_LCOL, row++);
            cprintf("%c) %s", (char)('a' + ci), nm);
        }
        csel = read_choice();
        clear_message();
        close_window(depth);
        if (csel >= 'a' && (unsigned char)(csel - 'a') < cn) {
            uncurse_slot(cslots[(unsigned char)(csel - 'a')]);
            queue_message(msg_curse_lift);
        } else {
            queue_message("Cancelled");
        }
    }
}
#pragma code-name (pop)

/* total gold; stored as a regular item in the inventory (in a single stack). */
unsigned int get_gold_total(void)
{
    unsigned char i;
    for (i = 0; i < g_inv_count; i++)
        if (g_inventory[i].type == ITEM_GOLD) return g_inventory[i].amount;
    return 0;
}

/* item's value in gold is the same for the shop price (roll_random_item +
 * spawn_shop, OVERLAY6) and for selling (menu_shop_sell, OVERLAY8). Resident
 * so that both overlays can call it. Now takes into account the specific
 * type/effect within a category (not just the type) - see the definition below,
 * after the melee_price/armor_price tables, etc., which it needs. */
unsigned char item_price(item_type_t type, signed char mod1, unsigned char dice_s, signed char def2);

/* is there a merchant nearby (in one of the 8 adjacent cells)? This is now in
 * rare_ov10.c, and is called only when the P/S (buy/sell) keys are explicitly
 * pressed; it happens quite rarely per game. */
monster_t *merchant_adjacent(void);

/* effective defense (determines whether a monster can hit at all) =
 * base + armor DV. Final AC (how hard it is to hit the player).
 * Now in effective_defense_asm.s; called for every battle. */
unsigned char effective_defense(void);

/* armor PV - the amount of damage absorbed directly upon a successful hit.
 * Now in armor_pv_asm.s; called for every hit in combat. */
signed char armor_pv(void);

/* actual (not 'skipped') room containing a cell - or 0 if the cell is
 * outside any room (corridor). Now in room_asm.s, it is called very
 * frequently (every step, every cell rendered). */

/* same as above, but only if the room is fully lit (not dark) */
zone_t *lit_room_at(unsigned char x, unsigned char y)
{
    zone_t *z = room_at(x, y);
    return (z && !(z->flags & RF_DARK)) ? z : 0;
}

/* cell_visible() is now in cell_visible_asm.s - it is called for every
 * map cell each time the screen is redrawn (up to 1,680 times per frame). */
unsigned char cell_visible(unsigned char x, unsigned char y, zone_t *pr);

/* a cell was once visible but is not currently in view - should we display
 * it from memory? For regular rooms and hallways, we remember everything; for
 * dark rooms, we only remember the boundaries (walls, doors, stairs), not the
 * interior floor; if walk past it, we can see the outline, but not through
 * the room itself. */
static unsigned char should_remember(unsigned char x, unsigned char y)
{
    zone_t *z;
    if (!is_seen(x, y)) return 0;
    z = room_at(x, y);
    if (z && (z->flags & RF_DARK))
        return (tile_t)g_map[y][x] != T_FLOOR && (tile_t)g_map[y][x] != T_TRAP_HIDDEN;
    return 1;
}

static char tile_char(tile_t t)
{
    /* character sequence must match the 'tile_t' in 'level.h'. Stairs leading
	 * down (T_STAIRS_DOWN) are a special case; they depend on 'g_stairs_inverted'
	 * and do not fit into the table. */
    if (t == T_STAIRS_DOWN) return g_stairs_inverted ? '<' : '>';
    return " .-|----+-|#  .^"[t];
}

/* output the MouseText character using the escape sequence for the
 * 80-column card firmware: SO(15) ESC(27) CAN code(24) SI(14). IMPORTANT: Use
 * putchar() (stdio, which goes through the COUT firmware), not cputc() (conio,
 * writes directly to screen memory, bypassing the firmware, and therefore does
 * not recognize these control codes as commands). Calling gotoxy() before the
 * function call still sets the cursor - it is assumed that the cursor state is
 * shared between conio and stdio; if not, some adjustments may be needed here. */
void put_mousetext(char code)
{
    putchar((char)15);
    putchar((char)27);
    putchar(code);
    putchar((char)24);
    putchar((char)14);
}

/* draw a map cell - using a regular character or, for corridors, via MouseText
 * (code 86). After MouseText (which goes through putchar/firmware, not through
 * conio), be sure to move the cursor to the NEXT cell using gotoxy - otherwise,
 * conio won't know that the cursor has moved, and all cells in the same line
 * after the corridor will start shifting. */
static void output_tile(unsigned char x, unsigned char y, tile_t t)
{
    if (t == T_PASSAGE) {
        put_mousetext((char)87);
        gotoxy((unsigned char)(x + 1), (unsigned char)(y + 1));
    } else if (t == T_TRAP_REVEALED) {
        put_mousetext((char)91);
        gotoxy((unsigned char)(x + 1), (unsigned char)(y + 1));
    } else {
        cputc(tile_char(t));
    }
}

/* is the cell passable? Secret doors are impassable until they are found using
 * the search function (the 's' key / Ring of Searching) - after that, the cell
 * becomes a regular T_DOOR and is included in this list. */
unsigned char is_open_tile(tile_t t)
{
    return t == T_FLOOR || t == T_PASSAGE || t == T_DOOR || t == T_STAIRS_DOWN ||
           t == T_TRAP_HIDDEN || t == T_TRAP_REVEALED;
}

/* step bonus based on strength. */
static const signed char str_hit_table[21] = {
    -2,-2,-2,-2,-2,-2, -1,-1,-1,-1, 0,0,0,0,0,0, 1,1, 2,2, 3
};
static const signed char str_dmg_table[21] = {
    -1,-1,-1,-1,-1,-1, 0,0,0,0,0,0,0,0,0,0, 1,1, 2,2, 3
};
signed char str_bonus_hit(unsigned char str)
{
    if (str > 20) str = 20;
    return str_hit_table[str];
}

signed char str_bonus_dmg(unsigned char str)
{
    if (str > 20) str = 20;
    return str_dmg_table[str];
}

/* d20 roll against defense: the higher the defender's defense, the harder
 * it is to hit. attacker_bonus is added directly to the roll. */
unsigned char roll_to_hit(signed int attacker_bonus, unsigned char defense)
{
    signed int roll = (signed int)rnd(20) + attacker_bonus;
    return roll >= (signed int)defense;
}

/* live monster in cell (x, y); now in monster_at_asm.s, called
 * very frequently (every step, collision checks). */
monster_t *monster_at(unsigned char x, unsigned char y);

/* active item on the floor in cell (x, y) - now in item_at_asm.s, is called
 * very frequently (every time a cell is drawn, or when an item is picked up or dropped). */
ground_item_t *item_at(unsigned char x, unsigned char y);

/* the same as pick_random_open_cell() in spawn.c (a random accessible
 * cell anywhere on the map, free of players and monsters - for initial
 * level population), but only among cells already VISIBLE to the player -
 * for roaming monsters that appear where the character has already been. */
static unsigned char pick_random_seen_open_cell(unsigned char *ox, unsigned char *oy)
{
    unsigned char tries;
    for (tries = 0; tries < 200; tries++) {
        unsigned char rx = rnd(MAP_W);
        unsigned char ry = rnd(MAP_H);
        tile_t t = (tile_t)g_map[ry][rx];
        if (is_seen(rx, ry) && (t == T_FLOOR || t == T_PASSAGE) &&
            !(rx == player.x && ry == player.y) && !monster_at(rx, ry) && !item_at(rx, ry)) {
            *ox = rx; *oy = ry;
            return 1;
        }
    }
    return 0;
}

/* fixed list of monster stats. HP is a range; the rest are fixed by type;
 * spawn depth and item drop chance are also listed here.
 *
 * fields, in order (see monster_template_t in game_shared.h):
 *   letter           - map glyph for this species
 *   name             - display name
 *   hp_lo, hp_hi     - HP range rolled once on spawn (monster_init_stats)
 *   attack           - this monster's to-hit bonus when it attacks the player
 *                      (roll_to_hit(m->attack, effective_defense()))
 *   defense          - target number the PLAYER's roll must meet or beat
 *                      to hit this monster (roll_to_hit(bonus, m->defense))
 *   dmg_lo, dmg_hi   - melee damage range this monster deals per hit
 *   depth_lo, depth_hi - dungeon depth range this species can spawn at
 *                      (pick_species_for_depth)
 *   chance_to_drop   - percent chance to drop an item on death
 *   exp              - experience points awarded for the kill */
const monster_template_t monster_templates[NUM_MONSTER_SPECIES] = {
    /*ltr  name			hp_lo	hp_hi	atk def dmg_lo	dmg_hi	d_lo	d_hi	drop%	exp */
    { 'A', "Arachne",	5,		40,		2,	2,	4,		8,		5,		14,		0,		40	},
    { 'b', "Bat",		1,		8,		0,	0,	1,		2,		1,		6,		0,		1	},
    { 'C', "Cyclops",	4,		32,		4,	4,	6,		6,		7,		25,		15,		30	},
    { 'D', "Demon",		10,		60,		5,	5,	8,		12,		15,		99,		100,	75	},
    { 'f', "Fairy",		4,		12,		1,	2,	3,		6,		4,		15,		25,		10	},
    { 'e', "Elemental",	1,		8,		0,	2,	1,		3,		1,		7,		0,		4	},
    { 'g', "Goblin",	1,		8,		1,	1,	1,		8,		2,		9,		20,		5	},
    { 'h', "Harpy",		5,		20,		1,	3,	2,		10,		6,		14,		10,		12	},
    { 'i', "Imp",		10,		12,		2,	4,	6,		8,		8,		20,		10,		20	},
	{ 'V', "Vampir",	10,		20,		2,	2,	8,		10,		10,		30,		20,		35	},
	{ 'B', "Bauk",		20,		25,		4,	4,	6,		10,		8,		99,		15,		45	},
	{ 'Z', "Zombie",	1,		15,		1,	0,	1,		6,		3,		12,		5,		5	}
};

/* two-handed weapons - determined by dice rolls, using the same method as
 * for the weapon name (Greatsword 1d20, Quarterstaff 2d3, Pike 1d12).
 * Resident - required both in menus_action1.c (equipping) and in main.c
 * (no check required during an attack: two-handed weapons are always held
 * only in the right hand; the left hand is simply empty - the combat system
 * skips it without any changes). */
unsigned char is_two_handed_weapon(unsigned char dice_n, unsigned char dice_s)
{
    return (dice_n == 1 && dice_s == 20) || (dice_n == 1 && dice_s == 12) ||
           (dice_n == 2 && dice_s == 3);
}

/* is the right hand actually occupied by a two-handed weapon? If so, the left
 * hand is unavailable for equipping anything, even though it is formally
 * empty (g_eq_hand_l == -1). */
unsigned char right_hand_two_handed(void)
{
    if (g_eq_hand_r < 0) return 0;
    {
        inv_item_t *it = &g_inventory[g_eq_hand_r];
        return it->type == ITEM_WEAPON_MELEE && is_two_handed_weapon(it->dice_n, it->dice_s);
    }
}

/* tiny - used in menu_read() (menus_start.c, list filter), and twice in
 * spawn.c: can_be_unidentified() (the identify-scroll target check) and
 * spawn_shop() (marking a scroll kind identified once the merchant offers it). */
unsigned char is_scroll(item_type_t t)
{
    return t >= ITEM_SCROLL_MAPPING && t <= ITEM_SCROLL_REMOVE_CURSE;
}

/* name for the species display string, from monster_templates[].
 * m->species must be a real spawn index here, not the merchant's
 * special 0xFF placeholder (game_shared.h) - that would read past
 * the end of the array. */
const char *monster_name(monster_t *m)
{
    return monster_templates[m->species].name;
}

/* randomly select a type that matches the depth (rejection sampling -
 * there are only 9 types, which is cheaper than compiling and storing
 * a list of suitable ones). */
unsigned char pick_species_for_depth(unsigned char depth)
{
    unsigned char idx;
    const monster_template_t *mt;
    do {
        idx = rnd(NUM_MONSTER_SPECIES);
        mt = &monster_templates[idx];
    } while (depth < mt->depth_lo || depth > mt->depth_hi);
    return idx;
}

/* fill in the monster's stats from a template - now in
 * monster_init_stats_asm.s, called every time a monster spawns. */
void monster_init_stats(monster_t *m, unsigned char species);

unsigned char inventory_add(item_type_t type, signed char mod1, signed char mod2,
                             unsigned char dice_n, unsigned char dice_s,
                             signed char def1, signed char def2)
{
    inv_item_t *it;
    if (g_inv_count >= MAX_INVENTORY) return 0;
    it = &g_inventory[g_inv_count];
    it->type = type;
    it->mod1 = mod1;
    it->mod2 = mod2;
    it->def1 = def1;
    it->def2 = def2;
    it->dice_n = dice_n;
    it->dice_s = dice_s;
    it->amount = 0;
    g_inv_count++;
    return 1;
}

unsigned char give_simple(item_type_t type, signed char mod1, signed char mod2); /* forward declaration, needed in try_pickup AND now in rare_ov10.c */

/* remove item idx from the inventory - shift the array and adjust the equipment
 * indices. Common to all 'consuming' screens (discarding, eating, potions,
 * wands when out of charges, shooting when out of arrows), which previously
 * each duplicated this block on their own. Resident, so as not to repeat
 * it in every overlay again. */
void inventory_remove(unsigned char idx)
{
    unsigned char j;
    for (j = idx; (unsigned char)(j + 1) < g_inv_count; j++) g_inventory[j] = g_inventory[j + 1];
    g_inv_count--;
    if (g_eq_armor > (signed char)idx) g_eq_armor--;
    if (g_eq_hand_l > (signed char)idx) g_eq_hand_l--;
    if (g_eq_hand_r > (signed char)idx) g_eq_hand_r--;
    if (g_eq_ranged > (signed char)idx) g_eq_ranged--;
    if (g_eq_ring_l > (signed char)idx) g_eq_ring_l--;
    if (g_eq_ring_r > (signed char)idx) g_eq_ring_r--;
}

/* the name of the item displayed on the floor - the common part of the messages
 * 'You found a ...' and 'Your pack is full. You see a ... here.' (previously duplicated). */
static const char *ground_item_display_name(ground_item_t *gi)
{
    if (gi->type == ITEM_POTION) return potion_display_name((unsigned char)gi->mod1);
    if (gi->type == ITEM_RING) return ring_display_name((unsigned char)gi->mod1);
    if (gi->type == ITEM_WAND) return wand_display_name((unsigned char)gi->mod1);
    if (gi->type == ITEM_TREASURE) return treasure_display_name((unsigned char)gi->mod1);
    return item_name((item_type_t)gi->type, gi->dice_n, gi->dice_s);
}

/* 'Your pack is full' message for an item that couldn't be picked up -
 * shared by both branches of try_pickup() (the stacked gold/food/ammo
 * case and the regular inventory_add() case). */
static void pack_full_message(ground_item_t *gi)
{
    char buf[64];
    sprintf(buf, "Your pack is full. You see a %s here", ground_item_display_name(gi));
    queue_message(buf);
}

/* weighted random pick of an index 0..n-1 from an array of weights - the
 * single point of rarity control for every item category (weapons, armor,
 * rings, etc). A higher weight means a higher chance to be picked. Weight
 * sums here are always small (~76 at most, for melee weapons), so they fit
 * in one byte with no overflow risk. Now in weighted_pick_asm.s - called
 * very often during item generation, for every category. */
unsigned char weighted_pick(const unsigned char *weights, unsigned char n);

/* find an existing stack of this type in the inventory; otherwise, create a
 * new one - general logic for picking up items from the ground (try_pickup)
 * and purchasing from a merchant (menu_shop_buy, level_shapes.c). Resident,
 * so the overlay can call it cheaply. Returns the slot index or 0xFF if there
 * is no space at all. */
unsigned char find_or_add_stack(item_type_t type, signed char kind)
{
    unsigned char k;
    for (k = 0; k < g_inv_count; k++)
        if (g_inventory[k].type == type && g_inventory[k].mod1 == kind) return k;
    if (!give_simple(type, kind, 0)) return 0xFF;
    return (unsigned char)(g_inv_count - 1);
}

/* automatic pickup of whatever's under the player's feet. Gold, food and ammo
 * are regular inventory items, stacked together (amount) rather than tracked
 * by a separate counter or a separate slot for each item, via find_or_add_stack();
 * everything else goes through a normal inventory_add() slot. The quest ring
 * (ITEM_QUEST_RING) is a special case - it flips the stairs for the rest
 * of the level. If there's no room, the item stays on the ground (pack_full_message()). */
static void try_pickup(void)
{
    ground_item_t *gi = item_at(player.x, player.y);
    if (!gi || !gi->active) return;

    if (gi->type == ITEM_GOLD || gi->type == ITEM_FOOD || gi->type == ITEM_AMMO) {
        unsigned char k = find_or_add_stack((item_type_t)gi->type, gi->mod1);
        unsigned char add = gi->amount;
        if (k == 0xFF) {
            pack_full_message(gi);
            return;
        }
        g_inventory[k].amount += add;
        gi->active = 0;
        if (gi->type == ITEM_GOLD) queue_message("Found some gold");
        else if (gi->type == ITEM_FOOD) queue_message("Found food ration");
        else {
            char buf[48];
            sprintf(buf, "Found %u %s", add, ammo_display_name((unsigned char)gi->mod1));
            queue_message(buf);
        }
    } else if (inventory_add(gi->type, gi->mod1, gi->mod2, gi->dice_n, gi->dice_s, gi->def1, gi->def2)) {
        gi->active = 0;
        if (gi->type == ITEM_QUEST_RING) {
            g_has_signet = 1;
            g_ring_found_depth = g_depth;
            g_stairs_inverted = 1;
            queue_message("You feel the dungeon shift around you...");
            render_full(g_depth); /* stairs have changed their icon - redraw the entire screen immediately */
        } else {
            queue_message_str("You found a %s", ground_item_display_name(gi));
        }
    } else {
        pack_full_message(gi);
    }
}

/* draw a single map cell at (x, y). Priority: the player's own glyph first,
 * then (if the cell is currently visible) a monster, then a ground item,
 * then the tile underneath. A cell outside current view but remembered
 * (should_remember) redraws the tile only, never monsters or items (see
 * the comments below for why); anything else unseen is blank. */
void draw_cell(unsigned char x, unsigned char y, zone_t *pr)
{
    gotoxy(x, y + 1); /* screen line 0 is reserved for messages; the map starts on line 1 */
    if (x == player.x && y == player.y) {
        set_seen(x, y);
        cputc('@');
        return;
    }
    if (cell_visible(x, y, pr)) {
        monster_t *m;
        ground_item_t *gi;
        set_seen(x, y);
        m = monster_at(x, y);
        if (m) {
            cputc(m->is_merchant ? 'M' : monster_templates[m->species].letter);
            return;
        }
        gi = item_at(x, y);
        if (gi && gi->active) {
            cputc(item_glyph(gi->type));
            return;
        }
        output_tile(x, y, (tile_t)g_map[y][x]);
    } else if (should_remember(x, y)) {
        /* don't draw objects from memory; otherwise, the cartography scroll
         * would reveal their locations as well, and that shouldn't happen */
        output_tile(x, y, (tile_t)g_map[y][x]); /* don't remember the monsters either, they're moving */
    } else {
        cputc(' ');
    }
}

static void redraw_region(unsigned char x0, unsigned char y0, unsigned char x1, unsigned char y1)
{
    unsigned char x, y;
    zone_t *pr = lit_room_at(player.x, player.y); /* calculate once for the entire region, not per cell */
    for (y = y0; y <= y1; y++)
        for (x = x0; x <= x1; x++)
            draw_cell(x, y, pr);
}

/* effects row (row 22, between the map and the status bar). Currently
 * poison and hunger; a slot for any potential status (paralysis, blindness,
 * etc.) - just append a new condition separated by a space. */
void draw_effects(void)
{
    gotoxy(0, 22);
    cclear(80);
    gotoxy(0, 22);
    if (g_poison_turns > 0) cputs("Poisoned ");
    if (pstat.hunger >= 2000) cputs("You are starving!");
    else if (pstat.hunger >= 1500) cputs("Weak");
    else if (pstat.hunger >= 1000) cputs("Hungry");
}

/* status bar (row 23, inverse video) - level, exp, HP, strength, AC and PV,
 * followed by the effects row (draw_effects()). Clears only up to the '?:Help'
 * hint in the corner rather than over it (see the comment below for why),
 * except in the rare case the status text itself runs long enough to reach
 * the hint - then it clears the whole row and redraws the hint via
 * draw_help_hint(). */
void draw_status(unsigned char depth)
{
    static char buf[90];
    unsigned char len;
    sprintf(buf, " Lvl:%u  Exp:%u(%lu/%lu)  HP:%u(%u)  Str:%u  AC:%u  PV:%d",
        depth, pstat.level, pstat.xp, xp_threshold(pstat.level), pstat.hp, pstat.hp_max,
        effective_strength(), effective_defense(), armor_pv());
    gotoxy(0, 23);
    revers(1);
    cputs(buf);
    len = (unsigned char)strlen(buf);
    /* clear only up to the prompt in the right corner, not past it; otherwise it
     * would be erased and rewritten every turn, causing it to flicker */
    if (len < 66) {
        cclear((unsigned char)(66 - len));
    } else if (len < 80) {
        cclear((unsigned char)(80 - len)); /* rare case of a long string, it overlaps the tooltip; clear it as before */
        draw_help_hint();
    }
    revers(0);
    draw_effects();
}

/* display the '?:Help' tooltip in the right corner of the status bar - usually just
 * once at startup (draw_status() no longer modifies this area unless necessary),
 * but it needs to be displayed again after full-screen operations such as saving. */
void draw_help_hint(void)
{
    gotoxy(66, 23);
    revers(1);
    cputs("!:Help ?:Keys ");
    revers(0);
}

/* message bar at the top of the screen (line 0) remains displayed until it
 * is replaced by the next message or is explicitly cleared. */
static unsigned char g_msg_shown_this_turn = 0;
static char last_message[72] = "";

static void clear_msg_line(void)
{
    gotoxy(0, 0);
    cclear(80);
    gotoxy(0, 0);
}

static void show_message(const char *msg)
{
    clear_msg_line();
    cputs(msg);
}

void clear_message(void)
{
    gotoxy(0, 0);
    cclear(80);
    g_msg_shown_this_turn = 0;
}

/* window in the center of the screen for the menu (inventory, equip,
 * discard...) - the WIN_* constants are declared in game_shared.h
 * (they're needed both here and in the overlay). */

static unsigned char g_win_max_y1;  /* highest lower bound for the current window 'session'
									 * (between opening and closing) - this is needed so that,
									 * for multi-page menus (backpack), closing removes the
									 * highest page ever displayed, rather than the last one. */

/* rows - the number of rows of content needed (excluding the header
 * row and the bottom border). The window is always the same width, but
 * its height is exactly as tall as the content - no longer than WIN_Y1,
 * but also no taller than necessary. */
void open_window(const char *title, unsigned char rows)
{
    unsigned char x, y;
    unsigned char y1 = (unsigned char)(WIN_Y0 + rows + 1);
    unsigned char clear_y1;
    if (y1 > WIN_Y1) y1 = WIN_Y1;
    if (y1 < (unsigned char)(WIN_Y0 + 1)) y1 = (unsigned char)(WIN_Y0 + 1); /* minimum: frame + 0 lines */
    /* clear up to g_win_max_y1; if it is greater,
	 * see the comment next to the variable. */
    clear_y1 = (g_win_max_y1 > y1) ? g_win_max_y1 : y1;
    g_win_max_y1 = clear_y1;
    for (y = WIN_Y0; y <= clear_y1; y++) {
        gotoxy(WIN_X0, y);
        cclear((unsigned char)(WIN_X1 - WIN_X0 + 1));
    }
    for (x = (unsigned char)(WIN_X0 + 1); x < WIN_X1; x++) {
        gotoxy(x, WIN_Y0); cputc('_'); /* top */
        gotoxy(x, y1); put_mousetext((char)76); /* bottom */
    }
    for (y = (unsigned char)(WIN_Y0 + 1); y < y1; y++) {
        gotoxy(WIN_X0, y); put_mousetext((char)90); /* left side */
        gotoxy(WIN_X1, y); put_mousetext((char)95); /* right side */
    }
    /* corners are intentionally left blank */
    gotoxy((unsigned char)(WIN_X0 + 2), WIN_Y0);
    cprintf(" %s ", title);
}

/* close the window: redraw only its actual map area plus
 * the status bar, rather than always using the maximum previous size. */
void close_window(unsigned char depth)
{
    unsigned char my0 = (unsigned char)(WIN_Y0 - 1); /* map on the screen is shifted up by +1 row */
    unsigned char my1 = (unsigned char)(g_win_max_y1 - 1);
    if (my1 >= MAP_H) my1 = MAP_H - 1;
    redraw_region(WIN_X0, my0, WIN_X1, my1);
    g_win_max_y1 = 0; /* window session has been closed; the next time it is opened, the count will start over */
    draw_status(depth);
}

/* if this isn't the first message of the turn - first display '-more-'
 * and wait for a key press, so the player has time to read the previous
 * message before it disappears. */
static void wait_more(void)
{
    gotoxy(73, 0);
    cputs("-more-");
    cgetc();
}

void queue_message(const char *msg)
{
    if (g_msg_shown_this_turn) wait_more();
    show_message(msg);
    g_msg_shown_this_turn = 1;
    strncpy(last_message, msg, 71);
    last_message[71] = '\0';
}

void queue_message_str(const char *fmt, const char *s)
{
    if (g_msg_shown_this_turn) wait_more();
    clear_msg_line();
    cprintf(fmt, s);
    g_msg_shown_this_turn = 1;
    sprintf(last_message, fmt, s);
}

/* experience threshold for the next level is a doubling
 * of 10: 10, 20, 40, 80... (after 19 steps, it stops increasing
 * to avoid overflow). */
unsigned long xp_threshold(unsigned char level)
{
    unsigned long t = 10;
    unsigned char i;
    for (i = 1; i < level && i < 19; i++) t <<= 1;
    return t;
}

void check_level_up(void)
{
    unsigned long need = xp_threshold(pstat.level);
    if (pstat.xp >= need) {
        unsigned char add = 2 + rnd(4);
        pstat.level++;
        pstat.hp_max = (unsigned char)(pstat.hp_max + add);
        pstat.hp = pstat.hp_max; /* LevelUp also heals completely */
        {
            char buf[24];
            sprintf(buf, "Welcome to level %u!", pstat.level);
            queue_message(buf);
        }
    }
}

/* health regeneration: a counter for 'idle' turns without taking damage. */
static void regenerate(void)
{
    unsigned char fast = ring_worn_effect(RING_EFFECT_REGENERATION); /* regeneration ring - heals twice as often */
    unsigned int threshold = fast ? 10 : 20;
    if (pstat.hp >= pstat.hp_max) return;
    if ((unsigned int)g_quiet_turns > threshold) {
        pstat.hp++;
        if (pstat.hp > pstat.hp_max) pstat.hp = pstat.hp_max;
        g_quiet_turns = 0;
    }
}

void player_die(const char *cause)
{
    if (load_overlay10_safe()) handle_player_death(cause); /* points + screen + Hall of Fame - one load */
    videomode(VIDEOMODE_40COL); /* 80 columns - hardware state, survives exit to BASIC */
    exit(0); /* game doesn't return to the menu and just closes */
}

/* melee and ranged weapon stats as parallel arrays (not an array of
 * structs), so weighted_pick() can take melee_weight[] directly without
 * a wrapper that copies the weights into a temporary array on every call. */
static const unsigned char melee_dice_n[13] = { 1, 1, 1, 2, 2, 1, 1, 2, 2, 1, 1, 1, 1 };
static const unsigned char melee_dice_s[13] = { 4, 6, 10, 5, 6, 8, 7, 4, 3, 9, 12, 20, 3 };
static const unsigned char melee_weight[13] = { 10, 10, 5, 5, 2, 10, 10, 5, 10, 5, 2, 2, 10 };
static const unsigned char melee_price[13]  = { 20, 30, 50, 55, 65, 40, 35, 45, 35, 45, 60, 95, 10 };
/* Dagger,ShortSword,LongSword,Saber,Rapier,Mace,Warhammer,Sickle,
 * Quarterstaff,ShortSpear,Pike,Greatsword,Knife */

static const unsigned char ranged_dice_s[4] = { 4, 6, 10, 12 };
static const unsigned char ranged_weight[4] = { 10, 10, 5, 2 };
static const unsigned char ranged_price[4]  = { 25, 35, 50, 55 };
/* ShortBow,LongBow,CompositeBow,ElfBow */

/* same approach applies to rings, wands, and potions: each slot (type) in this
 * game has its own randomly shuffled effect (slot_effect[]), and we want the weight
 * to be based on the EFFECT (effect_weight[]), not on the slot. */
static unsigned char pick_by_effect(const unsigned char *slot_effect, const unsigned char *effect_weight, unsigned char n)
{
    unsigned char kw[12]; /* maximum among calls - NUM_RING_KINDS=12; previously kw[8] -
						   * stack overflow when n>8 (rings and, after adding
						   * beer/wine, potions) wrote outside the massive bounds */
    unsigned char i;
    for (i = 0; i < n; i++) kw[i] = effect_weight[slot_effect[i]];
    return weighted_pick(kw, n);

}

/* random quality of a weapon instance. Out of 20: 2 = cursed (-2),
 * 4 = bad (-1), 9 = normal (0), 5 = good (+1). */
static signed char quality_roll(void)
{
    unsigned char r = rnd(20);
    if (r < 2) return -2;
    if (r < 6) return -1;
    if (r < 15) return 0;
    return 1;
}

/* random PV of armor/shield. Out of 20: 2 = cursed (-1), 10 = zero (0),
 * 6 = +1, 2 = +2. */
static signed char pv_roll(void)
{
    unsigned char r = rnd(20);
    if (r < 2) return -1;
    if (r < 12) return 0;
    if (r < 18) return 1;
    return 2;
}

/* final AC of armor by kind - all armor except Robe gets +1, Full Plate
 * gets +2. Index is (kind-1), i.e. k from gen_armor_like() or (kind-1)
 * from give_armor(). Shared by both armor generation sites. */
const unsigned char armor_ac[7] = { 1, 3, 4, 5, 6, 7, 9 };

/* armor and shield are generated the same way: the kind is a weighted
 * pick, AC is fixed by kind, PV is a separate roll. Shared by both
 * categories. */
static void gen_armor_like(ground_item_t *gi, item_type_t type, const unsigned char *weight, unsigned char n)
{
    unsigned char k = weighted_pick(weight, n);
    gi->type = type;
    gi->dice_s = (unsigned char)(k + 1);                                    /* kind index 1..7 */
    gi->mod1 = (signed char)((type == ITEM_ARMOR) ? armor_ac[k] : (k + 1)); /* AC: armor from the table, shield = kind */
    gi->mod2 = pv_roll();                                                   /* PV, can be cursed (-1) */
}

/* rarity/price of armor and shields - the index matches (type-1), i.e.,
 * (dice_s-1) on the item. Common types appear more often and cost less. */
static const unsigned char armor_weight[7] = { 10, 10, 10, 5, 5, 2, 2 };
static const unsigned char armor_price[7]  = { 15, 20, 30, 35, 40, 45, 50 };
static const unsigned char shield_weight[4] = { 10, 10, 5, 2 };
static const unsigned char shield_price[4]  = { 15, 20, 30, 35 };

/* price of scrolls: the index corresponds to (type - ITEM_SCROLL_MAPPING), i.e.,
 * the enum order: Mapping, Sleep, Create Food, Aggravate, Identify. */
static const unsigned char scroll_price[6] = { 45, 50, 30, 10, 30, 35 };

/* price of rings/wands/potions is based on their EFFECT (not their
 * appearance or slot), since the player only learns the price in the shop,
 * and when they equip a ring, the effect is revealed immediately. */
static const unsigned char ring_effect_price[NUM_RING_KINDS] = {
    48, 50, 70, 5, 45, 45, 72, 75,	/* Protection,Regen,Strength,None,SlowDig,Searching,Dex,Damage */
    2, 2, 2, 5						/* Poison,Sleep,FastDigestion,Aggravate */
};
static const unsigned char wand_effect_price[NUM_WAND_KINDS] = {
    40, 65, 55, 80, 45, 48, 52, 60	/* Missile,Paralyze,DrainLife,Identify,Sleep,Force,Power,Ruin */
};
static const unsigned char potion_effect_price[NUM_POTION_KINDS] = {
    20, 50, 2, 2, 10, 60, 60, 4, 6	/* Healing,Strength,Poison,Water,Digestion,Dexterity,Damage,Beer,Wine */
};
static const unsigned char ammo_kind_price[NUM_AMMO_KINDS] = { 2, 4, 8 };
static const unsigned char treasure_kind_price[NUM_TREASURE_KINDS] = { 20, 45, 80 };

/* base price of an item by type - one resident function for buying
 * (spawn_shop), selling (half of this, level_shapes.c) and the score
 * calculation (rare_ov10.c). Which field selects the price depends on
 * the type: melee/ranged weapons use def2 (kind index), armor and
 * shields use dice_s (kind 1..), rings/wands/potions use mod1 through
 * the *_effect[] table, so the price follows the real effect, not the
 * unidentified appearance. Out-of-range indexes fall back to a default. */
unsigned char item_price(item_type_t type, signed char mod1, unsigned char dice_s, signed char def2)
{
    if (type >= ITEM_SCROLL_MAPPING && type <= ITEM_SCROLL_REMOVE_CURSE)
        return scroll_price[type - ITEM_SCROLL_MAPPING];
    switch (type) {
        case ITEM_WEAPON_MELEE:
            return ((unsigned char)def2 < 13) ? melee_price[(unsigned char)def2] : 30;
        case ITEM_WEAPON_RANGED:
            return ((unsigned char)def2 < 4) ? ranged_price[(unsigned char)def2] : 35;
        case ITEM_ARMOR:
            return (dice_s >= 1 && dice_s <= 7) ? armor_price[dice_s - 1] : 15;
        case ITEM_SHIELD:
            return (dice_s >= 1 && dice_s <= 4) ? shield_price[dice_s - 1] : 15;
        case ITEM_RING:
            return ring_effect_price[ring_effect[(unsigned char)mod1]];
        case ITEM_WAND:
            return wand_effect_price[wand_effect[(unsigned char)mod1]];
        case ITEM_POTION:
            return potion_effect_price[potion_effect[(unsigned char)mod1]];
        case ITEM_AMMO:
            return ammo_kind_price[(unsigned char)mod1];
        case ITEM_TREASURE:
            return treasure_kind_price[(unsigned char)mod1];
        case ITEM_FOOD: return 5;
        case ITEM_QUEST_RING: return 10;
        default: return 10; /* GOLD and anything else not listed */
    }
}

/* randomly select one item using the standard probability table - the one
 * used for the main spawn, treasure chests, shop items, and monster loot.
 * This table is resident; from resident code, it is called by 'monster_drop()',
 * and from overlays, by 'menus_action3.c' when a monster is killed with an
 * arrow or a wand. */
void roll_random_item(ground_item_t *gi, unsigned char depth)
{
    unsigned char roll = rnd(100);
    gi->mod1 = 0;
    gi->mod2 = 0;
    gi->def1 = 0;
    gi->def2 = 0;
    gi->dice_n = 0;
    gi->dice_s = 0;
    gi->amount = 0;
    if (roll < 35) {
        gi->type = ITEM_GOLD;
        {
            unsigned char cap = (unsigned char)((depth * 3 + 5) > 250 ? 250 : depth * 3 + 5);
            gi->amount = (unsigned char)(1 + rnd(cap));
        }
    } else if (roll < 48) {
        unsigned char widx;
        gi->type = (roll < 43) ? ITEM_WEAPON_MELEE : ITEM_WEAPON_RANGED;
        if (gi->type == ITEM_WEAPON_MELEE) {
            widx = weighted_pick(melee_weight, 13);
            gi->dice_n = melee_dice_n[widx];
            gi->dice_s = melee_dice_s[widx];
        } else {
            widx = weighted_pick(ranged_weight, 4);
            gi->dice_n = 1;
            gi->dice_s = ranged_dice_s[widx];
        }
        gi->mod1 = quality_roll();				/* -2..+1 to hit - random quality of the instance */
        gi->mod2 = (rnd(3) == 0) ? (signed char)(1 + rnd(3)) : 0;	/* occasionally enchanted */
        gi->def2 = (signed char)widx;			/* product index - for item_price(), without searching dices */
    } else if (roll < 56) {
        gen_armor_like(gi, ITEM_ARMOR, armor_weight, 7);
    } else if (roll < 61) {
        gen_armor_like(gi, ITEM_SHIELD, shield_weight, 4);
    } else if (roll < 66) {
        static const unsigned char ring_kind_weight[NUM_RING_KINDS] = {
            /* index by EFFECT: Protection, Regen, Strength, None, SlowDig, Searching, Dex, Damage */
            5, 5, 2, 10, 5, 5, 2, 2,
            /* Poison, Sleep, Fast Digestion, Aggravate - all 'unusual' */
            5, 5, 5, 5
        };
        gi->type = ITEM_RING;
        gi->mod1 = (signed char)pick_by_effect(ring_effect, ring_kind_weight, NUM_RING_KINDS);
        gi->mod2 = (rnd(10) < 1) ? 1 : 0; /* curse - regardless of the effect, 10%, same as for weapons/armor */
    } else if (roll < 71) {
        static const unsigned char wand_kind_weight[NUM_WAND_KINDS] = {
            /* index by EFFECT: Missile, Paralyze, Drain Life, Identify, Sleep, Force, Power, Ruin */
            10, 2, 5, 2, 5, 5, 5, 5
        };
        gi->type = ITEM_WAND;
        gi->mod1 = (signed char)pick_by_effect(wand_effect, wand_kind_weight, NUM_WAND_KINDS);
        gi->mod2 = (signed char)(2 + rnd(9));		/* 2-10 charges */
    } else if (roll < 76) {
        /* order of the weights matches the order of the ITEM_SCROLL_* entries in the enum:
         * Mapping, Sleep, Create Food, Aggravate, Identify, Remove Curse. */
        static const unsigned char scroll_weight[6] = { 4, 4, 8, 10, 8, 6 };
        gi->type = (item_type_t)(ITEM_SCROLL_MAPPING + weighted_pick(scroll_weight, 6));
    } else if (roll < 84) {
        gi->type = ITEM_FOOD;
        gi->amount = 1;
    } else if (roll < 92) {
        gi->type = ITEM_AMMO;
        gi->amount = (unsigned char)(1 + rnd(5));	/* stack of 1-5 arrows */
        {
            unsigned char kroll = rnd(100);
            gi->mod1 = (kroll < 70) ? AMMO_NORMAL : (kroll < 92) ? AMMO_QUALITY : AMMO_MAGIC;
        }
    } else if (roll < 97) {
        /* index by EFFECT: Healing, Strength, Poison (see POTION_EFFECT_*) */
        static const unsigned char potion_kind_weight[NUM_POTION_KINDS] = {
            /* Healing,Strength,Poison,Water,Digestion,Dexterity,Damage,Beer,Wine */
            10, 3, 10, 10, 2, 2, 2, 10, 10
        };
        gi->type = ITEM_POTION;
        gi->mod1 = (signed char)pick_by_effect(potion_effect, potion_kind_weight, NUM_POTION_KINDS);
    } else {
        static const unsigned char treasure_weight[NUM_TREASURE_KINDS] = { 10, 5, 2 };
        gi->type = ITEM_TREASURE;
        gi->mod1 = (signed char)weighted_pick(treasure_weight, NUM_TREASURE_KINDS); /* type is immediately apparent, no need to be identify */
    }
}

/* find a slot for a new item on the floor - first, look for a slot that's already
 * free (active=0, item picked up) among those already selected, and only if none
 * exists - create a new one, if there's space. Without this, 'g_ground_count'
 * would just keep increasing throughout the entire level, and after about ten picks,
 * it would be impossible to discard anything else, even though the floor is
 * actually empty. Returns an index or 0xFF if there is absolutely no space. */
unsigned char find_free_ground_slot(void)
{
    unsigned char i;
    for (i = 0; i < g_ground_count; i++)
        if (!g_ground_items[i].active) return i;
    if (g_ground_count < MAX_GROUND_ITEMS) return g_ground_count++;
    return 0xFF;
}

/* ask for a quantity (1..max) to discard from the stack (gold, arrows, food).
 * An empty input or 0 means cancel (return 0). If the input is greater than
 * max, truncate it to max instead of rejecting it - this is more convenient than
 * forcing a recalculation. */
unsigned int read_amount(unsigned int max)
{
    char buf[6];
    unsigned char len = 0;
    unsigned int val;
    for (;;) {
        char ch = cgetc();
        if (ch == 13) {
            if (len == 0) return 0;
            buf[len] = '\0';
            val = (unsigned int)atoi(buf);
            if (val == 0) return 0;
            if (val > max) val = max;
            return val;
        }
        if ((ch == 8 || ch == 127) && len > 0) {
            len--;
            cputc(8); cputc(' '); cputc(8);
            continue;
        }
        if (ch >= '0' && ch <= '9' && len < 5) {
            buf[len++] = ch;
            cputc(ch);
        }
    }
}

/* on a monster's death: with the species' chance_to_drop (percent, from
 * monster_templates[]) a random item for the current depth lands on the
 * monster's cell, if there is a free ground slot. */
void monster_drop(monster_t *m)
{
    unsigned char slot;
    if (rnd(100) >= monster_templates[m->species].chance_to_drop) return;
    slot = find_free_ground_slot();
    if (slot == 0xFF) return;
    {
        ground_item_t *gi = &g_ground_items[slot];
        gi->x = m->x;
        gi->y = m->y;
        gi->active = 1;
        roll_random_item(gi, g_depth);
    }
}

/* melee damage from a single hand - now in hand_weapon_damage_asm.s -
 * is called twice per strike (left and right hand). */
signed int hand_weapon_damage(signed char eq_slot, unsigned char *had_flag);

/* player hits monster m (by bumping into its cell). No movement occurs during this action. */
static void player_attack(monster_t *m, zone_t *pr)
{
    signed int bonus = str_bonus_hit(effective_strength());
    unsigned char had_weapon = 0;
    bonus += 2 * ring_worn_effect(RING_EFFECT_DEXTERITY);
    bonus += g_dex_bonus; /* permanent bonus from the Potion of Dexterity */
    if (g_player_class == CLASS_STRONGMAN) bonus += 1 + (signed int)(pstat.level / 2);
    else if (g_player_class == CLASS_INTELLECTUAL) bonus -= 1;
    m->awake = 1; /* point-blank attack will wake up anyone */
    {
        unsigned char has_l = 0, has_r = 0;
        if (g_eq_hand_l >= 0) {
            inv_item_t *hl = &g_inventory[g_eq_hand_l];
            if (hl->type == ITEM_WEAPON_MELEE) { has_l = 1; bonus += hl->mod1; }
        }
        if (g_eq_hand_r >= 0) {
            inv_item_t *hr = &g_inventory[g_eq_hand_r];
            if (hr->type == ITEM_WEAPON_MELEE) { has_r = 1; bonus += hr->mod1; }
        }
        if (has_l && has_r)
            bonus -= (g_player_class == CLASS_STRONGMAN ? 1 : 2); /* dual-wield to-hit penalty, smaller for the strongman */
    }
    bonus -= (signed int)(armor_pv() / 3); /* heavy armor slightly hinders your own attacks */
    if (roll_to_hit(bonus, m->defense)) {
        signed int dmg = str_bonus_dmg(effective_strength());
        dmg += hand_weapon_damage(g_eq_hand_l, &had_weapon);
        dmg += hand_weapon_damage(g_eq_hand_r, &had_weapon);
        if (!had_weapon) dmg += 1 + rnd(2); /* with bare hands */
        dmg += 2 * ring_worn_effect(RING_EFFECT_DAMAGE);
        dmg += g_dmg_bonus; /* permanent bonus from Potion of Damage */
        if (dmg < 1) dmg = 1;
        if ((unsigned int)dmg >= m->hp) {
            m->hp = 0;
            m->alive = 0;
            pstat.xp += monster_templates[m->species].exp;
            queue_message_str("You slay the %s!", monster_name(m));
            monster_drop(m);
            draw_cell(m->x, m->y, pr);
            check_level_up();
        } else {
            m->hp = (unsigned char)(m->hp - dmg);
            queue_message_str("You hit the %s", monster_name(m));
        }
    } else {
        queue_message_str("You miss the %s", monster_name(m));
    }
}

/* monster m attacks the player (m is already right next to the player and is moving toward them). */
static void monster_attack(monster_t *m)
{
    g_quiet_turns = 0; /* monster attacks, doesn't matter whether it hits or not */
    if (roll_to_hit((signed int)m->attack, effective_defense())) {
        unsigned char range = (unsigned char)(m->dmg_hi - m->dmg_lo + 1);
        signed int dmg = (signed int)m->dmg_lo + rnd(range);
        dmg -= (signed int)armor_pv(); /* PV absorbs damage directly */
        if (dmg < 0) dmg = 0;
        if ((unsigned int)dmg >= pstat.hp && dmg > 0) {
            pstat.hp = 0;
            queue_message_str("%s hits hard!", monster_name(m));
            {
                char cause[24];
                sprintf(cause, "a %s", monster_name(m));
                player_die(cause);
            }
        } else if (dmg == 0) {
            queue_message_str("The %s hits, but your armor absorbs it", monster_name(m));
        } else {
            pstat.hp = (unsigned char)(pstat.hp - dmg);
            queue_message_str("%s hits you", monster_name(m));
        }
    } else {
        queue_message_str("%s misses you", monster_name(m));
    }
}

void render_full(unsigned char depth)
{
    unsigned char x, y;
    zone_t *pr = lit_room_at(player.x, player.y); /* once per frame */
    for (y = 0; y < MAP_H; y++)
        for (x = 0; x < MAP_W; x++)
            draw_cell(x, y, pr);
    draw_status(depth);
}

/* simple pursuit: a sleeping monster may wake up if it sees the player
 * (1 in 3 chance per turn); an awake one steps toward the player (diagonals
 * included) if the target cell is free, or attacks if it is already adjacent.
 * Paralyzed monsters and the merchant are skipped. */
static void process_monsters(void)
{
    unsigned char i;
    zone_t *pr = lit_room_at(player.x, player.y);

    for (i = 0; i < g_monster_count; i++) {
        monster_t *m = &g_monsters[i];
        if (!m->alive)		continue;
        if (m->paralyzed)	continue; /* paralyzed by a wand - cannot move or attack */
        if (m->is_merchant)	continue; /* merchant is calm and not pushy */

        if (!m->awake) {
            if (cell_visible(m->x, m->y, pr) && rnd(3) == 0)
                m->awake = 1;
            else
                continue;
        }

        {
            signed char sx = 0, sy = 0;
            unsigned char nx, ny;
            if (m->x < player.x) sx = 1; else if (m->x > player.x) sx = -1;
            if (m->y < player.y) sy = 1; else if (m->y > player.y) sy = -1;
            if (sx == 0 && sy == 0) continue; /* standing on the player's tile - this shouldn't happen */
            nx = (unsigned char)(m->x + sx);
            ny = (unsigned char)(m->y + sy);
            if (nx == player.x && ny == player.y) {
                monster_attack(m);
                continue;
            }
            if (is_open_tile((tile_t)g_map[ny][nx]) && !monster_at(nx, ny)) {
                unsigned char ox = m->x, oy = m->y;
                m->x = nx;
                m->y = ny;
                draw_cell(ox, oy, pr);
                draw_cell(m->x, m->y, pr);
            }
        }
    }
}

/* wandering monster: Occasionally appears in a room the player has already
 * seen, immediately awake. Monsters cannot tell whether the player is
 * standing still or not - the chance of encounter is constant. */
static void try_wander_spawn(unsigned char depth)
{
    unsigned char chance = 120;
    unsigned char mx, my, species;

    if (g_monster_count >= MAX_MONSTERS) return;
    if (rnd(chance) != 0) return;
    if (!pick_random_seen_open_cell(&mx, &my)) return;

    {
        monster_t *nm = &g_monsters[g_monster_count];
        nm->x = mx;
        nm->y = my;
        nm->awake = 1; /* wandering monsters are already active */
        nm->alive = 1;
        species = pick_species_for_depth(depth);
        monster_init_stats(nm, species);
    }
    g_monster_count++;
}

/* loading overlay files (each overlay replaces
 * the previous one in the same memory window). */
extern void _OVERLAY1_LOAD__[], _OVERLAY1_SIZE__[];
extern void _OVERLAY2_LOAD__[], _OVERLAY2_SIZE__[];
extern void _OVERLAY3_LOAD__[], _OVERLAY3_SIZE__[];

/* load the entire overlay file. The 'read()' function on a real ProDOS
 * disk does not guarantee that the entire requested amount will be read
 * in a single call - for small overlays, this might have gone unnoticed,
 * but for the largest overlay of that time (the map generator, ~7 KB),
 * it once failed to read to the end, leaving the bottom of the window filled
 * with the old contents of the previous overlay. We read in a loop until
 * we've collected all the bytes or encounter an error or the end of the file. */
static unsigned char load_overlay_file(const char *name, void *addr, unsigned size)
{
    unsigned char *p = (unsigned char *)addr;
    unsigned got_total = 0;
    int f;
    char buf[40];

    f = open(name, O_RDONLY);
    if (f == -1) {
        char cwd[24];
        if (!getcwd(cwd, sizeof(cwd))) strcpy(cwd, "?");
        if (cwd[0] == '\0') {
            /* prefix is missing (see 'ensure_prefix' in 'main()') - this
			 * is almost certainly the cause of the ENOENT error, not a missing
			 * file. Let's try to restore the prefix and read it one more time
			 * before giving up and displaying a diagnostic message to the player. */
            ensure_prefix();
            f = open(name, O_RDONLY);
        }
        if (f == -1) {
            sprintf(buf, "%s oe=%u en=%d heap=%u", name, (unsigned)_oserror, errno, (unsigned)_heapmaxavail());
            queue_message(buf);
            sprintf(buf, "cwd=%s", cwd);
            queue_message(buf);
            return 0;
        }
    }
    while (got_total < size) {
        int got = read(f, p + got_total, size - got_total);
        if (got <= 0) {
            close(f);
            sprintf(buf, "Ov:%s(%u)", name, (unsigned)_oserror);
            queue_message(buf);
            return 0;
        }
        got_total += (unsigned)got;
    }
    close(f);
    return 1;
}

static unsigned char load_view_overlay(void)
{
    return load_overlay_file("RASC.1", _OVERLAY1_LOAD__, (unsigned)_OVERLAY1_SIZE__);
}

static unsigned char load_hands_overlay(void)
{
    return load_overlay_file("RASC.2", _OVERLAY2_LOAD__, (unsigned)_OVERLAY2_SIZE__);
}

static unsigned char load_use_overlay(void)
{
    return load_overlay_file("RASC.3", _OVERLAY3_LOAD__, (unsigned)_OVERLAY3_SIZE__);
}

extern void _OVERLAY7_LOAD__[], _OVERLAY7_SIZE__[];

static unsigned char load_combat_overlay(void)
{
    return load_overlay_file("RASC.7", _OVERLAY7_LOAD__, (unsigned)_OVERLAY7_SIZE__);
}

extern void _OVERLAY8_LOAD__[], _OVERLAY8_SIZE__[];

unsigned char load_shapes_overlay(void)
{
    return load_overlay_file("RASC.8", _OVERLAY8_LOAD__, (unsigned)_OVERLAY8_SIZE__);
}

extern void _OVERLAY9_LOAD__[], _OVERLAY9_SIZE__[];

unsigned char load_connect_overlay(void)
{
    return load_overlay_file("RASC.9", _OVERLAY9_LOAD__, (unsigned)_OVERLAY9_SIZE__);
}

extern void _OVERLAY5_LOAD__[], _OVERLAY5_SIZE__[];

static unsigned char load_start_overlay(void)
{
    return load_overlay_file("RASC.5", _OVERLAY5_LOAD__, (unsigned)_OVERLAY5_SIZE__);
}

extern void _OVERLAY4_LOAD__[], _OVERLAY4_SIZE__[];

static unsigned char load_end_overlay(void)
{
    return load_overlay_file("RASC.4", _OVERLAY4_LOAD__, (unsigned)_OVERLAY4_SIZE__);
}

extern void _OVERLAY6_LOAD__[], _OVERLAY6_SIZE__[];

static unsigned char load_spawn_overlay(void)
{
    return load_overlay_file("RASC.6", _OVERLAY6_LOAD__, (unsigned)_OVERLAY6_SIZE__);
}

/* wrapper that repeats the process at the full-load level
 * (open-read-close), rather than inside the 'open()' function -
 * this is safer and avoids the risk of a heap leak. */
static unsigned char load_spawn_overlay_safe(void)
{
    return load_overlay_retry(load_spawn_overlay);
}

/* OVERLAY10 contains code that is rarely triggered (periodic
 * activation of the cursed Poison/Sleep rings) - so as not to waste space
 * in memory all the time. Need to build it using the custom config file
 * apple2enh-overlay-10.cfg, not the standard apple2enh-overlay.cfg. */
extern void _OVERLAY10_LOAD__[], _OVERLAY10_SIZE__[];

static unsigned char load_overlay10(void)
{
    return load_overlay_file("RASC.10", _OVERLAY10_LOAD__, (unsigned)_OVERLAY10_SIZE__);
}

static unsigned char load_overlay10_safe(void)
{
    return load_overlay_retry(load_overlay10);
}

void apply_ring_poison_trigger(void); /* defined in rare_ov10.c */
void apply_ring_sleep_trigger(unsigned char depth); /* defined in rare_ov10.c */

typedef enum { MOVE_BLOCKED, MOVE_ATTACKED, MOVE_STEPPED } move_result_t;

/* one step in the direction dx, dy - used for both normal movement
 * and running (run_direction). It does not itself count the move or
 * move monsters - that is done by the calling code. */
static move_result_t try_move(signed char dx, signed char dy)
{
    signed int nx = (signed int)player.x + dx;
    signed int ny = (signed int)player.y + dy;

    if (!(nx >= 0 && nx < MAP_W && ny >= 0 && ny < MAP_H &&
          is_open_tile((tile_t)g_map[ny][nx])))
        return MOVE_BLOCKED;

    {
        monster_t *target = monster_at((unsigned char)nx, (unsigned char)ny);
        if (target) {
            if (target->is_merchant) return MOVE_BLOCKED;
            player_attack(target, lit_room_at(player.x, player.y));
            return MOVE_ATTACKED;
        }
    }
    {
        unsigned char ox = player.x, oy = player.y;
        zone_t *old_room = lit_room_at(ox, oy);
        zone_t *new_room;

        player.x = (unsigned char)nx;
        player.y = (unsigned char)ny;
        new_room = lit_room_at(player.x, player.y);
        {
            tile_t here = (tile_t)g_map[player.y][player.x];
            if (here == T_TRAP_HIDDEN || here == T_TRAP_REVEALED) {
                signed int dmg = 3 + rnd(3); /* 3..5 */
                if (here == T_TRAP_HIDDEN) {
                    g_map[player.y][player.x] = T_TRAP_REVEALED;
                    draw_cell(player.x, player.y, new_room);
                }
                queue_message("You trigger a trap!");
                if ((unsigned int)dmg >= pstat.hp) { pstat.hp = 0; player_die("a trap"); }
                else pstat.hp = (unsigned char)(pstat.hp - dmg);
            }
        }
        try_pickup();

        if (old_room != new_room) {
            if (old_room)
                redraw_region(old_room->pos.x, old_room->pos.y,
                    (unsigned char)(old_room->pos.x + old_room->size.x - 1),
                    (unsigned char)(old_room->pos.y + old_room->size.y - 1));
            if (new_room)
                redraw_region(new_room->pos.x, new_room->pos.y,
                    (unsigned char)(new_room->pos.x + new_room->size.x - 1),
                    (unsigned char)(new_room->pos.y + new_room->size.y - 1));
        }
        {
            unsigned char rx0 = (ox < player.x ? ox : player.x);
            unsigned char ry0 = (oy < player.y ? oy : player.y);
            unsigned char rx1 = (ox > player.x ? ox : player.x);
            unsigned char ry1 = (oy > player.y ? oy : player.y);
            rx0 = (rx0 > FOV_RADIUS) ? (unsigned char)(rx0 - FOV_RADIUS) : 0;
            ry0 = (ry0 > FOV_RADIUS) ? (unsigned char)(ry0 - FOV_RADIUS) : 0;
            rx1 = (rx1 + FOV_RADIUS < MAP_W) ? (unsigned char)(rx1 + FOV_RADIUS) : MAP_W - 1;
            ry1 = (ry1 + FOV_RADIUS < MAP_H) ? (unsigned char)(ry1 + FOV_RADIUS) : MAP_H - 1;
            redraw_region(rx0, ry0, rx1, ry1);
        }
    }
    return MOVE_STEPPED;
}

/* search the 8 neighboring cells for secret doors and hidden traps (each
 * found with a 1 in 5 chance per check) - shared by the 's' key and by the
 * free automatic search of the Ring of Searching. Not guaranteed, same as
 * in the original: sometimes you need to press it or wait several times
 * in a row. */
static void search_secret_doors(void)
{
    static const signed char sdx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
    static const signed char sdy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };
    unsigned char sk;
    for (sk = 0; sk < 8; sk++) {
        signed int snx = (signed int)player.x + sdx[sk];
        signed int sny = (signed int)player.y + sdy[sk];
        if (snx < 0 || snx >= MAP_W || sny < 0 || sny >= MAP_H) continue;
        {
            tile_t st = (tile_t)g_map[sny][snx];
            if ((st == T_DOOR_SECRET_H || st == T_DOOR_SECRET_V) && rnd(5) == 0) {
                g_map[sny][snx] = T_DOOR;
                draw_cell((unsigned char)snx, (unsigned char)sny,
                    lit_room_at(player.x, player.y));
            } else if (st == T_TRAP_HIDDEN && rnd(5) == 0) {
                g_map[sny][snx] = T_TRAP_REVEALED;
                draw_cell((unsigned char)snx, (unsigned char)sny,
                    lit_room_at(player.x, player.y));
            }
        }
    }
}

/* remove dead monsters from the array by adjusting g_monster_count - otherwise,
 * after a major battle (for example, in a treasure chamber), the counter remains
 * high until the end of the level, and process_monsters()/monster_at() continue
 * to iterate through long-dead slots every turn/frame. Swap places with the
 * last living monster and decrement the counter - the order of the monsters
 * doesn't matter. It's safe to call this only between turns: within a single
 * turn, the code holds pointers to g_monsters (m->x, etc., after death), and
 * by the time end_turn() begins, they've all been processed and are no longer
 * needed. */
static void compact_monsters(void)
{
    unsigned char i = 0;
    while (i < g_monster_count) {
        if (!g_monsters[i].alive) {
            g_monsters[i] = g_monsters[g_monster_count - 1];
            g_monster_count--;
        } else {
            i++;
        }
    }
}

/* process the consequences of one turn: compact dead monsters, hunger,
 * poison, free searching (Ring of Searching), regeneration, monsters,
 * wandering spawns, status bar. Common for a regular step and for running. */
void end_turn(unsigned char moved, unsigned char depth)
{
    static unsigned char digest_tick = 0;
    (void)moved; /* unused, kept in the signature for all existing call sites */
    compact_monsters();
    g_quiet_turns++;
    {
        unsigned int old_hunger = pstat.hunger;
        unsigned char hungry_now = 1;
        unsigned char hunger_amount = 1;
        if (ring_worn_effect(RING_EFFECT_SLOW_DIGESTION)) {
            digest_tick ^= 1;      /* hunger increases at half the rate - every two turns */
            hungry_now = digest_tick;
        } else if (ring_worn_effect(RING_EFFECT_FAST_DIGESTION)) {
            hunger_amount = 2;     /* hunger increases twice as fast - by 2 per turn */
        }
        if (hungry_now) pstat.hunger = (unsigned int)(pstat.hunger + hunger_amount);
        if (old_hunger < 1000 && pstat.hunger >= 1000)
            queue_message("You are hungry");
        else if (old_hunger < 1500 && pstat.hunger >= 1500)
            queue_message("Weak with hunger");
        else if (old_hunger < 2000 && pstat.hunger >= 2000)
            queue_message("You are starving!");
        if (pstat.hunger >= 2000 && rnd(4) == 0) {
            if (pstat.hp > 3) pstat.hp = (unsigned char)(pstat.hp - 3);
            else player_die("starvation");
        }
    }
    if (g_poison_turns > 0) {
        g_poison_turns--;
        {
            signed int dmg = 1 + rnd(3);
            if ((unsigned int)dmg >= pstat.hp) { pstat.hp = 0; player_die("poison"); }
            else pstat.hp = (unsigned char)(pstat.hp - dmg);
        }
    }
    if (ring_worn_effect(RING_EFFECT_SEARCHING)) search_secret_doors(); /* free, every turn */
    regenerate();
    process_monsters();
    try_wander_spawn(depth);
    draw_status(depth);
}

/* cursed Rings of Poison/Sleep: while worn, they re-trigger with a 1 in 20
 * chance each turn. NOT called from end_turn() itself: end_turn() is called
 * from several overlays, and loading overlay 10 from there would overwrite
 * the memory of the overlay that is currently running. Instead it's called
 * only from resident code in main.c (movement, waiting, searching), so a
 * turn spent in another overlay (scroll/wand/ring use) skips the check -
 * an acceptable trade-off for safety. */
static void check_cursed_ring_triggers(unsigned char depth)
{
    if (g_poison_turns == 0 && ring_worn_effect(RING_EFFECT_POISON) && rnd(20) == 0) {
        if (load_overlay10_safe()) apply_ring_poison_trigger();
    }
    if (!g_ring_sleep_active && ring_worn_effect(RING_EFFECT_SLEEP) && rnd(20) == 0) {
        if (load_overlay10_safe()) apply_ring_sleep_trigger(depth);
    }
}

/* running: keep moving in that direction until you hit an obstacle, encounter
 * a monster or message, or the lit room changes (it's worth looking around). */
static void run_direction(signed char dx, signed char dy, unsigned char depth)
{
    unsigned char steps;
    zone_t *start_room = lit_room_at(player.x, player.y);
    for (steps = 0; steps < 60; steps++) {
        move_result_t r;
        g_msg_shown_this_turn = 0;
        r = try_move(dx, dy);
        if (r != MOVE_STEPPED) { end_turn(0, depth); check_cursed_ring_triggers(depth); break; }
        end_turn(1, depth);
        check_cursed_ring_triggers(depth);
        if (g_msg_shown_this_turn) break;
        {
            zone_t *cur_room = lit_room_at(player.x, player.y);
            if (cur_room != start_room) break;
        }
    }
}

/* RASCAL.SYSTEM is loaded by ProDOS directly, bypassing BASIC.SYSTEM, which
 * is what normally sets the prefix to the boot volume (the ProDOS kernel
 * doesn't). With a direct load the prefix stays empty, and any relative
 * open() (overlays, saves) fails with errno=2 even though the files are on
 * the disk. So we set the prefix ourselves, once, before the first attempt
 * to open anything by a relative path. */
static void ensure_prefix(void)
{
    char cwd[24];
    if (getcwd(cwd, sizeof(cwd)) && cwd[0] != '\0') return; /* prefix already set - leave it */
    chdir(GAME_VOLUME);
}

/* generate a level and populate it - this is common to both starting a new game and
 * taking the stairs (previously, the code was repeated verbatim in both places). */
static void generate_and_populate(unsigned char depth)
{
    level_generate(depth);
    place_traps();
    if (load_spawn_overlay_safe()) {
        place_player();
        spawn_monsters(depth);
        spawn_items(depth);
        if (ring_worn_effect(RING_EFFECT_AGGRAVATE)) {
            unsigned char k;
            for (k = 0; k < g_monster_count; k++) {
                monster_t *mk = &g_monsters[k];
                if (mk->alive) mk->awake = 1;
            }
        }
    }
}

/* general level-entry sequence - resetting step counters, redrawing,
 * announcements about the shop/treasure chest. This was also repeated verbatim (three
 * instances: starting a new game, loading a save, taking the stairs). */
static void finish_entering_level(unsigned char depth)
{
    g_quiet_turns = 0;
    clear_message();
    render_full(depth);
    if (g_level_has_shop) queue_message("You hear the jingle of coins");
    if (g_level_has_treasure) queue_message("You hear distant stomping");
}

/* ring/scroll/wand - they don't need dice_n/dice_s/def1/def2, just the type
   and mod1/mod2 (material/type and, for wands, charges). */
unsigned char give_simple(item_type_t type, signed char mod1, signed char mod2)
{
    return inventory_add(type, mod1, mod2, 0, 0, 0, 0);
}

void give_starting_gear(void); /* defined in rare_ov10.c; class starting kit, called once on a new game */

int main(void)
{
    unsigned char depth;
    char k;
    unsigned char loaded;

    videomode(VIDEOMODE_80COL);
    *(unsigned char *)0xC00F = 0; /* SETALTCH - keep enabled for the entire session; required for MouseText */

    ensure_prefix();

    for (;;) {
        if (load_start_overlay()) loaded = menu_title();
        else loaded = 0;
        if (loaded == 2) {
            if (load_end_overlay()) show_instructions(); /* another overlay - it's safe; we've already exited menu_title() */
            continue;
        }
        if (loaded == 3) {
            if (load_overlay10_safe()) show_hall_of_fame();
            continue;
        }
        break;
    }

    if (!loaded) {
        depth = 1;
        g_max_depth = 1; /* otherwise, the new character would inherit the depth of death from the past */
        memset(g_seen, 0, sizeof(g_seen));
        pstat.hp = pstat.hp_max = 12;
        if (g_player_class == CLASS_STRONGMAN) pstat.strength = 15;
        else if (g_player_class == CLASS_TRICKSTER) pstat.strength = 14;
        else pstat.strength = 13; /* CLASS_INTELLECTUAL */
        pstat.level = 1;
        pstat.xp = 0;
        pstat.defense = 0;
        pstat.hunger = 0;
        g_poison_turns = 0;
        g_inv_count = 0;
        g_eq_armor = g_eq_hand_l = g_eq_hand_r = g_eq_ranged = g_eq_ring_l = g_eq_ring_r = -1;
        if (load_overlay10_safe()) {
            shuffle_kind(potion_effect, potion_identified, NUM_POTION_KINDS);
            shuffle_kind(ring_effect, ring_identified, NUM_RING_KINDS);
            shuffle_kind(wand_effect, wand_identified, NUM_WAND_KINDS);
            shuffle_kind(scroll_title_for, scroll_identified, NUM_SCROLL_KINDS);
            /* starting gear: by class, plus two identical meals for everyone */
            give_starting_gear();
        }
        give_simple(ITEM_FOOD, 0, 0);
        g_inventory[g_inv_count - 1].amount = 2; /* one stack for 2 servings, not two slots */
        generate_and_populate(depth);
    } else {
        depth = g_depth;		/* restored from a saved file */
        g_level_has_shop = 0;	/* not saved/restored - so no announcement after loading */
        g_level_has_treasure = 0;
    }
    finish_entering_level(depth);
    draw_help_hint();

    for (;;) {
        signed char dx = 0, dy = 0;

        k = cgetc();
        clear_message(); /* message from the previous turn lasts exactly until the next action */
        switch (k) {
            case 'g': case CH_CURS_LEFT:  dx = -1; dy =  0; break;
            case 'j': case CH_CURS_RIGHT: dx =  1; dy =  0; break;
            case 'y': case CH_CURS_UP:    dx =  0; dy = -1; break;
            case 'n': case CH_CURS_DOWN:  dx =  0; dy =  1; break;
            case 't': dx = -1; dy = -1; break;
            case 'u': dx =  1; dy = -1; break;
            case 'b': dx = -1; dy =  1; break;
            case 'm': dx =  1; dy =  1; break;
            case 'G': run_direction(-1,  0, depth); continue;
            case 'J': run_direction( 1,  0, depth); continue;
            case 'Y': run_direction( 0, -1, depth); continue;
            case 'N': run_direction( 0,  1, depth); continue;
            case 'T': run_direction(-1, -1, depth); continue;
            case 'U': run_direction( 1, -1, depth); continue;
            case 'B': run_direction(-1,  1, depth); continue;
            case 'M': run_direction( 1,  1, depth); continue;
            case 'h': case 'H':
                end_turn(0, depth);
                check_cursed_ring_triggers(depth);
                continue;
            case 'R':
                show_message(last_message);
                continue;
            case '>':
                if (g_map[player.y][player.x] == T_STAIRS_DOWN) {
                    if (g_stairs_inverted && depth <= 1) {
                        /* reached victory - the ring has been brought to the surface */
                        videomode(VIDEOMODE_40COL); /* otherwise, the text below the image will overlap with the 80-column AUX memory */
                        draw_victory_picture(); /* full-screen low-res graphics via ROM - first */
                        cgetc();
                        *(unsigned char *)0xC051 = 0; /* TEXT - back to text mode */
                        *(unsigned char *)0xC001 = 0; /* 80STOREON - return; otherwise, clrscr() and cprintf will hang in 80-column mode */
                        videomode(VIDEOMODE_80COL);
                        clrscr();
                        if (load_overlay10_safe()) handle_player_win(); /* points + screen + Hall of Fame - one load */
                        videomode(VIDEOMODE_40COL); /* 80 columns - hardware state, survives exit to BASIC */
                        exit(0); /* game doesn't return to the menu - it just closes */
                    }
                    if (g_stairs_inverted) { if (depth > 1) depth--; }
                    else { if (depth < 99) depth++; if (depth > g_max_depth) g_max_depth = depth; }
                    g_depth = depth;
                    memset(g_seen, 0, sizeof(g_seen));
                    generate_and_populate(depth);
                    finish_entering_level(depth);
                }
                continue;
            case 'P': {
                /* to buy from a merchant, use only the explicit key next to them; never
                   do it automatically step by step (that's what broke the last attempt at the store). */
                if (load_overlay10_safe() && merchant_adjacent()) {
                    if (load_shapes_overlay()) menu_shop_buy(depth);
                    else { render_full(depth); draw_help_hint(); }
                } else {
                    queue_message(msg_no_merchant);
                    draw_status(depth);
                }
                continue;
            }
            case 'k': {
                if (load_spawn_overlay_safe()) menu_known(depth);
                continue;
            }
            case 's': {
                search_secret_doors();
                queue_message("Searching...");
                end_turn(0, depth); /* searching does not move the player */
                check_cursed_ring_triggers(depth);
                continue;
            }
            case 'S': {
                /* to sell to a merchant - only by pressing the dedicated key next to it, symmetrical to P. */
                if (load_overlay10_safe() && merchant_adjacent()) {
                    if (load_shapes_overlay()) menu_shop_sell(depth);
                    else { render_full(depth); draw_help_hint(); }
                } else {
                    queue_message(msg_no_merchant);
                    draw_status(depth);
                }
                continue;
            }
            case 'i': case 'I':
                if (load_view_overlay()) menu_backpack(depth);
                continue;
            case '@':
                if (load_view_overlay()) menu_character(depth);
                continue;
            case '?':
                if (load_end_overlay()) menu_help(depth);
                continue;
            case 'L':
                if (load_end_overlay()) menu_legend(depth);
                continue;
            case '!':
                if (load_end_overlay()) show_instructions();
                render_full(depth);
                draw_help_hint();
                continue;
            case 'w':
                if (load_hands_overlay()) menu_wield(depth);
                continue;
            case 'W':
                if (load_hands_overlay()) menu_remove(depth);
                continue;
            case 'd': case 'D':
                if (load_use_overlay()) menu_drop(depth);
                continue;
            case 'r':
                if (load_start_overlay()) menu_read(depth);
                if (g_pending_identify) {
                    g_pending_identify = 0;
                    if (load_spawn_overlay_safe()) menu_identify_target(depth);
                }
                continue;
            case 'e': case 'E':
                if (load_use_overlay()) menu_eat(depth);
                continue;
            case 'q':
                if (load_use_overlay()) menu_quaff(depth);
                continue;
            case 'z': case 'Z':
                if (load_combat_overlay()) menu_zap(depth);
                if (g_pending_identify) {
                    g_pending_identify = 0;
                    if (load_spawn_overlay_safe()) menu_identify_target(depth);
                }
                continue;
            case 'f': case 'F':
                if (load_combat_overlay()) menu_fire(depth);
                continue;
            case 'Q':
                clear_msg_line();
                cputs("Save & quit? (y/n)");
                k = cgetc();
                clear_message();
                if (k == 'y' || k == 'Y') {
                    if (load_start_overlay() && save_game_prompt()) {
                        videomode(VIDEOMODE_40COL);
                        clrscr();
                        return 0;
                    }
                    /* slot selection was canceled or saving failed -
                       under no circumstances should we exit without saving (permadeath) */
                    render_full(depth);
                    draw_help_hint();
                } else {
                    draw_status(depth);
                }
                continue;
            default:
                continue;
        }

        {
            move_result_t r = try_move(dx, dy);
            end_turn(r == MOVE_STEPPED, depth);
            check_cursed_ring_triggers(depth);
        }
    }
}
