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

#ifndef GAME_SHARED_H
#define GAME_SHARED_H

#include "level.h"

/* player */
typedef struct { unsigned char x, y; } pt_t;
extern pt_t player;
extern unsigned char g_depth;
extern unsigned char g_stairs_inverted;		/* took the ring - the stairs now lead upstairs */
extern unsigned char g_has_signet;			/* carrying the Ring of the Apple Signet right now */
extern unsigned char g_max_depth;			/* the deepest depth reached - for the victory screen */
extern unsigned char g_level_has_shop;		/* if level has merchant */
extern unsigned char g_level_has_treasure;	/* if level has treasure room */
extern unsigned char g_poison_turns;
extern unsigned char g_ring_sleep_active;	/* recursion check - required from rare_ov10.c */
extern unsigned char g_ring_found_depth;	/* depth at which the ring was found - needed from rare_ov10.c for points */
extern const unsigned char armor_ac[7];		/* needed from rare_ov10.c (give_armor) */
extern unsigned char g_dex_bonus;			/* permanent bonus to hit from the Potion of Dexterity */
extern unsigned char g_dmg_bonus;			/* permanent damage bonus from the Potion of Damage */
extern unsigned char g_pending_identify;
#define CLASS_STRONGMAN 0
#define CLASS_TRICKSTER 1
#define CLASS_INTELLECTUAL 2
extern unsigned char g_player_class;
extern unsigned char g_seen[MAP_H][MAP_W / 8];	/* per bit per cell - see is_seen()/set_seen() */
unsigned char is_seen(unsigned char x, unsigned char y);
void set_seen(unsigned char x, unsigned char y);

typedef struct {
    unsigned char hp, hp_max;
    unsigned char strength;
    unsigned char level;
    unsigned long xp;
    unsigned char defense;
    unsigned int hunger; /* hunger. increases with each move; see the thresholds in main.c */
} player_stats_t;

extern player_stats_t pstat;
extern char player_name[16];
unsigned long xp_threshold(unsigned char level);
unsigned char effective_defense(void);
signed char armor_pv(void);
signed char str_bonus_hit(unsigned char str);
signed char str_bonus_dmg(unsigned char str);

/* menu window (see main.c: open_window/close_window) */
#define WIN_X0 10
#define WIN_X1 69
#define WIN_Y0 5
#define WIN_Y1 18
#define WIN_LCOL (WIN_X0 + 2)
#define WIN_RCOL (WIN_X0 + 32)

void open_window(const char *title, unsigned char rows);
void close_window(unsigned char depth);
/* draw_credit_line() - is now static in menus_start.c; this is the only place it is called */
void clear_message(void);
void queue_message(const char *msg);
void queue_message_str(const char *fmt, const char *s);
void draw_status(unsigned char depth);
void draw_help_hint(void);
void end_turn(unsigned char moved, unsigned char depth);
void inventory_remove(unsigned char idx);
void put(unsigned char y, unsigned char x, tile_t t);
tile_t get(unsigned char y, unsigned char x);
unsigned char load_shapes_overlay(void);
unsigned char load_overlay_retry(unsigned char (*loader)(void));
unsigned char load_connect_overlay(void);
void draw_maze(zone_t *z);
void draw_room(zone_t *z);
void build_connectivity(unsigned char depth);
unsigned int get_gold_total(void);
void render_full(unsigned char depth);
void draw_cell(unsigned char x, unsigned char y, zone_t *pr);
void put_mousetext(char code); /* resident, displays a MouseText character (corridor on the map, window frames, traps) */
void check_level_up(void);
zone_t *room_at(unsigned char x, unsigned char y);
zone_t *lit_room_at(unsigned char x, unsigned char y);

/* items */
#define MAX_GROUND_ITEMS 16
#define MAX_INVENTORY 18

typedef enum {
    ITEM_GOLD,
    ITEM_WEAPON_MELEE,
    ITEM_WEAPON_RANGED,
    ITEM_ARMOR,
    ITEM_SCROLL_MAPPING,
    ITEM_SCROLL_SLEEP,
    ITEM_SCROLL_CREATE_FOOD,
    ITEM_SCROLL_AGGRAVATE,
    ITEM_SCROLL_IDENTIFY,
    ITEM_SCROLL_REMOVE_CURSE,
    ITEM_FOOD,
    ITEM_POTION,
    ITEM_SHIELD,
    ITEM_RING,
    ITEM_WAND,
    ITEM_AMMO,
    ITEM_QUEST_RING, /* Ring of the Apple Signet - the game's objective; see the ending in main.c */
    ITEM_TREASURE /* pure treasure - for sale only, no features */
} item_type_t;

typedef struct {
    unsigned char x, y;
    unsigned char active;
    unsigned char type;
    unsigned char amount;
    signed char mod1;
    signed char mod2;
    signed char def1;
    signed char def2;
    unsigned char dice_n, dice_s;
} ground_item_t;

extern ground_item_t g_shop_offer[5];
extern unsigned char g_shop_price[5]; /* maximum of 105 for the most expensive items */

void roll_random_item(ground_item_t *gi, unsigned char depth); /* resident (main.c) */
unsigned char inventory_add(item_type_t type, signed char mod1, signed char mod2,
                             unsigned char dice_n, unsigned char dice_s,
                             signed char def1, signed char def2); /* resident */
unsigned char give_simple(item_type_t type, signed char mod1, signed char mod2); /* resident, requires try_pickup and rare_ov10.c */
unsigned char item_price(item_type_t type, signed char mod1, unsigned char dice_s, signed char def2); /* resident, common for both buying and selling */
unsigned char find_or_add_stack(item_type_t type, signed char kind); /* resident, common for selection and purchase */
unsigned char weighted_pick(const unsigned char *weights, unsigned char n); /* resident, generally rare across all categories */
unsigned char find_free_ground_slot(void); /* resident, reuses freed floor slots */
unsigned int read_amount(unsigned int max); /* Resident, enter the quantity for a partial discharge */

typedef struct {
    unsigned char type;
    unsigned int amount;
    signed char mod1;
    signed char mod2;
    signed char def1;
    signed char def2;
    unsigned char dice_n, dice_s;
} inv_item_t;

extern ground_item_t g_ground_items[MAX_GROUND_ITEMS];
extern unsigned char g_ground_count;
extern inv_item_t g_inventory[MAX_INVENTORY];
extern unsigned char g_inv_count;
extern signed char g_eq_armor;
extern signed char g_eq_hand_l, g_eq_hand_r;
extern signed char g_eq_ranged;
extern signed char g_eq_ring_l, g_eq_ring_r;

/* potions: appearance (color) is randomized based on the actual effect once per game */
#define NUM_POTION_KINDS 9
#define POTION_EFFECT_HEALING 0
#define POTION_EFFECT_STRENGTH 1
#define POTION_EFFECT_POISON 2
#define POTION_EFFECT_WATER 3      /* +1 HP */
#define POTION_EFFECT_DIGESTION 4  /* like 1 food ration */
#define POTION_EFFECT_DEXTERITY 5  /* +1 to hit permanently (melee and ranged combat) */
#define POTION_EFFECT_DAMAGE 6     /* +1 to damage permanently (melee and ranged) */
#define POTION_EFFECT_BEER 7       /* +2 HP - zajecarsko Beer */
#define POTION_EFFECT_WINE 8       /* +3 HP - rakija */
extern unsigned char potion_effect[NUM_POTION_KINDS];
extern unsigned char potion_identified[NUM_POTION_KINDS];
const char *potion_display_name(unsigned char kind);
#define NUM_AMMO_KINDS 3
#define AMMO_NORMAL  0
#define AMMO_QUALITY 1
#define AMMO_MAGIC   2
const char *ammo_display_name(unsigned char kind);
signed char ammo_damage_bonus(unsigned char kind);
void apply_potion_effect(unsigned char kind); /* menus_action2.c, OVERLAY3 - only the part from there is needed */
void player_die(const char *cause); /* resident (main.c) */

/* Scrolls: The scroll itself (Mapping/Sleep/...) is the effect itself (there is no
 * separate 'material' layer, as with potions), but its name isn't
 * written out in plain text until it has been read at least once - instead,
 * it displays a meaningless title that is randomly shuffled once per game. */
#define NUM_SCROLL_KINDS 6 /* Mapping, Sleep, Create Food, Aggravate, Identify, Remove Curse - by the number of ITEM_SCROLL_* */
extern unsigned char scroll_identified[NUM_SCROLL_KINDS];
extern unsigned char scroll_title_for[NUM_SCROLL_KINDS];

/* Rings: These can also be identified by their appearance (material), but unlike
 * potions, their effect lasts as long as the ring is worn. */
#define NUM_RING_KINDS 12
#define RING_EFFECT_PROTECTION 0
#define RING_EFFECT_REGENERATION 1
#define RING_EFFECT_STRENGTH 2
#define RING_EFFECT_NONE 3				/* no effect */
#define RING_EFFECT_SLOW_DIGESTION 4
#define RING_EFFECT_SEARCHING 5
#define RING_EFFECT_DEXTERITY 6
#define RING_EFFECT_DAMAGE 7
#define RING_EFFECT_POISON 8			/* harmful, applies poison when equipped */
#define RING_EFFECT_SLEEP 9				/* harmful, puts to sleep randomly */
#define RING_EFFECT_FAST_DIGESTION 10	/* harmful, hunger is growing twice as fast */
#define RING_EFFECT_AGGRAVATE 11		/* harmful, wakes up all monsters when entering the level */
extern unsigned char ring_effect[NUM_RING_KINDS];
extern unsigned char ring_identified[NUM_RING_KINDS];
const char *ring_display_name(unsigned char kind);
void ring_name_str(char *buf, unsigned char kind, signed char mod2, unsigned char force_reveal); /* resident (LC) - ring name + CURSED marker */
unsigned char ring_worn_effect(unsigned char effect); /* 1 if such a ring is currently being worn (on either hand) */
unsigned char effective_strength(void);

/* magic wands: Identified by their material, just like potions and rings. They are consumed when used. */
#define NUM_WAND_KINDS 8
#define WAND_EFFECT_MISSILE 0
#define WAND_EFFECT_PARALYZE 1
#define WAND_EFFECT_DRAIN_LIFE 2	/* deal damage to the monster + heal yourself for the same amount */
#define WAND_EFFECT_IDENTIFY 3		/* like a scroll of identification */
#define WAND_EFFECT_SLEEP 4			/* puts the monster to sleep, set 'awake=0' */
#define WAND_EFFECT_FORCE 5			/* same as Missile, but damage x1.5 */
#define WAND_EFFECT_POWER 6			/* same as Missile, but damage x2 */
#define WAND_EFFECT_RUIN 7			/* same as Missile, but damage x4 */
extern unsigned char wand_effect[NUM_WAND_KINDS];
extern unsigned char wand_identified[NUM_WAND_KINDS];

#define NUM_TREASURE_KINDS 3 /* small gem, large gem, crystal set, no need to identify */
const char *treasure_display_name(unsigned char kind);
const char *wand_display_name(unsigned char kind);

const char *item_name(item_type_t t, unsigned char dice_n, unsigned char dice_s);
void item_stat_str(char *buf, item_type_t t, signed char mod1, signed char mod2,
                    unsigned char dn, unsigned char ds, signed char def1, signed char def2);
void item_stat_str_inv(char *buf, unsigned char idx); /* resident */
char read_choice(void);
unsigned char page_size(unsigned char count, unsigned char per_page, unsigned char page); /* resident (LC), general arithmetic of page size */
unsigned char read_page_nav(unsigned char has_more, unsigned char empty, char *out_sel); /* resident (LC), common to menus with alphabetic selection */
void read_page_continue(unsigned char has_more); /* resident (LC), common for read-only menus */
unsigned char slot_is_cursed(signed char slot); /* resident (LC), Is the equipment slot cursed */
void uncurse_slot(signed char slot); /* resident (LC), remove the curse from one slot */
unsigned char sleep_nearby_monsters(unsigned char px, unsigned char py); /* resident (LC), put adjacent monsters to sleep, 50% each */
void remove_curse_scroll_effect(unsigned char depth); /* resident (LC), the full effect of the scroll to remove the curse */
ground_item_t *item_at(unsigned char x, unsigned char y);

/* monsters */
#define MAX_MONSTERS 12

typedef struct {
    unsigned char x, y;
    unsigned char awake;
    unsigned char alive;
    unsigned char hp;
    signed char attack;
    unsigned char defense;
    unsigned char dmg_lo, dmg_hi;
    unsigned char paralyzed;	/* struck by the paralysis wand: unable to act, but alive */
    unsigned char is_merchant;	/* Merchant: does not attack, takes no part in combat, immobile */
    unsigned char species;		/* index in monster_templates: 0xFF for the merchant (no appearance) */
} monster_t;

extern monster_t g_monsters[MAX_MONSTERS];
extern unsigned char g_monster_count;

/* Fixed list of monster types: statistics (except for HP, which is in
 * a range) are not calculated using a formula. Resident (main.c),
 * since it's needed both from spawn.c (OVERLAY6, selection upon spawn) and from
 * menus_action3.c (OVERLAY7, damage/messages for ranged combat). */
#define NUM_MONSTER_SPECIES 12
typedef struct {
    char letter;
    const char *name;
    unsigned char hp_lo, hp_hi;
    signed char attack;
    unsigned char defense;
    unsigned char dmg_lo, dmg_hi;
    unsigned char depth_lo, depth_hi;
    unsigned char chance_to_drop;
    unsigned int exp;
} monster_template_t;
extern const monster_template_t monster_templates[NUM_MONSTER_SPECIES];
const char *monster_name(monster_t *m); /* resident, monster_templates[m->species].name */
unsigned char is_two_handed_weapon(unsigned char dice_n, unsigned char dice_s);
unsigned char right_hand_two_handed(void);
void monster_drop(monster_t *m); /* resident, also needed from menus_action3.c (wands) */
unsigned char pick_species_for_depth(unsigned char depth); /* resident, also needed from spawn.c */
monster_t *monster_at(unsigned char x, unsigned char y);
unsigned char is_open_tile(tile_t t);
unsigned char roll_to_hit(signed int attacker_bonus, unsigned char defense);
void monster_init_stats(monster_t *m, unsigned char species); /* species has already been filtered by depth in the calling code */

/* Level preparation functions located in the generator overlay (spawn.c,
 * OVERLAY6): these are only needed immediately after level_generate, in the same slot. */
void place_player(void);
void spawn_monsters(unsigned char depth);
void spawn_items(unsigned char depth);

/* Menu screen functions located in the overlay (see menus_*.c) */
void menu_backpack(unsigned char depth);
void menu_character(unsigned char depth);
void menu_help(unsigned char depth);			/* menus_end.c, OVERLAY4 */
void menu_legend(unsigned char depth);			/* menus_end.c, OVERLAY4, map icon legend */
void menu_wield(unsigned char depth);
void menu_remove(unsigned char depth);
void menu_drop(unsigned char depth);
void menu_read(unsigned char depth);			/* menus_start.c, OVERLAY5 */
unsigned char is_scroll(item_type_t t);			/* resident, main.c */
void menu_identify_target(unsigned char depth);	/* spawn.c, OVERLAY6 */
void menu_eat(unsigned char depth);
void menu_quaff(unsigned char depth);
void menu_shop_buy(unsigned char depth);		/* level_shapes.c, OVERLAY8 */
void menu_shop_sell(unsigned char depth);		/* level_shapes.c, OVERLAY8 */
void menu_known(unsigned char depth);			/* spawn.c, OVERLAY6 */
void menu_zap(unsigned char depth);
void menu_fire(unsigned char depth);
unsigned char menu_title(void);
void show_instructions(void);					/* menus_end.c, OVERLAY4, Help & Credits */
unsigned char save_game_prompt(void);
void show_death_screen(const char *cause);
void show_win_screen(void);
void draw_victory_picture(void);				/* victory_picture_asm.s */

#endif
