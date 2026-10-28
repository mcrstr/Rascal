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
 
/* Item menu screens: compiled as a separate overlay slot (OVERLAY2)
 * and loaded from disk on demand (see main.c: load_menu_overlay). */

#include <conio.h>
#include <stdio.h>
#include <string.h>
#include "game_shared.h"

/* whether inventory item idx is equipped in any equipment slot - a local helper (there is
 * a similar one in level_shapes.c/menus_action2.c; there's no point in creating a global one). */
static unsigned char is_equipped(unsigned char idx)
{
    return (unsigned char)g_eq_armor == idx || (unsigned char)g_eq_hand_l == idx ||
           (unsigned char)g_eq_hand_r == idx || (unsigned char)g_eq_ranged == idx ||
           (unsigned char)g_eq_ring_l == idx || (unsigned char)g_eq_ring_r == idx;
}

/* w - wear or wield items */
static void wield_message(const char *nm, const char *position, unsigned char cursed)
{
    char msg[64];
    sprintf(msg, "You wield the %s%s", nm, position);
    strcat(msg, cursed ? "... it feels wrong!" : "."); /* if cursed */
    queue_message(msg);
}

void menu_wield(unsigned char depth)
{
    unsigned char row, i, count = 0;
    char stat[24];
    for (i = 0; i < g_inv_count; i++) {
        item_type_t t = (item_type_t)g_inventory[i].type;
        if (is_equipped(i)) continue;
        if (t == ITEM_WEAPON_MELEE || t == ITEM_WEAPON_RANGED || t == ITEM_ARMOR || t == ITEM_SHIELD ||
            t == ITEM_RING)
            count++;
    }
    open_window("Wield/wear which item?", count ? count : 1);
    row = (unsigned char)(WIN_Y0 + 1);
    for (i = 0; i < g_inv_count; i++) {
        item_type_t t = (item_type_t)g_inventory[i].type;
        if (is_equipped(i)) continue;
        if (t == ITEM_WEAPON_MELEE || t == ITEM_WEAPON_RANGED || t == ITEM_ARMOR || t == ITEM_SHIELD) {
            gotoxy(WIN_LCOL, row++);
            item_stat_str_inv(stat, i);
            cprintf("%c) %s %s", (char)('a' + i), item_name(t, g_inventory[i].dice_n, g_inventory[i].dice_s), stat);
        } else if (t == ITEM_RING) {
            char rname[34];
            gotoxy(WIN_LCOL, row++);
            ring_name_str(rname, (unsigned char)g_inventory[i].mod1, g_inventory[i].mod2, 0);
            cprintf("%c) %s", (char)('a' + i), rname);
        }
    }
    if (count == 0) { gotoxy(WIN_LCOL, row); cputs("Nothing to wield"); }
    {
        char sel = read_choice();
        if (sel >= 'a' && (unsigned char)(sel - 'a') < g_inv_count) {
            unsigned char idx = (unsigned char)(sel - 'a');
            inv_item_t *iv = &g_inventory[idx];
            item_type_t t = (item_type_t)iv->type;
            const char *nm = item_name(t, iv->dice_n, iv->dice_s);
            unsigned char cursed_hand = slot_is_cursed((signed char)idx);
            if (t == ITEM_WEAPON_MELEE || t == ITEM_SHIELD) {
                unsigned char is_2h = (t == ITEM_WEAPON_MELEE) &&
                    is_two_handed_weapon(iv->dice_n, iv->dice_s);
                if (g_eq_hand_l >= 0 && g_eq_hand_r >= 0) {
                    clear_message();
                    close_window(depth);
                    queue_message("Both hands full. Remove first");
                } else if (g_eq_hand_l < 0 && g_eq_hand_r < 0) {
                    if (is_2h) {
                        iv->def1 = 1; /* identified */
                        g_eq_hand_r = (signed char)idx;
                        clear_message();
                        close_window(depth);
                        wield_message(nm, " in both hands", cursed_hand);
                    } else {
                        char hk;
                        gotoxy(WIN_LCOL, (unsigned char)(WIN_Y0 + 1));
                        cclear((unsigned char)(WIN_X1 - WIN_X0 - 2));
                        gotoxy(WIN_LCOL, (unsigned char)(WIN_Y0 + 1));
                        cputs("Which hand? (l/r)");
                        hk = cgetc();
                        if (hk == 'l') { iv->def1 = 1; g_eq_hand_l = (signed char)idx; }
                        else if (hk == 'r') { iv->def1 = 1; g_eq_hand_r = (signed char)idx; }
                        clear_message();
                        close_window(depth);
                        if (hk == 'l') wield_message(nm, " in your left hand", cursed_hand);
                        else if (hk == 'r') wield_message(nm, " in your right hand", cursed_hand);
                        else
                            queue_message("Never mind");
                    }
                } else {
                    /* only one hand is occupied */
                    clear_message();
                    close_window(depth);
                    if (g_eq_hand_r < 0) {
                        /* right hand is free, left hand is occupied, not enough space for a two-handed grip */
                        if (is_2h) {
                            queue_message("Need both hands free");
                        } else {
                            iv->def1 = 1; /* identified */
                            g_eq_hand_r = (signed char)idx;
                            queue_message_str(cursed_hand ? "You wield the %s... it feels wrong!" : "You wield the %s", nm);
                        }
                    } else if (right_hand_two_handed()) {
                        /* left hand is technically empty, but it's occupied by the two-handed weapon */
                        queue_message("Both hands hold your two-handed weapon");
                    } else if (is_2h) {
                        /* right hand is occupied with something that requires one hand, there wouldn't be enough room for a two-handed weapon */
                        queue_message("Need both hands free");
                    } else {
                        iv->def1 = 1; /* identified */
                        g_eq_hand_l = (signed char)idx;
                        queue_message_str(cursed_hand ? "You wield the %s... it feels wrong!" : "You wield the %s", nm);
                    }
                }
            } else if (t == ITEM_WEAPON_RANGED) {
                if (g_eq_ranged >= 0 && slot_is_cursed(g_eq_ranged)) {
                    clear_message();
                    close_window(depth);
                    queue_message("Your cursed weapon won't budge!");
                } else {
                    unsigned char cursed = slot_is_cursed((signed char)idx);
                    iv->def1 = 1; /* identified - reveal weapon info */
                    g_eq_ranged = (signed char)idx;
                    clear_message();
                    close_window(depth);
                    if (cursed) queue_message_str("You wield the %s... it feels wrong!", nm);
                    else queue_message_str("You wield the %s", nm);
                }
            } else if (t == ITEM_ARMOR) {
                if (g_eq_armor >= 0 && slot_is_cursed(g_eq_armor)) {
                    clear_message();
                    close_window(depth);
                    queue_message("Your cursed armor won't budge!");
                } else {
                    unsigned char cursed = slot_is_cursed((signed char)idx);
                    iv->def1 = 1; /* identified - reveal armor info */
                    g_eq_armor = (signed char)idx;
                    clear_message();
                    close_window(depth);
                    if (cursed) queue_message_str("You wear the %s... it feels wrong!", nm);
                    else queue_message_str("You wear the %s", nm);
                }
            } else if (t == ITEM_RING) {
                signed char *slot = 0;
                if (g_eq_ring_l >= 0 && g_eq_ring_r >= 0) {
                    clear_message();
                    close_window(depth);
                    queue_message("Both rings full. Remove first");
                } else if (g_eq_ring_l < 0 && g_eq_ring_r < 0) {
                    char hk;
                    gotoxy(WIN_LCOL, (unsigned char)(WIN_Y0 + 1));
                    cclear((unsigned char)(WIN_X1 - WIN_X0 - 2));
                    gotoxy(WIN_LCOL, (unsigned char)(WIN_Y0 + 1));
                    cputs("Which hand? (l/r)");
                    hk = cgetc();
                    if (hk == 'l') slot = &g_eq_ring_l;
                    else if (hk == 'r') slot = &g_eq_ring_r;
                    clear_message();
                    close_window(depth);
                    if (!slot) queue_message("Never mind");
				/* only one ring slot free - no need to ask which */
                } else if (g_eq_ring_r < 0) {
                    slot = &g_eq_ring_r;
                    clear_message();
                    close_window(depth);
                } else {
                    slot = &g_eq_ring_l;
                    clear_message();
                    close_window(depth);
                }
                if (slot) {
                    unsigned char kind = (unsigned char)iv->mod1;
                    unsigned char eff = ring_effect[kind];
                    *slot = (signed char)idx;
                    ring_identified[kind] = 1;
                    if (eff == RING_EFFECT_POISON) {
                        g_poison_turns = 5;
                        queue_message_str("You put on the %s and feel very sick!", ring_display_name(kind));
                    } else if (eff == RING_EFFECT_SLEEP) {
                        queue_message_str("You put on the %s and suddenly fall asleep!", ring_display_name(kind));
                        end_turn(0, depth);
                        end_turn(0, depth);
                        end_turn(0, depth);
                        queue_message("You wake up");
                        draw_status(depth);
                    } else {
                        /* display the ring's name only after it has been identified, not before */
                        if (slot_is_cursed((signed char)idx))
                            queue_message_str("You put on the %s... it feels wrong!", ring_display_name(kind));
                        else
                            queue_message_str("You put on the %s", ring_display_name(kind));
                    }
                }
            } else {
                clear_message();
                close_window(depth);
            }
        } else {
            clear_message();
            close_window(depth);
        }
    }
    draw_status(depth);
}

/* W - remove item */
void menu_remove(unsigned char depth)
{
    signed char *slot_ptr[6];
    const char *slot_label[6];
    unsigned char slot_used[6];
    unsigned char row, n = 0, i;
    unsigned char count;
    slot_ptr[0] = &g_eq_armor;   slot_label[0] = "Armor";
    slot_ptr[1] = &g_eq_hand_l;  slot_label[1] = "L.Hand";
    slot_ptr[2] = &g_eq_hand_r;  slot_label[2] = "R.Hand";
    slot_ptr[3] = &g_eq_ranged;  slot_label[3] = "Ranged";
    slot_ptr[4] = &g_eq_ring_l;  slot_label[4] = "L.Ring";
    slot_ptr[5] = &g_eq_ring_r;  slot_label[5] = "R.Ring";
    count = 0;
    for (i = 0; i < 6; i++) if (*slot_ptr[i] >= 0) count++;
    open_window("Remove which item?", count ? count : 1);
    row = (unsigned char)(WIN_Y0 + 1);
    for (i = 0; i < 6; i++) {
        if (*slot_ptr[i] >= 0) {
            gotoxy(WIN_LCOL, row++);
            if (g_inventory[*slot_ptr[i]].type == ITEM_RING) {
                char rname[34];
                ring_name_str(rname, (unsigned char)g_inventory[*slot_ptr[i]].mod1, g_inventory[*slot_ptr[i]].mod2, 0);
                cprintf("%c) %s: %s", (char)('a' + n), slot_label[i], rname);
            } else {
                char stat[24];
                item_stat_str_inv(stat, *slot_ptr[i]);
                cprintf("%c) %s: %s %s", (char)('a' + n), slot_label[i],
                    item_name((item_type_t)g_inventory[*slot_ptr[i]].type, g_inventory[*slot_ptr[i]].dice_n, g_inventory[*slot_ptr[i]].dice_s), stat);
            }
            slot_used[n] = i;
            n++;
        }
    }
    if (n == 0) { gotoxy(WIN_LCOL, row); cputs("Nothing equipped"); }
    {
        char sel = read_choice();
        clear_message();
        close_window(depth);
        if (sel >= 'a' && (unsigned char)(sel - 'a') < n) {
            unsigned char si = slot_used[(unsigned char)(sel - 'a')];
            if (slot_is_cursed(*slot_ptr[si])) {
                queue_message("You can't remove it, it's cursed!");
            } else {
                const char *nm = (g_inventory[*slot_ptr[si]].type == ITEM_RING)
                    ? ring_display_name((unsigned char)g_inventory[*slot_ptr[si]].mod1)
                    : item_name((item_type_t)g_inventory[*slot_ptr[si]].type, g_inventory[*slot_ptr[si]].dice_n, g_inventory[*slot_ptr[si]].dice_s);
                queue_message_str("You remove the %s", nm);
                *slot_ptr[si] = -1;
            }
        }
    }
    draw_status(depth);
}

