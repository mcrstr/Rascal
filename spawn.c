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

/* Level setup (player, monsters, and item placement) + list of already identified
 * potions/rings/wands/scrolls + target selection for the identification scroll -
 * compiled as a separate overlay slot (OVERLAY6), loaded immediately after the map
 * generator (level_shapes.c, OVERLAY8, and level_connect.c, OVERLAY9) and called
 * immediately after it during the same transition to the level. */

#include <conio.h>
#include <stdio.h>
#include "level.h"
#include "rng.h"
#include "game_shared.h"

static unsigned char pick_random_open_cell(unsigned char *ox, unsigned char *oy)
{
    unsigned char tries;
    for (tries = 0; tries < 200; tries++) {
        unsigned char rx = rnd(MAP_W);
        unsigned char ry = rnd(MAP_H);
        tile_t t = (tile_t)g_map[ry][rx];
        if ((t == T_FLOOR || t == T_PASSAGE) &&
            !(rx == player.x && ry == player.y) && !monster_at(rx, ry) && !item_at(rx, ry)) {
            *ox = rx; *oy = ry;
            return 1;
        }
    }
    return 0;
}

/* Initial monster population for the level - all are asleep */
static void spawn_one_monster(unsigned char mx, unsigned char my, unsigned char depth)
{
    monster_t *nm = &g_monsters[g_monster_count];
    nm->x = mx;
    nm->y = my;
    nm->awake = 0;
    nm->alive = 1;
    monster_init_stats(nm, pick_species_for_depth(depth));
    g_monster_count++;
}

void spawn_monsters(unsigned char depth)
{
    unsigned char count = 2 + depth / 4;
    unsigned char i;
    if (count > MAX_MONSTERS) count = MAX_MONSTERS;
    g_monster_count = 0;
    for (i = 0; i < count; i++) {
        unsigned char mx, my;
        if (!pick_random_open_cell(&mx, &my)) break;
        spawn_one_monster(mx, my, depth);
    }
}

/* select a random point inside zone z that is free of anything important
 * to the caller (monster/player for monsters, item for items) - up to
 * 30 attempts. Common section for placing monsters and items in
 * the treasure chamber. */
static void pick_zone_point(zone_t *z, unsigned char *ox, unsigned char *oy, unsigned char for_monster)
{
    unsigned char tries, mx = 0, my = 0;
    for (tries = 0; tries < 30; tries++) {
        mx = (unsigned char)(z->pos.x + 1 + rnd(z->size.x > 2 ? (unsigned char)(z->size.x - 2) : 1));
        my = (unsigned char)(z->pos.y + 1 + rnd(z->size.y > 2 ? (unsigned char)(z->size.y - 2) : 1));
        if (g_map[my][mx] != T_FLOOR) continue;
        if (for_monster) {
            if (!monster_at(mx, my) && !(mx == player.x && my == player.y)) break;
        } else {
            if (!item_at(mx, my)) break;
        }
    }
    *ox = mx; *oy = my;
}

/* treasure room: with a 15% chance per level, if there is a sufficiently
 * large regular (not a maze, not a skipped) area - at least 10 cells
 * on one side - add 5-7 monsters and 5-7 items in addition to
 * the regular spawn. Monsters are immediately on guard (protecting their loot). */
static void spawn_treasure_room(unsigned char depth)
{
    unsigned char i, best = 255;
    if (rnd(100) >= 15) return;
    for (i = 0; i < NUM_ZONES; i++) {
        zone_t *z = &g_zones[i];
        if (z->flags & (RF_GONE | RF_MAZE)) continue;
        if (z->size.x >= 10 || z->size.y >= 10) { best = i; break; }
    }
    if (best == 255) return;
    g_level_has_treasure = 1;
    {
        zone_t *z = &g_zones[best];
        unsigned char n = (unsigned char)(5 + rnd(3)); /* 5..7 */
        unsigned char k;

        for (k = 0; k < n && g_monster_count < MAX_MONSTERS; k++) {
            unsigned char mx, my;
            pick_zone_point(z, &mx, &my, 1);
            spawn_one_monster(mx, my, depth); /* sleeps like regular monsters - wakes up when it sees the player */
        }

        n = (unsigned char)(5 + rnd(3));
        for (k = 0; k < n && g_ground_count < MAX_GROUND_ITEMS; k++) {
            unsigned char mx, my;
            ground_item_t *gi;
            pick_zone_point(z, &mx, &my, 0);
            gi = &g_ground_items[g_ground_count];
            gi->x = mx; gi->y = my; gi->active = 1;
            roll_random_item(gi, depth);
            g_ground_count++;
        }
    }
}

/* shop: appears at levels 5, 10, and 15 with a 50% chance. The merchant is a
 * harmless monster that spawns on a random empty tile in a pre-generated standard
 * room, no different from the others (same doors, same generation). */
static void spawn_shop(unsigned char depth)
{
    unsigned char idx, j, best = 255, mx, my, tries;
    if (depth != 5 && depth != 10 && depth != 15) return;
    if (rnd(100) >= 50) return;

    idx = rnd(NUM_ZONES);
    for (j = 0; j < NUM_ZONES; j++) {
        zone_t *z = &g_zones[idx];
        if (!(z->flags & (RF_GONE | RF_MAZE))) { best = idx; break; }
        idx++;
        if (idx >= NUM_ZONES) idx = 0;
    }
    if (best == 255) return;

    {
        zone_t *z = &g_zones[best];
        for (tries = 0; tries < 100; tries++) {
            mx = (unsigned char)(z->pos.x + rnd(z->size.x));
            my = (unsigned char)(z->pos.y + rnd(z->size.y));
            if (g_map[my][mx] == T_FLOOR && !monster_at(mx, my)) break;
        }
        if (tries == 100) return;
    }

    if (g_monster_count < MAX_MONSTERS) {
        monster_t *m = &g_monsters[g_monster_count];
        m->x = mx; m->y = my;
        m->awake = 0;
        m->alive = 1;
        m->is_merchant = 1;
        m->hp = 255;
        m->attack = 0;
        m->defense = 99;
        m->dmg_lo = 0;
        m->dmg_hi = 0;
        m->paralyzed = 0;
        m->species = 0; /* not formally used (is_merchant), but we're not leaving behind garbage from the previous level */
        g_monster_count++;
        g_level_has_shop = 1;

        {
            unsigned char k;
            for (k = 0; k < 5; k++) {
                ground_item_t *gi = &g_shop_offer[k];
                do { roll_random_item(gi, depth); } while (gi->type == ITEM_GOLD);
                gi->active = 1; /* reuse the field as 'not yet purchased' */
                g_shop_price[k] = item_price((item_type_t)gi->type, gi->mod1, gi->dice_s, gi->def2);
                /* merchant knows what they're selling - identify the type before making a purchase */
                if (gi->type == ITEM_POTION) potion_identified[(unsigned char)gi->mod1] = 1;
                else if (gi->type == ITEM_RING) ring_identified[(unsigned char)gi->mod1] = 1;
                else if (gi->type == ITEM_WAND) wand_identified[(unsigned char)gi->mod1] = 1;
                else if (is_scroll(gi->type)) scroll_identified[(unsigned char)(gi->type - ITEM_SCROLL_MAPPING)] = 1;
            }
        }
    }
}

void spawn_items(unsigned char depth)
{
    unsigned char count = 3 + depth / 6;
    unsigned char i;
    g_level_has_shop = 0;
    g_level_has_treasure = 0;
    if (count > MAX_GROUND_ITEMS) count = MAX_GROUND_ITEMS;
    g_ground_count = 0;
    for (i = 0; i < count; i++) {
        unsigned char mx, my;
        ground_item_t *gi;
        if (!pick_random_open_cell(&mx, &my)) break;
        gi = &g_ground_items[g_ground_count];
        gi->x = mx;
        gi->y = my;
        gi->active = 1;
        roll_random_item(gi, depth);
        g_ground_count++;
    }

    /* Ring of the Apple Signet is the game's objective. It has a 10% chance of appearing on Level 15,
     * and that chance increases by 10% for each subsequent level (20% on Level 16, 30% on Level 17...).
     * It will not reappear if you are already carrying it. */
    if (depth >= 15 && !g_has_signet && g_ground_count < MAX_GROUND_ITEMS) {
        unsigned int chance = (unsigned int)(depth - 15 + 1) * 10;
        if (chance > 100) chance = 100;
        if (rnd(100) < chance) {
            unsigned char mx, my;
            if (pick_random_open_cell(&mx, &my)) {
                ground_item_t *gi = &g_ground_items[g_ground_count];
                gi->x = mx; gi->y = my; gi->active = 1;
                gi->type = ITEM_QUEST_RING;
                gi->mod1 = 0; gi->mod2 = 0; gi->def1 = 0; gi->def2 = 0; gi->dice_n = 0; gi->dice_s = 0; gi->amount = 0;
                g_ground_count++;
            }
        }
    }

    spawn_treasure_room(depth);
    spawn_shop(depth);
}

/* put the player in a random passable cell of a random real room (not a
 * 'skipped' zone and not a bare maze with no floor). */
void place_player(void)
{
    unsigned char tries;
    for (tries = 0; tries < 200; tries++) {
        zone_t *z = &g_zones[rnd(NUM_ZONES)];
        unsigned char rx, ry;
        if (z->flags & RF_GONE) continue;
        rx = z->pos.x + 1 + rnd(z->size.x > 2 ? z->size.x - 2 : 1);
        ry = z->pos.y + 1 + rnd(z->size.y > 2 ? z->size.y - 2 : 1);
        if (g_map[ry][rx] == T_FLOOR) {
            player.x = rx;
            player.y = ry;
            return;
        }
    }
    /* fallback option - the first accessible cell on the map */
    {
        unsigned char x, y;
        for (y = 0; y < MAP_H; y++)
            for (x = 0; x < MAP_W; x++)
                if (g_map[y][x] == T_FLOOR) {
                    player.x = x;
                    player.y = y;
                    return;
                }
    }
}

/* k - a list of potions, rings, wands, and scrolls that have already been identified. */
typedef const char *(*known_name_fn_t)(unsigned char);

#pragma code-name (push, "LC")
static const char *scroll_name_for_known(unsigned char i)
{
    return item_name((item_type_t)(ITEM_SCROLL_MAPPING + i), 0, 0);
}

/* A single pass through a single category of the known table - previously duplicated
 * four times almost verbatim (potions/rings/wands/scrolls), differing only in the
 * identified array, the boundary, and the name function. Returns 1 if the page is
 * full (there is more) - in which case further categories on this page are not
 * processed. Placed in the Language Card - called only from here, but the actual
 * code bytes should not take up space in the already cramped overlay. */
static unsigned char known_category(const unsigned char *identified, unsigned char n, known_name_fn_t namefn,
                                     unsigned char page_start, unsigned char per_page,
                                     unsigned char *idx, unsigned char *shown, unsigned char *row)
{
    unsigned char i;
    for (i = 0; i < n; i++) {
        if (!identified[i]) continue;
        if (*idx < page_start) { (*idx)++; continue; }
        if (*shown >= per_page) return 1;
        gotoxy(WIN_LCOL, (*row)++);
        cputs(namefn(i));
        (*idx)++; (*shown)++;
    }
    return 0;
}
#pragma code-name (pop)

void menu_known(unsigned char depth)
{
    unsigned char page = 0;
    unsigned char per_page = (unsigned char)(WIN_Y1 - WIN_Y0 - 2);
    unsigned char count = 0, ci;
    for (ci = 0; ci < NUM_POTION_KINDS; ci++) if (potion_identified[ci]) count++;
    for (ci = 0; ci < NUM_RING_KINDS; ci++) if (ring_identified[ci]) count++;
    for (ci = 0; ci < NUM_WAND_KINDS; ci++) if (wand_identified[ci]) count++;
    for (ci = 0; ci < NUM_SCROLL_KINDS; ci++) if (scroll_identified[ci]) count++;

    for (;;) {
        unsigned char row, idx = 0, shown = 0, has_more = 0;
        unsigned char page_start = (unsigned char)(page * per_page);
        unsigned char pc = page_size(count, per_page, page);
        open_window("Known items", pc ? pc : 1);
        row = (unsigned char)(WIN_Y0 + 1);

        if (!has_more) has_more = known_category(potion_identified, NUM_POTION_KINDS, potion_display_name, page_start, per_page, &idx, &shown, &row);
        if (!has_more) has_more = known_category(ring_identified, NUM_RING_KINDS, ring_display_name, page_start, per_page, &idx, &shown, &row);
        if (!has_more) has_more = known_category(wand_identified, NUM_WAND_KINDS, wand_display_name, page_start, per_page, &idx, &shown, &row);
        if (!has_more) has_more = known_category(scroll_identified, NUM_SCROLL_KINDS, scroll_name_for_known, page_start, per_page, &idx, &shown, &row);

        if (count == 0) { gotoxy(WIN_LCOL, row); cputs("Nothing identified yet"); }
        read_page_continue(has_more);
        if (has_more) { page++; continue; }
        break;
    }
    close_window(depth);
}

/* Selecting a target for the identification scroll. This lives here and is
 * launched as a separate, safe step from main() AFTER menu_read() has already
 * fully completed and returned control - not from within it, so as not to
 * repeat the lesson from monster_drop(). */
#pragma code-name (push, "LC")
static unsigned char can_be_unidentified(unsigned char idx)
{
    inv_item_t *it = &g_inventory[idx];
    item_type_t t = (item_type_t)it->type;
    if (t == ITEM_POTION) return !potion_identified[(unsigned char)it->mod1];
    if (t == ITEM_RING) return !ring_identified[(unsigned char)it->mod1];
    if (t == ITEM_WAND) return !wand_identified[(unsigned char)it->mod1];
    if (is_scroll(t)) return !scroll_identified[(unsigned char)(t - ITEM_SCROLL_MAPPING)];
    if ((t == ITEM_WEAPON_MELEE || t == ITEM_WEAPON_RANGED || t == ITEM_ARMOR || t == ITEM_SHIELD) &&
        !it->def1) return 1;
    return 0;
}
#pragma code-name (pop)

static const char *inv_item_display_name(unsigned char idx)
{
    inv_item_t *it = &g_inventory[idx];
    item_type_t t = (item_type_t)it->type;
    if (t == ITEM_POTION) return potion_display_name((unsigned char)it->mod1);
    if (t == ITEM_RING) return ring_display_name((unsigned char)it->mod1);
    if (t == ITEM_WAND) return wand_display_name((unsigned char)it->mod1);
    return item_name(t, it->dice_n, it->dice_s);
}

void menu_identify_target(unsigned char depth)
{
    unsigned char row2, i2, count2 = 0;
    for (i2 = 0; i2 < g_inv_count; i2++)
        if (can_be_unidentified(i2)) count2++;
    if (count2 == 0) {
        queue_message("Nothing left to identify");
        return;
    }
    open_window("Identify what?", count2);
    row2 = (unsigned char)(WIN_Y0 + 1);
    for (i2 = 0; i2 < g_inv_count; i2++) {
        if (can_be_unidentified(i2)) {
            gotoxy(WIN_LCOL, row2++);
            cprintf("%c) %s", (char)('a' + i2), inv_item_display_name(i2));
        }
    }
    {
        char sel2 = read_choice();
        close_window(depth);
        if (sel2 >= 'a' && (unsigned char)(sel2 - 'a') < g_inv_count &&
            can_be_unidentified((unsigned char)(sel2 - 'a'))) {
            unsigned char idx2 = (unsigned char)(sel2 - 'a');
            inv_item_t *iv2 = &g_inventory[idx2];
            item_type_t t2 = (item_type_t)iv2->type;
            unsigned char kind2 = (unsigned char)iv2->mod1;
            if (t2 == ITEM_POTION) potion_identified[kind2] = 1;
            else if (t2 == ITEM_RING) ring_identified[kind2] = 1;
            else if (t2 == ITEM_WAND) wand_identified[kind2] = 1;
            else if (t2 == ITEM_WEAPON_MELEE || t2 == ITEM_WEAPON_RANGED || t2 == ITEM_ARMOR || t2 == ITEM_SHIELD)
                iv2->def1 = 1; /* stats and any curse become visible, same as when equipped */
            else scroll_identified[(unsigned char)(t2 - ITEM_SCROLL_MAPPING)] = 1;
            queue_message_str("You identify the %s!", inv_item_display_name(idx2));
        } else {
            queue_message("Never mind");
        }
    }
}