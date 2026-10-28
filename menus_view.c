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
 
/* Item menu screens - compiled into OVERLAY1 and loaded from disk on
 * demand (see main.c: load_menu_overlay). They can freely call
 * resident functions; overlays cannot call each other. */

#include <conio.h>
#include <stdio.h>
#include "game_shared.h"

/* display a single equipment slot on the character screen - 
 * a general function for Armor/L.Hand/R.Hand/Ranged/L.Ring/R.Ring. */
static void print_slot(unsigned char col, unsigned char row, const char *label, signed char idx)
{
    gotoxy(col, row);
    if (idx >= 0) {
        item_type_t t = (item_type_t)g_inventory[idx].type;
        if (t == ITEM_RING) {
            char rname[34];
            ring_name_str(rname, (unsigned char)g_inventory[idx].mod1, g_inventory[idx].mod2, 0);
            cprintf("%s: %s", label, rname);
        } else {
            char stat[24];
            item_stat_str_inv(stat, idx);
            cprintf("%s: %s %s", label, item_name(t, g_inventory[idx].dice_n, g_inventory[idx].dice_s), stat);
        }
    } else {
        cprintf("%s: -- empty --", label);
    }
}

/* summary of the current melee attack for the character screen: hit bonus
 * and damage recorded as NdS + bonus (or unarmed, if no weapon is equipped). */
static void sign_mag(signed int v, char *sign, unsigned char *mag)
{
    *sign = (v < 0) ? '-' : '+';
    *mag = (unsigned char)((v < 0) ? -v : v);
}

static void melee_summary(char *buf)
{
    signed int bonus = str_bonus_hit(effective_strength());
    signed int dmgflat = str_bonus_dmg(effective_strength());
    unsigned char has_l = 0, has_r = 0;
    unsigned char dn_l = 0, ds_l = 0, dn_r = 0, ds_r = 0;
    char bs, ds2;
    unsigned char ba, da;
    if (g_eq_hand_l >= 0 && g_inventory[g_eq_hand_l].type == ITEM_WEAPON_MELEE) {
        bonus += g_inventory[g_eq_hand_l].mod1;
        dn_l = g_inventory[g_eq_hand_l].dice_n;
        ds_l = g_inventory[g_eq_hand_l].dice_s;
        dmgflat += g_inventory[g_eq_hand_l].mod2;
        has_l = 1;
    }
    if (g_eq_hand_r >= 0 && g_inventory[g_eq_hand_r].type == ITEM_WEAPON_MELEE) {
        bonus += g_inventory[g_eq_hand_r].mod1;
        dn_r = g_inventory[g_eq_hand_r].dice_n;
        ds_r = g_inventory[g_eq_hand_r].dice_s;
        dmgflat += g_inventory[g_eq_hand_r].mod2;
        has_r = 1;
    }
    bonus += 2 * ring_worn_effect(RING_EFFECT_DEXTERITY);
    bonus += g_dex_bonus;
    if (g_player_class == CLASS_STRONGMAN) bonus += 1 + (signed int)(pstat.level / 2);
    else if (g_player_class == CLASS_INTELLECTUAL) bonus -= 1;
    if (has_l && has_r) bonus -= (g_player_class == CLASS_STRONGMAN ? 1 : 2); /* same rule as in combat */
    bonus -= (signed int)(armor_pv() / 3);
    dmgflat += 2 * ring_worn_effect(RING_EFFECT_DAMAGE);
    dmgflat += g_dmg_bonus;
    sign_mag(bonus, &bs, &ba);
    sign_mag(dmgflat, &ds2, &da);
    if (!has_l && !has_r) {
        sprintf(buf, "%c%u to hit, 1d2%c%u dmg (bare hands)", bs, ba, ds2, da);
    } else if (has_l && has_r && ds_l == ds_r) {
        /* same dice for both hands */
        sprintf(buf, "%c%u to hit, %ud%u%c%u dmg", bs, ba, (unsigned char)(dn_l + dn_r), ds_l, ds2, da);
    } else if (has_l && has_r) {
        /* different weapons in each hand - different dice, can't combine them into a single NdS */
        sprintf(buf, "%c%u to hit, %ud%u+%ud%u%c%u dmg", bs, ba, dn_l, ds_l, dn_r, ds_r, ds2, da);
    } else {
        unsigned char dn = has_l ? dn_l : dn_r;
        unsigned char ds = has_l ? ds_l : ds_r;
        sprintf(buf, "%c%u to hit, %ud%u%c%u dmg", bs, ba, dn, ds, ds2, da);
    }
}

/* whether inventory item idx is equipped in any equipment slot - its own
 * local helper (other overlays have similar ones, each with its own). */
static unsigned char is_equipped(unsigned char idx)
{
    return (unsigned char)g_eq_armor == idx || (unsigned char)g_eq_hand_l == idx ||
           (unsigned char)g_eq_hand_r == idx || (unsigned char)g_eq_ranged == idx ||
           (unsigned char)g_eq_ring_l == idx || (unsigned char)g_eq_ring_r == idx;
}

/* i - backpack: a list of unequipped items, displayed page by page (if it doesn't
 * fit in the window all at once - for example, when the backpack is nearly full
 * and the player is wearing the minimum number of items). The backpack limit remains
 * the same as the total inventory limit (see main.c: MAX_INVENTORY) -
 * just displaying it across several pages. */
void menu_backpack(unsigned char depth)
{
    unsigned char page = 0;
    unsigned char per_page = (unsigned char)(WIN_Y1 - WIN_Y0 - 2);
    unsigned char count = 0, ci;
    for (ci = 0; ci < g_inv_count; ci++) if (!is_equipped(ci)) count++;

    for (;;) {
        unsigned char row, i, bp_index = 0, shown = 0, has_more = 0, any = 0;
        unsigned char page_start = (unsigned char)(page * per_page);
        unsigned char page_count = page_size(count, per_page, page);
        char stat[24];

        open_window("Backpack", page_count ? page_count : 1);
        row = (unsigned char)(WIN_Y0 + 1);
        for (i = 0; i < g_inv_count; i++) {
            if (is_equipped(i)) continue;
            any = 1;
            if (bp_index < page_start) { bp_index++; continue; }
            if (shown >= per_page) { has_more = 1; break; }
            gotoxy(WIN_LCOL, row++);
            {
                inv_item_t *it = &g_inventory[i];
                if (it->type == ITEM_GOLD) {
                    cprintf("%c) Gold (%u)", (char)('a' + i), it->amount);
                } else if (it->type == ITEM_FOOD) {
                    cprintf("%c) %s (%u)", (char)('a' + i),
                        item_name((item_type_t)it->type, 0, 0), it->amount);
                } else if (it->type == ITEM_AMMO) {
                    cprintf("%c) %s (%u)", (char)('a' + i),
                        ammo_display_name((unsigned char)it->mod1), it->amount);
                } else if (it->type == ITEM_POTION) {
                    cprintf("%c) %s", (char)('a' + i), potion_display_name((unsigned char)it->mod1));
                } else if (it->type == ITEM_RING) {
                    char rname[34];
                    ring_name_str(rname, (unsigned char)it->mod1, it->mod2, 0);
                    cprintf("%c) %s", (char)('a' + i), rname);
                } else if (it->type == ITEM_WAND) {
                    if (wand_identified[(unsigned char)it->mod1])
                        cprintf("%c) %s (%u)", (char)('a' + i),
                            wand_display_name((unsigned char)it->mod1), (unsigned)it->mod2);
                    else
                        cprintf("%c) %s", (char)('a' + i), wand_display_name((unsigned char)it->mod1));
                } else if (it->type == ITEM_TREASURE) {
                    cprintf("%c) %s", (char)('a' + i), treasure_display_name((unsigned char)it->mod1));
                } else {
                    item_stat_str_inv(stat, i);
                    cprintf("%c) %s %s", (char)('a' + i), item_name((item_type_t)it->type, it->dice_n, it->dice_s), stat);
                }
            }
            bp_index++;
            shown++;
        }
        if (!any) { gotoxy(WIN_LCOL, row); cputs("Backpack empty"); }
        read_page_continue(has_more);
        if (has_more) { page++; continue; }
        break;
    }
    close_window(depth);
}

/* Summary of long-range attacks for the character screen - equivalent to 'melee_summary()'
 * earlier in the file. The formula must match the actual calculation in 'menu_fire()' (menus_action3.c). */
static void ranged_summary(char *buf)
{
    if (g_eq_ranged < 0) {
        sprintf(buf, "no ranged weapon");
        return;
    }
    {
        signed int bonus = str_bonus_hit(effective_strength()) +
            g_inventory[g_eq_ranged].mod1;
        signed int dmgflat = g_inventory[g_eq_ranged].mod2;
        unsigned char dn = g_inventory[g_eq_ranged].dice_n;
        unsigned char ds = g_inventory[g_eq_ranged].dice_s;
        char bs, ds2;
        unsigned char ba, da;
        if (g_player_class == CLASS_TRICKSTER) bonus += 1 + (signed int)(pstat.level / 2);
        else if (g_player_class == CLASS_STRONGMAN) bonus -= 1;
        else if (g_player_class == CLASS_INTELLECTUAL) bonus -= 1;
        bonus += 2 * ring_worn_effect(RING_EFFECT_DEXTERITY);
        bonus += g_dex_bonus;
        dmgflat += 2 * ring_worn_effect(RING_EFFECT_DAMAGE);
        dmgflat += g_dmg_bonus;
        sign_mag(bonus, &bs, &ba);
        sign_mag(dmgflat, &ds2, &da);
        sprintf(buf, "%c%u to hit, %ud%u%c%u dmg", bs, ba, dn, ds, ds2, da);
    }
}

/* file scope, NOT inside menu_character() - a local static const array inside
 * an overlay function crashes the program; this is the same
 * documented architectural rule hit when this class-name table was added. */
static const char *const class_names[3] = { "Strongman", "Trickster", "Intellectual" };

/* @ - information about the character and the items they are wearing or holding. */
void menu_character(unsigned char depth)
{
    char summ[32];
    char title[32];
    sprintf(title, "%s, the %s", player_name, class_names[g_player_class]);
    open_window(title, (unsigned char)(WIN_Y1 - WIN_Y0)); /* fixed size, does not adjust */
    gotoxy(WIN_LCOL, (unsigned char)(WIN_Y0 + 1));
    cprintf("Level %u  Experience %lu/%lu", pstat.level, pstat.xp, xp_threshold(pstat.level));
    gotoxy(WIN_LCOL, (unsigned char)(WIN_Y0 + 2));
    cprintf("Hit Points %u/%u  Strength %u", pstat.hp, pstat.hp_max, effective_strength());
    gotoxy(WIN_LCOL, (unsigned char)(WIN_Y0 + 3));
    cprintf("Armor Class %u  Protection Value %d", effective_defense(), armor_pv());
    melee_summary(summ);
    gotoxy(WIN_LCOL, (unsigned char)(WIN_Y0 + 4));
    cprintf("Melee attack: %s", summ);
    ranged_summary(summ);
    gotoxy(WIN_LCOL, (unsigned char)(WIN_Y0 + 5));
    cprintf("Ranged attack: %s", summ);
    print_slot(WIN_LCOL, (unsigned char)(WIN_Y0 + 7), "Right hand", g_eq_hand_r);
    print_slot(WIN_LCOL, (unsigned char)(WIN_Y0 + 8), "Left hand ", g_eq_hand_l);
    print_slot(WIN_LCOL, (unsigned char)(WIN_Y0 + 9), "Ranged    ", g_eq_ranged);
    print_slot(WIN_LCOL, (unsigned char)(WIN_Y0 + 10), "Armor     ", g_eq_armor);
    print_slot(WIN_LCOL, (unsigned char)(WIN_Y0 + 11), "Right ring", g_eq_ring_r);
    print_slot(WIN_LCOL, (unsigned char)(WIN_Y0 + 12), "Left ring ", g_eq_ring_l);
    gotoxy(0, 0);
    cclear(80);
    gotoxy(0, 0);
    cputs("Press any key");
    cgetc();
    clear_message();
    close_window(depth);
}