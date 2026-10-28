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

/* Screens for the combat use of items (wands, shooting): compiled
 * into a separate overlay slot (OVERLAY7) and loaded from disk on demand. */

#include <conio.h>
#include <stdio.h>
#include "game_shared.h"
#include "rng.h"

/* draw a line from the player in the direction of dx, dy to the first monster or wall -
 * a general foundation for wands and shooting. hit_x/hit_y - where the line stops
 * (the monster's cell, or the last passable cell before the wall). */
static monster_t *trace_line(signed char dx, signed char dy, unsigned char *hit_x, unsigned char *hit_y)
{
    unsigned char x = player.x;
    unsigned char y = player.y;
    for (;;) {
        x += dx; /* If x was 0 and dx == -1, x will become 255 - an overflow */
        y += dy;
        if (x >= MAP_W || y >= MAP_H) { /* 255 >= MAP_W will be treated as an out-of-bounds error */
            *hit_x = (unsigned char)(x - dx); *hit_y = (unsigned char)(y - dy); return 0;
        }
        if (!is_open_tile((tile_t)g_map[y][x])) {
            *hit_x = (unsigned char)(x - dx); *hit_y = (unsigned char)(y - dy); return 0;
        }
        {
            monster_t *m = monster_at(x, y);
            if (m) { *hit_x = x; *hit_y = y; return m; }
        }
    }
}

/* ask for a direction - all 8 directions, including the diagonals. Returns 0
 * if the user pressed something else (changed their mind). */
static unsigned char choose_direction(signed char *dx, signed char *dy)
{
    char k;
    gotoxy(0, 0);
    cclear(80);
    gotoxy(0, 0);
    cputs("Dir?");
    k = cgetc();
    switch (k) {
        case 'g': case CH_CURS_LEFT:  *dx = -1; *dy =  0; return 1;
        case 'j': case CH_CURS_RIGHT: *dx =  1; *dy =  0; return 1;
        case 'y': case CH_CURS_UP:    *dx =  0; *dy = -1; return 1;
        case 'n': case CH_CURS_DOWN:  *dx =  0; *dy =  1; return 1;
        case 't': *dx = -1; *dy = -1; return 1;
        case 'u': *dx =  1; *dy = -1; return 1;
        case 'b': *dx = -1; *dy =  1; return 1;
        case 'm': *dx =  1; *dy =  1; return 1;
        default: return 0;
    }
}

/* LOS symbol for shooting for direction: diagonals \ or /,
 * straight lines - or |. The (*) symbol is always used for the wands. */
static char fire_line_char(signed char dx, signed char dy)
{
    if (dy == 0) return '-';
    if (dx == 0) return '|';
    if ((dx > 0) == (dy > 0)) return '\\';
    return '/';
}

/* draw a line of sight from the player to (hx, hy) - the cell where
 * trace_line() stopped (a monster or a wall) - display it briefly on
 * the screen, and then restore the cells to their original state. */
static void draw_aim_line(unsigned char hx, unsigned char hy, signed char dx, signed char dy, char ch)
{
    unsigned char x = player.x, y = player.y;
    unsigned char path_x[24], path_y[24];
    unsigned char count = 0;
    zone_t *pr;
    unsigned char i;
    while (!(x == hx && y == hy) && count < 24) {
        x = (unsigned char)(x + dx);
        y = (unsigned char)(y + dy);
        path_x[count] = x;
        path_y[count] = y;
        count++;
        gotoxy(x, (unsigned char)(y + 1));
        cputc(ch);
    }
    {
        unsigned char delay_x = 30, delay_y = 0; /* a short pause - otherwise the line
           * isn't visible; an 8-bit nested loop instead of a 16-bit for(;<3500;) loop.
           * The value 30 was ROUGHLY chosen to be about the same duration as before -
           * it hasn't been measured on actual hardware, so it's worth double-checking visually */
        while (delay_x) { if (--delay_y == 0) delay_x--; }
    }
    pr = lit_room_at(player.x, player.y);
    for (i = 0; i < count; i++) draw_cell(path_x[i], path_y[i], pr);
}

/* general rewards for killing enemies - experience by type, drop chance, and level-up check. */
static void award_kill(monster_t *m)
{
    pstat.xp += monster_templates[m->species].exp;
    monster_drop(m);
    check_level_up();
}

/* Apply damage from the wand to a monster - common section for missile/drain_life;
 * the only difference is in the message text (drain also heals the player on top of that).
 * MISSILE=0,FORCE=5,POWER=6,RUIN=7 - common damage level table for all
 * four projectile effects of the wands; damage x1/x1.5/x2/x4 (base 3+rnd(6)).
 * File scope - NOT inside the function: local static const arrays inside overlay functions
 * cause crashes on actual hardware; this is a documented architectural rule of the project. */
static const unsigned char tier_base[4]  = { 3, 4, 6, 12 };
static const unsigned char tier_range[4] = { 6, 9, 12, 24 };

static void wand_hit(monster_t *m, signed int dmg, const char *msg_hits, const char *msg_kills)
{
    if ((unsigned int)dmg >= m->hp) {
        m->alive = 0;
        queue_message_str(msg_kills, monster_name(m));
        award_kill(m);
        draw_cell(m->x, m->y, lit_room_at(m->x, m->y)); /* monster could be in a different room */
    } else {
        m->hp = (unsigned char)(m->hp - dmg);
        m->awake = 1;
        queue_message_str(msg_hits, monster_name(m));
    }
}

/* z - use the magic wand in the selected direction. Deduct one charge from the
 * wand with idx (of type kind) - the wand itself breaks when the charge reaches zero. */
static void wand_use_charge(unsigned char idx, unsigned char kind)
{
    inv_item_t *it = &g_inventory[idx];
    if (it->mod2 > 0) it->mod2--;
    if (it->mod2 == 0) {
        queue_message_str("%s crumbles", wand_display_name(kind));
        inventory_remove(idx);
    }
}

void menu_zap(unsigned char depth)
{
    unsigned char row, i, count = 0;
    for (i = 0; i < g_inv_count; i++)
        if (g_inventory[i].type == ITEM_WAND) count++;
    open_window("Zap what?", count ? count : 1);
    row = (unsigned char)(WIN_Y0 + 1);
    for (i = 0; i < g_inv_count; i++) {
        if (g_inventory[i].type == ITEM_WAND) {
            inv_item_t *it = &g_inventory[i];
            unsigned char wk = (unsigned char)it->mod1;
            gotoxy(WIN_LCOL, row++);
            if (wand_identified[wk])
                cprintf("%c) %s (%u)", (char)('a' + i), wand_display_name(wk), (unsigned)it->mod2);
            else
                cprintf("%c) %s", (char)('a' + i), wand_display_name(wk));
        }
    }
    if (count == 0) { gotoxy(WIN_LCOL, row); cputs("No wands"); }    {
        char sel = read_choice();
        if (sel >= 'a' && (unsigned char)(sel - 'a') < g_inv_count &&
            g_inventory[(unsigned char)(sel - 'a')].type == ITEM_WAND) {
            unsigned char idx = (unsigned char)(sel - 'a');
            unsigned char kind = (unsigned char)g_inventory[idx].mod1;
            signed char dx, dy;

            clear_message();
            close_window(depth);
            if (wand_effect[kind] == WAND_EFFECT_IDENTIFY) {
                wand_identified[kind] = 1;
                g_pending_identify = 1; /* select a target as a separate, safe step from main() */
                wand_use_charge(idx, kind);
                end_turn(0, depth);
                return;
            }
            if (!choose_direction(&dx, &dy)) {
                clear_message();
                draw_status(depth);
                return;
            }
            {
                unsigned char hx, hy;
                monster_t *m = trace_line(dx, dy, &hx, &hy);
                draw_aim_line(hx, hy, dx, dy, '*');
                wand_identified[kind] = 1;
                if (m) {
                    unsigned char hit_chance = 50; /* base chance of hitting with a wand */
                    if (g_player_class == CLASS_INTELLECTUAL) {
                        unsigned int bonus_pct = (unsigned int)(pstat.level - 1) * 10;
                        if (bonus_pct >= 50) hit_chance = 100;
                        else hit_chance = (unsigned char)(50 + bonus_pct);
                    }
                    if (rnd(100) < hit_chance) {
                        unsigned char effect = wand_effect[kind];
                        signed int wand_dmg_bonus = 0;
                        if (g_player_class == CLASS_INTELLECTUAL) wand_dmg_bonus = (signed int)(pstat.level / 2);
                        if (effect == WAND_EFFECT_MISSILE || effect == WAND_EFFECT_FORCE ||
                            effect == WAND_EFFECT_POWER || effect == WAND_EFFECT_RUIN) {
                            /* MISSILE=0,FORCE=5,POWER=6,RUIN=7 - general table (file scope; tier_base/tier_range
							 * are declared earlier in the file - local static consts inside the overlay function
                             * cause a crash; this is a documented project rule) */
                            unsigned char tier = (effect == WAND_EFFECT_MISSILE) ? 0 : (unsigned char)(effect - WAND_EFFECT_FORCE + 1);
                            signed int dmg = tier_base[tier] + rnd(tier_range[tier]) + wand_dmg_bonus;
                            wand_hit(m, dmg, "Bolt hits the %s!", "Bolt kills the %s!");
                        } else if (effect == WAND_EFFECT_DRAIN_LIFE) {
                            signed int dmg = 3 + rnd(6) + wand_dmg_bonus;
                            wand_hit(m, dmg, "You drain life from the %s!", "%s withers and dies!");
                            pstat.hp = (unsigned char)((pstat.hp + dmg > pstat.hp_max) ? pstat.hp_max : pstat.hp + dmg);
                        } else if (effect == WAND_EFFECT_SLEEP) {
                            m->awake = 0;
                            queue_message_str("%s falls asleep!", monster_name(m));
                        } else {
                            m->paralyzed = 1;
                            m->awake = 1;
                            queue_message_str("%s froze!", monster_name(m));
                        }
                    } else {
                        queue_message_str("Magic misses the %s", monster_name(m));
                    }
                } else {
                    queue_message("Fizzle");
                }
            }
            wand_use_charge(idx, kind);
            end_turn(0, depth);
        } else {
            clear_message();
            close_window(depth);
        }
    }
}

/* f - fire a projectile from an equipped long-range weapon. Technically, only
 * bows are available (there wasn't enough memory for other types of weapons) */
void menu_fire(unsigned char depth)
{
    if (g_eq_ranged < 0) {
        queue_message("No bows");
        draw_status(depth);
        return;
    }
    {
        unsigned char row, i, count = 0;
        for (i = 0; i < g_inv_count; i++)
            if (g_inventory[i].type == ITEM_AMMO) count++;
        open_window("Fire what?", count ? count : 1);
        row = (unsigned char)(WIN_Y0 + 1);
        for (i = 0; i < g_inv_count; i++) {
            if (g_inventory[i].type == ITEM_AMMO) {
                gotoxy(WIN_LCOL, row++);
                cprintf("%c) %s (%u)", (char)('a' + i),
                    ammo_display_name((unsigned char)g_inventory[i].mod1), (unsigned)g_inventory[i].amount);
            }
        }
        if (count == 0) { gotoxy(WIN_LCOL, row); cputs("No ammo"); }
        {
            char sel = read_choice();
            if (sel >= 'a' && (unsigned char)(sel - 'a') < g_inv_count &&
                g_inventory[(unsigned char)(sel - 'a')].type == ITEM_AMMO) {
                unsigned char idx = (unsigned char)(sel - 'a');
                inv_item_t *ammo = &g_inventory[idx];
                signed char dx, dy;

                clear_message();
                close_window(depth);
                if (!choose_direction(&dx, &dy)) {
                    clear_message();
                    draw_status(depth);
                    return;
                }
                {
                unsigned char hx, hy;
                monster_t *m;

                m = trace_line(dx, dy, &hx, &hy);
                draw_aim_line(hx, hy, dx, dy, fire_line_char(dx, dy));
                if (m) {
                    inv_item_t *rw = &g_inventory[g_eq_ranged];
                    signed int bonus = str_bonus_hit(effective_strength()) + rw->mod1;
                    if (g_player_class == CLASS_TRICKSTER) bonus += 1 + (signed int)(pstat.level / 2);
                    else if (g_player_class == CLASS_STRONGMAN) bonus -= 1;
                    else if (g_player_class == CLASS_INTELLECTUAL) bonus -= 1;
                    bonus += 2 * ring_worn_effect(RING_EFFECT_DEXTERITY);
                    bonus += g_dex_bonus; /* permanent bonus from the Potion of Dexterity */
                    m->awake = 1;
                    if (roll_to_hit(bonus, m->defense)) {
                        signed int dmg = 0;
                        unsigned char k;
                        for (k = 0; k < rw->dice_n; k++)
                            dmg += 1 + rnd(rw->dice_s);
                        dmg += rw->mod2;
                        dmg += ammo_damage_bonus((unsigned char)ammo->mod1);
                        dmg += 2 * ring_worn_effect(RING_EFFECT_DAMAGE);
                        dmg += g_dmg_bonus; /* permanent bonus from Potion of Damage */
                        if (dmg < 1) dmg = 1;
                        if ((unsigned int)dmg >= m->hp) {
                            m->hp = 0;
                            m->alive = 0;
                            queue_message_str("Slain the %s!", monster_name(m));
                            award_kill(m);
                            draw_cell(m->x, m->y, lit_room_at(m->x, m->y));
                        } else {
                            m->hp = (unsigned char)(m->hp - dmg);
                            queue_message_str("Hit the %s", monster_name(m));
                        }
                    } else {
                        queue_message_str("Miss the %s!", monster_name(m));
                    }
                } else {
                    queue_message("Missed!");
                }

                /* There is a 50% chance that the projectile will remain on the ground where it came to rest */
                if (rnd(2) == 0 && !item_at(hx, hy)) {
                    unsigned char gslot = find_free_ground_slot();
                    if (gslot != 0xFF) {
                        ground_item_t *gi = &g_ground_items[gslot];
                        gi->x = hx; gi->y = hy; gi->active = 1;
                        gi->type = ITEM_AMMO;
                        gi->amount = 1;
                        gi->mod1 = ammo->mod1; /* save the type of arrow fired */
                        gi->mod2 = 0; gi->def1 = 0; gi->def2 = 0; gi->dice_n = 0; gi->dice_s = 0;
                        draw_cell(hx, hy, lit_room_at(hx, hy));
                    }
                }

                ammo->amount--;
                if (ammo->amount == 0) {
                    inventory_remove(idx);
                }
                end_turn(0, depth);
                }
            } else {
                clear_message();
                close_window(depth);
            }
        }
    }
}