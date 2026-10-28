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

/* Item menu screens: compiled as a separate overlay slot (OVERLAY3) 
 * and loaded from disk on demand (see main.c: load_menu_overlay). */

#include <conio.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game_shared.h"
#include "rng.h"

/* whether inventory item idx is equipped in any equipment slot - a local helper (there is
 * a similar one in level_shapes.c/menus_action1.c; there's no point in creating a global one). */
static unsigned char is_equipped(unsigned char idx)
{
    return (unsigned char)g_eq_armor == idx || (unsigned char)g_eq_hand_l == idx ||
           (unsigned char)g_eq_hand_r == idx || (unsigned char)g_eq_ranged == idx ||
           (unsigned char)g_eq_ring_l == idx || (unsigned char)g_eq_ring_r == idx;
}

/* General name of the item to be displayed (excluding quantity/charges -
 * the caller adds these themselves). Writes to a buffer (requires at least 34 bytes) -
 * it used to return a pointer, but the ring needs a CURSED marker, which is appended
 * dynamically. The function is only needed here (drop list + drop message). */
static void item_display_name(char *buf, item_type_t type, signed char mod1, signed char mod2,
                               unsigned char dice_n, unsigned char dice_s)
{
    switch (type) {
        case ITEM_GOLD: strcpy(buf, "Gold"); return;
        case ITEM_AMMO: strcpy(buf, ammo_display_name((unsigned char)mod1)); return;
        case ITEM_POTION: strcpy(buf, potion_display_name((unsigned char)mod1)); return;
        case ITEM_RING: ring_name_str(buf, (unsigned char)mod1, mod2, 0); return;
        case ITEM_WAND: strcpy(buf, wand_display_name((unsigned char)mod1)); return;
        case ITEM_TREASURE: strcpy(buf, treasure_display_name((unsigned char)mod1)); return;
        default: strcpy(buf, item_name(type, dice_n, dice_s)); return;
    }
}

/* d - drop item from backpack (cannot drop a worn item) */
void menu_drop(unsigned char depth)
{
    unsigned char count = 0, i;
    unsigned char per_page = (unsigned char)(WIN_Y1 - WIN_Y0 - 1); /* one line shorter for the navigation hint */
    unsigned char page = 0;
    char sel = 0;
    for (i = 0; i < g_inv_count; i++) if (!is_equipped(i)) count++;

    for (;;) {
        unsigned char row, idx_on_page = 0, shown = 0, has_more = 0;
        unsigned char page_start = (unsigned char)(page * per_page);
        unsigned char page_count = page_size(count, per_page, page);
        unsigned char nav;
        open_window("Drop which item?", page_count ? page_count : 1);
        row = (unsigned char)(WIN_Y0 + 1);
        for (i = 0; i < g_inv_count; i++) {
            char stat[24];
            inv_item_t *it;
            if (is_equipped(i)) continue;
            if (idx_on_page < page_start) { idx_on_page++; continue; }
            if (shown >= per_page) { has_more = 1; break; }
            gotoxy(WIN_LCOL, row++);
            it = &g_inventory[i];
            if (it->type == ITEM_GOLD || it->type == ITEM_FOOD || it->type == ITEM_AMMO) {
                char nm[34];
                item_display_name(nm, (item_type_t)it->type, it->mod1, it->mod2, it->dice_n, it->dice_s);
                cprintf("%c) %s (%u)", (char)('a' + i), nm, it->amount);
            } else if (it->type == ITEM_POTION || it->type == ITEM_RING || it->type == ITEM_TREASURE) {
                char nm[34];
                item_display_name(nm, (item_type_t)it->type, it->mod1, it->mod2, it->dice_n, it->dice_s);
                cprintf("%c) %s", (char)('a' + i), nm);
            } else if (it->type == ITEM_WAND) {
                if (wand_identified[(unsigned char)it->mod1])
                    cprintf("%c) %s (%u)", (char)('a' + i), wand_display_name((unsigned char)it->mod1), (unsigned)it->mod2);
                else
                    cprintf("%c) %s", (char)('a' + i), wand_display_name((unsigned char)it->mod1));
            } else {
                item_stat_str_inv(stat, i);
                cprintf("%c) %s %s", (char)('a' + i), item_name((item_type_t)it->type, it->dice_n, it->dice_s), stat);
            }
            idx_on_page++;
            shown++;
        }
        if (count == 0) { gotoxy(WIN_LCOL, row); cputs("Backpack empty"); }

        nav = read_page_nav(has_more, count == 0, &sel);
        if (nav == 0) { close_window(depth); return; }
        if (nav == 1) { page++; continue; }
        close_window(depth);
        break;
    }
    {
        if (sel >= 'a' && (unsigned char)(sel - 'a') < g_inv_count && !is_equipped((unsigned char)(sel - 'a'))) {
            unsigned char idx = (unsigned char)(sel - 'a');
            if (item_at(player.x, player.y)) {
                queue_message("Something is already here");
            } else {
                unsigned char stackable = (g_inventory[idx].type == ITEM_GOLD ||
                                           g_inventory[idx].type == ITEM_FOOD ||
                                           g_inventory[idx].type == ITEM_AMMO);
                unsigned int full = g_inventory[idx].amount;
                unsigned int qty = full;
                if (stackable && full > 1) {
                    unsigned int cap = (full > 255) ? 255 : full; /* floor stores the amount in 1 byte */
                    gotoxy(0, 0);
                    cclear(80);
                    gotoxy(0, 0);
                    cprintf("Drop how many (1-%u)? ", cap);
                    qty = read_amount(cap);
                    clear_message();
                    if (qty == 0) {
                        queue_message("Never mind");
                        draw_status(depth);
                        return;
                    }
                }
                {
                    unsigned char slot = find_free_ground_slot();
                    if (slot != 0xFF) {
                        ground_item_t *gi = &g_ground_items[slot];
                        inv_item_t *src = &g_inventory[idx];
                        gi->x = player.x; gi->y = player.y; gi->active = 1;
                        gi->type = src->type;
                        gi->amount = (unsigned char)qty;
                        gi->mod1 = src->mod1;
                        gi->mod2 = src->mod2;
                        gi->def1 = src->def1;
                        gi->def2 = src->def2;
                        gi->dice_n = src->dice_n;
                        gi->dice_s = src->dice_s;
                        if (src->type == ITEM_GOLD || src->type == ITEM_FOOD || src->type == ITEM_AMMO) {
                            char buf[48], nm[34];
                            item_display_name(nm, (item_type_t)src->type, src->mod1,
                                src->mod2, src->dice_n, src->dice_s);
                            sprintf(buf, "You drop %u %s", qty, nm);
                            queue_message(buf);
                        } else {
                            char nm[34];
                            item_display_name(nm, (item_type_t)src->type, src->mod1,
                                src->mod2, src->dice_n, src->dice_s);
                            queue_message_str("You drop the %s", nm);
                        }
                        if (qty >= full) {
                            inventory_remove(idx);
                        } else {
                            src->amount = (unsigned int)(full - qty);
                        }
                        draw_cell(player.x, player.y, lit_room_at(player.x, player.y));
                    } else {
                        queue_message("The floor here is too cluttered already");
                    }
                }
            }
        }
    }
    draw_status(depth);
}


/* e - eat some food from backpack; take the edge off hunger */
void menu_eat(unsigned char depth)
{
    unsigned char row, i, count = 0;
    for (i = 0; i < g_inv_count; i++)
        if (g_inventory[i].type == ITEM_FOOD) count++;
    open_window("Eat which food?", count ? count : 1);
    row = (unsigned char)(WIN_Y0 + 1);
    for (i = 0; i < g_inv_count; i++) {
        if (g_inventory[i].type == ITEM_FOOD) {
            gotoxy(WIN_LCOL, row++);
            cprintf("%c) %s (%u)", (char)('a' + i),
                item_name((item_type_t)g_inventory[i].type, g_inventory[i].dice_n, g_inventory[i].dice_s), g_inventory[i].amount);
        }
    }
    if (count == 0) { gotoxy(WIN_LCOL, row); cputs("You have no food"); }
    {
        char sel = read_choice();
        if (sel >= 'a' && (unsigned char)(sel - 'a') < g_inv_count &&
            g_inventory[(unsigned char)(sel - 'a')].type == ITEM_FOOD) {
            unsigned char idx = (unsigned char)(sel - 'a');

            if (pstat.hunger > 800) pstat.hunger -= 800; else pstat.hunger = 0;

            g_inventory[idx].amount--; /* eat exactly one ration from the stack */
            if (g_inventory[idx].amount == 0) {
                inventory_remove(idx);
            }

            clear_message();
            close_window(depth);
            queue_message("You eat a food ration. That tastes good");
            draw_status(depth);
        } else {
            clear_message();
            close_window(depth);
        }
    }
}

/* q - drink a potion from backpack. Apply the potion's effect based on its ACTUAL (already
 * identified by 'kind') value; at the same time, mark this color as recognized. */
void apply_potion_effect(unsigned char kind)
{
    unsigned char effect;
    if (kind >= NUM_POTION_KINDS) kind = 0;
    effect = potion_effect[kind];
    potion_identified[kind] = 1;
    switch (effect) {
        case POTION_EFFECT_HEALING:
            pstat.hp = pstat.hp_max;
            queue_message("You feel much better");
            break;
        case POTION_EFFECT_STRENGTH:
            pstat.strength++;
            queue_message("You feel stronger");
            break;
        case POTION_EFFECT_WATER:
            if (pstat.hp < pstat.hp_max) pstat.hp++;
            queue_message("It was just water");
            break;
        case POTION_EFFECT_DIGESTION:
            if (pstat.hunger > 800) pstat.hunger -= 800; else pstat.hunger = 0;
            queue_message("You feel full");
            break;
        case POTION_EFFECT_DEXTERITY:
            g_dex_bonus++;
            queue_message("You feel more agile");
            break;
        case POTION_EFFECT_DAMAGE:
            g_dmg_bonus++;
            queue_message("You feel deadlier");
            break;
        case POTION_EFFECT_BEER:
            pstat.hp = (unsigned char)((pstat.hp + 2 > pstat.hp_max) ? pstat.hp_max : pstat.hp + 2);
            queue_message("Hladno i dobro!");
            break;
        case POTION_EFFECT_WINE:
            pstat.hp = (unsigned char)((pstat.hp + 3 > pstat.hp_max) ? pstat.hp_max : pstat.hp + 3);
            queue_message("Domaca je najbolja!");
            break;
        default: /* POTION_EFFECT_POISON */
            g_poison_turns = 5;
            queue_message("You feel very sick!");
            break;
    }
}

void menu_quaff(unsigned char depth)
{
    unsigned char row, i, count = 0;
    for (i = 0; i < g_inv_count; i++)
        if (g_inventory[i].type == ITEM_POTION) count++;
    open_window("Quaff which potion?", count ? count : 1);
    row = (unsigned char)(WIN_Y0 + 1);
    for (i = 0; i < g_inv_count; i++) {
        if (g_inventory[i].type == ITEM_POTION) {
            gotoxy(WIN_LCOL, row++);
            cprintf("%c) %s", (char)('a' + i), potion_display_name((unsigned char)g_inventory[i].mod1));
        }
    }
    if (count == 0) { gotoxy(WIN_LCOL, row); cputs("You have no potions"); }
    {
        char sel = read_choice();
        if (sel >= 'a' && (unsigned char)(sel - 'a') < g_inv_count &&
            g_inventory[(unsigned char)(sel - 'a')].type == ITEM_POTION) {
            unsigned char idx = (unsigned char)(sel - 'a');
            unsigned char kind = (unsigned char)g_inventory[idx].mod1;

            inventory_remove(idx);

            clear_message();
            close_window(depth);
            apply_potion_effect(kind); /* automatically sets the message and the identification tag */
            draw_status(depth);
        } else {
            clear_message();
            close_window(depth);
        }
    }
}