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

/* OVERLAY8. first half of the level generator + shop. loads before the zone
 * shapes are placed. the second half (connectivity) is in level_connect.c (OVERLAY9). */

#include <string.h>
#include <stdio.h>
#include <conio.h>
#include "level.h"
#include "rng.h"
#include "game_shared.h"

void draw_room(zone_t *z)
{
    unsigned char x, y;
    unsigned char x0 = z->pos.x, y0 = z->pos.y;
    unsigned char x1 = x0 + z->size.x - 1, y1 = y0 + z->size.y - 1;

    for (x = x0 + 1; x < x1; x++) {
        put(y0, x, T_WALL_H);
        put(y1, x, T_WALL_H);
    }
    for (y = y0 + 1; y < y1; y++) {
        put(y, x0, T_WALL_V);
        put(y, x1, T_WALL_V);
    }
    put(y0, x0, T_WALL_UL);
    put(y0, x1, T_WALL_UR);
    put(y1, x0, T_WALL_LL);
    put(y1, x1, T_WALL_LR);

    for (y = y0 + 1; y < y1; y++)
        for (x = x0 + 1; x < x1; x++)
            put(y, x, T_FLOOR);
}

/* --- maze zones: randomized frontier algorithm (prim-like) --- */
#define MAXFRONT 64
static unsigned char fr_y[MAXFRONT], fr_x[MAXFRONT];
static unsigned char fr_count;

/* does cell belong to the zone (needed for the frontier algorithm)? */
static unsigned char in_zone(zone_t *z, signed int y, signed int x)
{
    if (y < z->pos.y || x < z->pos.x) return 0;
    if (y >= z->pos.y + z->size.y || x >= z->pos.x + z->size.x) return 0;
    return 1;
}

static void maze_add_frontier(zone_t *z, signed int y, signed int x)
{
    if (fr_count >= MAXFRONT) return;
    if (!in_zone(z, y, x)) return;
    if (get((unsigned char)y, (unsigned char)x) != T_EMPTY) return;
    put((unsigned char)y, (unsigned char)x, T_FRONTIER_TMP); /* prevent double entries */
    fr_y[fr_count] = (unsigned char)y;
    fr_x[fr_count] = (unsigned char)x;
    fr_count++;
}

/* neighbor offsets for the frontier algorithm in the maze. previously, these were
 * static const arrays directly in draw_maze. it turned out that when merging
 * code-name and rodata-name into a single segment, the cc65 overlay places such
 * a local constant table right at the beginning of the compiled body of the function,
 * without jumping to it. that is, the function address itself points to data rather
 * than code, and any call immediately 'executes' these bytes as instructions.
 * a function with a switch statement compiles to clean code without this problem. */
static signed char dy4_val(unsigned char i)
{
    switch (i) { case 0: return -2; case 1: return 2; default: return 0; }
}
static signed char dx4_val(unsigned char i)
{
    switch (i) { case 2: return -2; case 3: return 2; default: return 0; }
}

/* randomized Prim's algorithm: start from one cell, repeatedly pick a
 * random cell from the growing 'frontier,' carve a passage to it from
 * whichever already-dug neighbor it touched, then add its own new
 * neighbors to the frontier. produces a maze that's a spanning tree.
 * exactly one path between any two cells, no loops yet. */
void draw_maze(zone_t *z)
{
    unsigned char sy = z->pos.y + 1, sx = z->pos.x + 1;
    unsigned char cy, cx, pick, i;
    signed int ny, nx;

    unsigned char di;

    fr_count = 0;
    put(sy, sx, T_PASSAGE);
    for (di = 0; di < 4; di++)
        maze_add_frontier(z, (signed int)sy + dy4_val(di), (signed int)sx + dx4_val(di));

    while (fr_count > 0) {
        unsigned char choice[4], nchoice = 0;

        pick = rnd(fr_count);
        cy = fr_y[pick];
        cx = fr_x[pick];
        fr_y[pick] = fr_y[fr_count - 1];
        fr_x[pick] = fr_x[fr_count - 1];
        fr_count--;

        for (i = 0; i < 4; i++) {
            ny = (signed int)cy + dy4_val(i);
            nx = (signed int)cx + dx4_val(i);
            if (ny >= 0 && nx >= 0 &&
                get((unsigned char)ny, (unsigned char)nx) == T_PASSAGE)
                choice[nchoice++] = i;
        }
        if (nchoice == 0) { put(cy, cx, T_EMPTY); continue; } /* if the frontier is out of date, free up the cell again */

        i = choice[rnd(nchoice)];
        put(cy, cx, T_PASSAGE);
        /* cut a cell between cy/cx and an existing adjacent passage */
        put((unsigned char)(cy + dy4_val(i) / 2), (unsigned char)(cx + dx4_val(i) / 2), T_PASSAGE);

        for (di = 0; di < 4; di++)
            maze_add_frontier(z, (signed int)cy + dy4_val(di), (signed int)cx + dx4_val(di));
    }
}

/* --- merchant --- */
/* merchant's buy/sell screens live in this overlay purely for
 * memory reasons. neither the room/maze generator above nor the shop
 * below needs the other loaded at the same time, and this slot had
 * room to spare when the merchant was added. */

/* is the inventory item idx equipped? */
static unsigned char is_equipped(unsigned char idx)
{
    return (unsigned char)g_eq_armor == idx || (unsigned char)g_eq_hand_l == idx ||
           (unsigned char)g_eq_hand_r == idx || (unsigned char)g_eq_ranged == idx ||
           (unsigned char)g_eq_ring_l == idx || (unsigned char)g_eq_ring_r == idx;
}

static unsigned char sellable(unsigned char idx)
{
    return g_inventory[idx].type != ITEM_GOLD && g_inventory[idx].type != ITEM_QUEST_RING &&
           !is_equipped(idx);
}

/* selling price of an inventory item with idx is half of item_price(). */
static unsigned char sell_price(unsigned char idx)
{
    inv_item_t *it = &g_inventory[idx];
    return (unsigned char)(item_price((item_type_t)it->type, it->mod1, it->dice_s, it->def2) / 2);
}

/* find the index of the stack of gold in the backpack, or -1 if it isn't there.
 * common part of spend_gold()/add_gold(). */
static signed char find_gold_slot(void)
{
    unsigned char gk;
    for (gk = 0; gk < g_inv_count; gk++)
        if (g_inventory[gk].type == ITEM_GOLD) return (signed char)gk;
    return -1;
}

/* reduce gold (upon purchase). 0 if there isn't enough. */
static unsigned char spend_gold(unsigned int amount)
{
    signed char gk = find_gold_slot();
    inv_item_t *g;
    if (gk < 0) return 0;
    g = &g_inventory[(unsigned char)gk];
    if (g->amount < amount) return 0;
    g->amount -= amount;
    if (g->amount == 0) inventory_remove((unsigned char)gk);
    return 1;
}

/* add gold (upon sale). creates a stack if one doesn't already exist. */
static void add_gold(unsigned int amount)
{
    unsigned char k = find_or_add_stack(ITEM_GOLD, 0);
    if (k != 0xFF) g_inventory[k].amount += amount;
}

/* display name of the item for the shop.
 * common part of print_shop_line() and queue_shop_message(). */
static void shop_item_name(char *buf, item_type_t type, signed char mod1, signed char mod2,
                            unsigned char dice_n, unsigned char dice_s)
{
    unsigned char t = (unsigned char)type;
    if (t == ITEM_POTION) { strcpy(buf, potion_display_name((unsigned char)mod1)); return; }
    if (t == ITEM_RING) { ring_name_str(buf, (unsigned char)mod1, mod2, 1); return; }
    if (t == ITEM_WAND) { strcpy(buf, wand_display_name((unsigned char)mod1)); return; }
    if (t == ITEM_TREASURE) { strcpy(buf, treasure_display_name((unsigned char)mod1)); return; }
    if (t == ITEM_AMMO) { strcpy(buf, ammo_display_name((unsigned char)mod1)); return; }
    strcpy(buf, item_name(type, dice_n, dice_s));
}

/* one line of the shop/sell list: 'a) Name stat (N gold)' */
static void print_shop_line(char letter, item_type_t type, signed char mod1, signed char mod2,
                             unsigned char dice_n, unsigned char dice_s,
                             signed char def1, signed char def2, unsigned char amount, unsigned char price)
{
    unsigned char t = (unsigned char)type;
    (void)def1; /* shop always displays the full stats. see the item_stat_str call below */
    if (t == ITEM_FOOD || t == ITEM_AMMO) {
        char nm[34];
        shop_item_name(nm, type, mod1, mod2, dice_n, dice_s);
        cprintf("%c) %s (%u) (%u gold)", letter, nm, amount, price);
    } else if (t == ITEM_POTION || t == ITEM_RING || t == ITEM_WAND || t == ITEM_TREASURE ||
        (t >= ITEM_SCROLL_MAPPING && t <= ITEM_SCROLL_REMOVE_CURSE)) {
        char nm[34];
        shop_item_name(nm, type, mod1, mod2, dice_n, dice_s);
        cprintf("%c) %s (%u gold)", letter, nm, price);
    } else {
        char stat[24];
        item_stat_str(stat, type, mod1, mod2, dice_n, dice_s, 1, def2);
        cprintf("%c) %s %s (%u gold)", letter, item_name(type, dice_n, dice_s), stat, price);
    }
}

/* subject line for a general message: 'You buy/sell the ...' */
static void queue_shop_message(const char *verb, item_type_t type, signed char mod1, signed char mod2,
                                unsigned char dice_n, unsigned char dice_s, unsigned char amount)
{
    char buf[48], nm[34];
    unsigned char t = (unsigned char)type;
    shop_item_name(nm, type, mod1, mod2, dice_n, dice_s);
    if (t == ITEM_FOOD || t == ITEM_AMMO)
        sprintf(buf, "You %s %u %s", verb, amount, nm);
    else
        sprintf(buf, "You %s the %s", verb, nm);
    queue_message(buf);
}

/* P next to the merchant - buy one of the items */
void menu_shop_buy(unsigned char depth)
{
    unsigned char i;
    open_window("Buy?", 5);
    for (i = 0; i < 5; i++) {
        ground_item_t *gi = &g_shop_offer[i];
        gotoxy(WIN_LCOL, (unsigned char)(WIN_Y0 + 1 + i));
        if (!gi->active) cprintf("%c) Sold", (char)('a' + i));
        else print_shop_line((char)('a' + i), (item_type_t)gi->type, gi->mod1, gi->mod2,
                              gi->dice_n, gi->dice_s, gi->def1, gi->def2, gi->amount, g_shop_price[i]);
    }
    {
        char sel = read_choice();
        clear_message();
        close_window(depth);
        if (sel >= 'a' && (unsigned char)(sel - 'a') < 5) {
            unsigned char idx = (unsigned char)(sel - 'a');
            ground_item_t *gi = &g_shop_offer[idx];
            if (!gi->active) {
                queue_message("Sold");
            } else if (get_gold_total() < g_shop_price[idx]) {
                queue_message("Pricey");
            } else {
                unsigned char bought = 0;
                if (gi->type == ITEM_FOOD || gi->type == ITEM_AMMO) {
                    /* common logic with try_pickup() - resident helper */
                    unsigned char k = find_or_add_stack((item_type_t)gi->type, gi->mod1);
                    if (k != 0xFF) {
                        g_inventory[k].amount += gi->amount;
                        bought = 1;
                    }
                } else if (inventory_add((item_type_t)gi->type, gi->mod1, gi->mod2,
                                          gi->dice_n, gi->dice_s, gi->def1, gi->def2)) {
                    bought = 1;
                }
                if (bought) {
                    spend_gold(g_shop_price[idx]);
                    gi->active = 0;
                    queue_shop_message("buy", (item_type_t)gi->type, gi->mod1, gi->mod2, gi->dice_n, gi->dice_s, gi->amount);
                } else {
                    queue_message("Pack full");
                }
            }
        }
    }
    draw_status(depth);
}

/* S next to the merchant - sell an item from your backpack (for half the market
 * price). can't sell equipped items or gold/ The ring of the apple signet. same
 * slot as for buying (see menu_shop_buy above). the shop screens are logically
 * linked. paginated! a heavily-stocked inventory can have more sellable items
 * than fit on one screen at once. */
void menu_shop_sell(unsigned char depth)
{
    unsigned char count = 0, i;
    unsigned char per_page = (unsigned char)(WIN_Y1 - WIN_Y0 - 1);
    unsigned char page = 0;
    char sel = 0;
    for (i = 0; i < g_inv_count; i++) if (sellable(i)) count++;

    for (;;) {
        unsigned char row, idx_on_page = 0, shown = 0, has_more = 0;
        unsigned char page_start = (unsigned char)(page * per_page);
        unsigned char page_count = page_size(count, per_page, page);
        unsigned char nav;
        open_window("Sell?", page_count ? page_count : 1);
        row = (unsigned char)(WIN_Y0 + 1);
        for (i = 0; i < g_inv_count; i++) {
            inv_item_t *it;
            if (!sellable(i)) continue;
            if (idx_on_page < page_start) { idx_on_page++; continue; }
            if (shown >= per_page) { has_more = 1; break; }
            it = &g_inventory[i];
            gotoxy(WIN_LCOL, row++);
            print_shop_line((char)('a' + i), (item_type_t)it->type, it->mod1,
                             it->mod2, it->dice_n, it->dice_s,
                             it->def1, it->def2, (unsigned char)it->amount,
                             sell_price(i));
            idx_on_page++;
            shown++;
        }
        if (count == 0) { gotoxy(WIN_LCOL, row); cputs("Empty"); }

        nav = read_page_nav(has_more, count == 0, &sel);
        if (nav == 0) { close_window(depth); return; }
        if (nav == 1) { page++; continue; }
        close_window(depth);
        break;
    }
    {
        if (sel >= 'a' && (unsigned char)(sel - 'a') < g_inv_count) {
            unsigned char idx = (unsigned char)(sel - 'a');
            if (sellable(idx)) {
                inv_item_t *it = &g_inventory[idx];
                unsigned int price = sell_price(idx);
                unsigned char qty = (it->type == ITEM_FOOD || it->type == ITEM_AMMO) ?
                    (unsigned char)it->amount : 1;
                item_type_t t = (item_type_t)it->type;
                signed char m1 = it->mod1;
                signed char m2 = it->mod2;
                unsigned char dn = it->dice_n;
                unsigned char ds = it->dice_s;
                inventory_remove(idx);
                add_gold((unsigned int)(price * qty));
                queue_shop_message("sell", t, m1, m2, dn, ds, qty);
            }
        }
    }
    draw_status(depth);
}
