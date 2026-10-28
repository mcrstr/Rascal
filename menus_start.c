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

/* Start screen: enter a name, choose class, generate a seed + save/load
 * + read scroll. Compiled as a separate overlay slot (OVERLAY5), needed
 * only once at startup; after that, it is not kept in memory. */

#include <conio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "game_shared.h"
#include "rng.h"

static void menu_new_game(void); /* forward, needed in menu_title */

static const char *save_slot_name(unsigned char slot)
{
    switch (slot) {
        case 1:  return "RASCAL.S1";
        case 2:  return "RASCAL.S2";
        default: return "RASCAL.S3";
    }
}

/* save the entire party's state to a single file: map, inventory, monsters,
 * items on the ground, identification, and the RNG seed (so that the random
 * number stream continues exactly from the same point). */
typedef struct { void *ptr; unsigned int size; } save_field_t;
static const save_field_t save_fields[] = {
    { player_name, sizeof(player_name) },
    { &pstat, sizeof(pstat) },
    { &player, sizeof(player) },
    { &g_depth, 1 },
    { &g_max_depth, 1 },
    { &g_stairs_inverted, 1 },
    { &g_has_signet, 1 },
    { g_map, sizeof(g_map) },
    { g_seen, sizeof(g_seen) },
    { g_zones, sizeof(g_zones) },
    { &g_inv_count, 1 },
    { g_inventory, sizeof(g_inventory) },
    { &g_ground_count, 1 },
    { g_ground_items, sizeof(g_ground_items) },
    { &g_monster_count, 1 },
    { g_monsters, sizeof(g_monsters) },
    { &g_eq_armor, 1 },
    { &g_eq_hand_l, 1 },
    { &g_eq_hand_r, 1 },
    { &g_eq_ranged, 1 },
    { &g_eq_ring_l, 1 },
    { &g_eq_ring_r, 1 },
    { potion_effect, sizeof(potion_effect) },
    { potion_identified, sizeof(potion_identified) },
    { ring_effect, sizeof(ring_effect) },
    { ring_identified, sizeof(ring_identified) },
    { wand_effect, sizeof(wand_effect) },
    { wand_identified, sizeof(wand_identified) },
};
#define NUM_SAVE_FIELDS (sizeof(save_fields) / sizeof(save_fields[0]))

static void save_load_fields(int f, unsigned char writing)
{
    unsigned char i;
    for (i = 0; i < NUM_SAVE_FIELDS; i++) {
        if (writing) write(f, save_fields[i].ptr, save_fields[i].size);
        else read(f, save_fields[i].ptr, save_fields[i].size);
    }
}

static unsigned char save_game(unsigned char slot)
{
    int f = open(save_slot_name(slot), O_WRONLY | O_CREAT | O_TRUNC);
    unsigned int rs;
    if (f == -1) return 0;
    save_load_fields(f, 1);
    rs = rng_get_state();
    write(f, &rs, sizeof(rs));
    close(f);
    return 1;
}

/* load a game, the same field table as in save_game(). */
static unsigned char load_game(unsigned char slot)
{
    int f = open(save_slot_name(slot), O_RDONLY);
    unsigned int rs;
    if (f == -1) return 0;
    save_load_fields(f, 0);
    read(f, &rs, sizeof(rs));
    rng_set_state(rs);
    close(f);
    unlink(save_slot_name(slot)); /* permadeath */
    return 1;
}

/* Save slot selection screen (Q in-game). Returns 1 if the game has been
 * saved and you can exit; 0 indicates cancellation or an error, and the game
 * continues (permadeath: cannot lose progress due to a save failure). */
static unsigned char save_preview(unsigned char slot, char *name_out,
    unsigned char *level_out, unsigned char *hp_out, unsigned char *hpmax_out, unsigned char *depth_out); /* forward, needed in save_game_prompt */

/* common section of the save and load screens - a list of three slots with previews and key selection. */
static unsigned char pick_save_slot(const char *prompt)
{
    char k;
    unsigned char i;
    clrscr();
    gotoxy(0, 0);
    cputs(prompt);
    for (i = 1; i <= 3; i++) {
        char nm[16];
        unsigned char lvl, hp, hpmax, depth;
        gotoxy(0, (unsigned char)(1 + i));
        cprintf("%u) ", i);
        if (save_preview(i, nm, &lvl, &hp, &hpmax, &depth))
            cprintf("%s  Lvl:%u  HP:%u/%u  Depth:%u", nm, lvl, hp, hpmax, depth);
        else
            cputs("No save");
    }
    gotoxy(0, 6);
    cputs("Any key to cancel");
    k = cgetc();
    if (k == '1') return 1;
    if (k == '2') return 2;
    if (k == '3') return 3;
    return 0;
}

unsigned char save_game_prompt(void)
{
    unsigned char slot = pick_save_slot("Save slot?");
    if (!slot) return 0;

    {
        int f = open(save_slot_name(slot), O_RDONLY);
        if (f != -1) {
            char k;
            close(f);
            gotoxy(0, 8);
            cputs("Overwrite? (y/n)");
            k = cgetc();
            if (k != 'y' && k != 'Y') return 0;
        }
    }

    if (save_game(slot)) {
        gotoxy(0, 10);
        cputs("Saved. Press any key");
        cgetc();
        return 1;
    }
    gotoxy(0, 10);
    cputs("Failed! Press any key");
    cgetc();
    return 0;
}

/* view the save file without affecting the current game session: only the name,
   character level, HP, and depth to preview it in the slot list. */
static unsigned char save_preview(unsigned char slot, char *name_out,
    unsigned char *level_out, unsigned char *hp_out, unsigned char *hpmax_out, unsigned char *depth_out)
{
    int f = open(save_slot_name(slot), O_RDONLY);
    player_stats_t ps;
    unsigned char skip_pos[2];
    unsigned char d;
    if (f == -1) return 0;
    read(f, name_out, 16);
    read(f, &ps, sizeof(ps));
    read(f, skip_pos, 2); /* player's position on the map isn't needed here */
    read(f, &d, 1);
    close(f);
    *level_out = ps.level;
    *hp_out = ps.hp;
    *hpmax_out = ps.hp_max;
    *depth_out = d;
    return 1;
}

/* slot selection screen when loading from the start menu. Returns 1
   if the game has loaded and can start immediately (main() skips the usual
   generation of the first level). */
static unsigned char load_game_prompt(void)
{
    unsigned char slot = pick_save_slot("Load slot?");
    if (!slot) return 0;

    if (load_game(slot)) return 1;
    gotoxy(0, 8);
    cputs("No save. Press any key");
    cgetc();
    return 0;
}

/* Start menu: New Game / Load / Help. No need to exit - just turn off or
 * restart the Apple II. Returns 1 if the game has been loaded (main() should
 * skip the usual initialization and the generation of the first level - the
 * state has already been restored), 0 - a normal new game.
 * draw_credit_line() is built right into menu_title(). */

unsigned char menu_title(void)
{
    for (;;) {
        char ch;
        clrscr();
        gotoxy(0, 0);
        cputs("Rascal: Beneath the Apple Signet");
        gotoxy(0, 2);
        cputs("1) Start a new game");
        gotoxy(0, 3);
        cputs("2) Load previous game");
        gotoxy(0, 4);
        cputs("3) Help & Credits");
        gotoxy(0, 5);
        cputs("4) Hall of Fame");
        gotoxy(0, 7);
        cputs("Choose:");
        gotoxy(0, 23);
        cputs("Copyright (c) 2026 mcrstr v1.0");
        ch = cgetc();
        if (ch == '1') {
            menu_new_game();
            return 0;
        } else if (ch == '2') {
            if (load_game_prompt()) return 1;
        } else if (ch == '3') {
            return 2; /* 'Show instructions and return to title screen' - see main() */
        } else if (ch == '4') {
            return 3; /* 'Show HoF and take back to title screen' - see main() */
        }
    }
}

static void menu_new_game(void)
{
    unsigned char len = 0;
    char ch;
    unsigned int spin;
    unsigned long seed;
    unsigned char i;

    clrscr();
    gotoxy(0, 0);
    cputs("Rascal: Beneath the Apple Signet");
    gotoxy(0, 2);
    cputs("Your name?");
    gotoxy(0, 4);

    for (;;) {
        ch = cgetc();
        if (ch == 13 && len > 0) break; /* enter - the name cannot be empty */
        if ((ch == 8 || ch == 127) && len > 0) { /* backspace */
            len--;
            gotoxy(len, 4);
            cputc(' ');
            gotoxy(len, 4);
            continue;
        }
        if (((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || ch == ' ') && len < 12) {
            player_name[len] = ch;
            len++;
            cputc(ch);
        }
    }
    player_name[len] = '\0';

    gotoxy(0, 6);
    cputs("Who are you?");
    gotoxy(0, 8);
    cputs("1) Strongman");
    gotoxy(0, 9);
    cputs("2) Trickster");
    gotoxy(0, 10);
    cputs("3) Intellectual");
    gotoxy(0, 12);
    cputs("Choose:");
    for (;;) {
        ch = cgetc();
        if (ch == '1') { g_player_class = CLASS_STRONGMAN; cputs(" Strongman"); break; }
        if (ch == '2') { g_player_class = CLASS_TRICKSTER; cputs(" Trickster"); break; }
        if (ch == '3') { g_player_class = CLASS_INTELLECTUAL; cputs(" Intellectual"); break; }
    }

    /* seed = a hash of the name, mixed (XORed) with the wait time before the keypress
	 * for the next keystroke - this is why dungeons differ even for characters with the same name */
    seed = 0;
    for (i = 0; i < len; i++) seed = seed * 31 + (unsigned char)player_name[i];

    gotoxy(0, 14);
    cputs("Press any key");
    spin = 0;
    while (!kbhit()) spin++;
    cgetc();
    seed ^= spin;
    rng_seed((unsigned int)(seed & 0xFFFFUL));

    clrscr();
}

/* r - read a scroll. Unlike potions, rings and wands, scrolls are not 'unidentified' -
 * their name is visible immediately and each kind has its own item_type_t. is_scroll()
 * is resident in main.c; the identify-target selection (can_be_unidentified() etc.)
 * lives in spawn.c (OVERLAY6) and is launched from main() after this menu returns. */
void menu_read(unsigned char depth)
{
    unsigned char row, i, count = 0;
    for (i = 0; i < g_inv_count; i++)
        if (is_scroll((item_type_t)g_inventory[i].type)) count++;
    open_window("Read what?", count ? count : 1);
    row = (unsigned char)(WIN_Y0 + 1);
    for (i = 0; i < g_inv_count; i++) {
        if (is_scroll((item_type_t)g_inventory[i].type)) {
            gotoxy(WIN_LCOL, row++);
            cprintf("%c) %s", (char)('a' + i), item_name((item_type_t)g_inventory[i].type, 0, 0));
        }
    }
    if (count == 0) { gotoxy(WIN_LCOL, row); cputs("No scrolls"); }
    {
        char sel = read_choice();
        if (sel >= 'a' && (unsigned char)(sel - 'a') < g_inv_count &&
            is_scroll((item_type_t)g_inventory[(unsigned char)(sel - 'a')].type)) {
            unsigned char idx = (unsigned char)(sel - 'a');
            item_type_t t = (item_type_t)g_inventory[idx].type;
            unsigned char player_slept = 0;
            scroll_identified[(unsigned char)(t - ITEM_SCROLL_MAPPING)] = 1;
            clear_message();
            close_window(depth);
            switch (t) {
                case ITEM_SCROLL_MAPPING: {
                    unsigned char bx, by;
                    for (by = 0; by < MAP_H; by++) {
                        for (bx = 0; bx < MAP_W; bx++) {
                            zone_t *bz = room_at(bx, by);
                            if (bz && (bz->flags & RF_DARK) && (tile_t)g_map[by][bx] == T_FLOOR)
                                continue;
                            set_seen(bx, by);
                        }
                    }
                    render_full(depth); /* redraw the entire screen */
                    queue_message("You read a scroll of magic mapping");
                    break;
                }
                case ITEM_SCROLL_SLEEP: {
                    unsigned char monsters_slept = sleep_nearby_monsters(player.x, player.y);
                    player_slept = (rnd(2) == 0);
                    if (player_slept)
                        queue_message(monsters_slept ? "You & monsters sleep!" : "You sleep!");
                    else
                        queue_message(monsters_slept ? "Monsters sleep!" : "Nothing happens");
                    break; /* player's sleep phase - three end_turn() calls later, if player_slept */
                }
                case ITEM_SCROLL_CREATE_FOOD: {
                    unsigned char amt = (unsigned char)(1 + rnd(3));
                    unsigned char k = find_or_add_stack(ITEM_FOOD, 0);
                    if (k != 0xFF) g_inventory[k].amount += amt;
                    queue_message("Food appears!");
                    break;
                }
                case ITEM_SCROLL_AGGRAVATE: {
                    unsigned char k;
                    for (k = 0; k < g_monster_count; k++)
                        if (g_monsters[k].alive) g_monsters[k].awake = 1;
                    queue_message("A scream sounds!");
                    break;
                }
                case ITEM_SCROLL_IDENTIFY:
                    g_pending_identify = 1; /* selecting the target itself is a separate, safe step within main() */
                    break;
                case ITEM_SCROLL_REMOVE_CURSE:
                    remove_curse_scroll_effect(depth);
                    break;
                default: break;
            }
            inventory_remove(idx);
            draw_status(depth);
            if (t == ITEM_SCROLL_SLEEP && player_slept) {
                end_turn(0, depth);
                end_turn(0, depth);
                end_turn(0, depth);
                queue_message("You wake up");
                draw_status(depth);
            }
        } else {
            clear_message();
            close_window(depth);
        }
    }
}