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

/* Periodic activation of the cursed rings (Poison/Sleep) + the character's
 * starting gear (give_starting_gear and its little helpers) + shuffle
 * material/effect once per game + other small checks, compiled into
 * OVERLAY10, called rarely. */
 
#pragma code-name (push, "OVERLAY10")
#pragma rodata-name (push, "OVERLAY10")

#include <conio.h>
#include <stddef.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include "rng.h"
#include "game_shared.h"

void apply_ring_poison_trigger(void)
{
    g_poison_turns = 5;
    queue_message("The ring poisons you again!");
}

void apply_ring_sleep_trigger(unsigned char depth)
{
    g_ring_sleep_active = 1; /* recursion protection */
    queue_message("The ring puts you to sleep!");
    end_turn(0, depth);
    g_ring_sleep_active = 0;
    queue_message("You wake up");
    draw_status(depth);
}

/* general function for all 'shuffle material/effect once per game' operations: called
 * only when a character is created, along with 'give_starting_gear' and neighbors -
 * the same 'load_overlay_10' covers all five calls. 'identified' may be NULL
 * if there is nothing to reset. */
void shuffle_kind(unsigned char *arr, unsigned char *identified, unsigned char n)
{
    unsigned char i;
    for (i = 0; i < n; i++) {
        arr[i] = i;
        if (identified) identified[i] = 0;
    }
    for (i = 0; i < n; i++) {
        unsigned char j = rnd(n);
        unsigned char tmp = arr[i];
        arr[i] = arr[j];
        arr[j] = tmp;
    }
}

/* offsets of 8 adjacent cells - file-scope visibility (not inside
 * a function: local static const arrays inside overlay functions
 * cause a crash; this is a documented project rule). */
static const signed char adj8_dx[8] = { -1, 0, 1, -1, 1, -1, 0, 1 };
static const signed char adj8_dy[8] = { -1, -1, -1, 0, 0, 1, 1, 1 };

/* is there a merchant nearby (in one of the 8 adjacent cells)? - called only by
 * explicitly pressing the P/S keys (buy/sell), rarely per batch. */
monster_t *merchant_adjacent(void)
{
    unsigned char k;
    for (k = 0; k < 8; k++) {
        signed int nx = (signed int)player.x + adj8_dx[k];
        signed int ny = (signed int)player.y + adj8_dy[k];
        monster_t *m;
        if (nx < 0 || nx >= MAP_W || ny < 0 || ny >= MAP_H) continue;
        m = monster_at((unsigned char)nx, (unsigned char)ny);
        if (m && m->is_merchant) return m;
    }
    return NULL;
}

static void give_melee_hand_r(unsigned char dice_s)
{
    inventory_add(ITEM_WEAPON_MELEE, 0, 0, 1, dice_s, 1, 0); /* def1=1 - equipped immediately, recognized from the start of the game */
    g_eq_hand_r = (signed char)(g_inv_count - 1);
}

static void give_armor(unsigned char kind) /* AC - from armor_ac[kind-1], PV=0 */
{
    inventory_add(ITEM_ARMOR, (signed char)armor_ac[kind - 1], 0, 0, kind, 1, 0); /* def1=1 - equipped immediately, recognized from the start of the game */
    g_eq_armor = (signed char)(g_inv_count - 1);
}

/* knowledge - exactly one-quarter of all types (potions + rings + wands + scrolls
 * combined), rather than an independent 1/4 chance for each (which could have resulted in zero
 * or almost all). Select random unique items from a common pool. */
static void give_knowledge(void)
{
    unsigned char total = (unsigned char)(NUM_POTION_KINDS + NUM_RING_KINDS + NUM_WAND_KINDS + NUM_SCROLL_KINDS);
    unsigned char target = (unsigned char)((total + 2) / 4); /* rounding to the nearest */
    unsigned char picked = 0;
    while (picked < target) {
        unsigned char roll = rnd(total);
        unsigned char *arr;
        unsigned char idx = roll;
        if (idx < NUM_POTION_KINDS) {
            arr = potion_identified;
        } else if ((idx = (unsigned char)(idx - NUM_POTION_KINDS)) < NUM_RING_KINDS) {
            arr = ring_identified;
        } else if ((idx = (unsigned char)(idx - NUM_RING_KINDS)) < NUM_WAND_KINDS) {
            arr = wand_identified;
        } else {
            idx = (unsigned char)(idx - NUM_WAND_KINDS);
            arr = scroll_identified;
        }
        if (!arr[idx]) { arr[idx] = 1; picked++; }
    }
}

/* starting equipment by class - called once when a new
   game begins. The class is selected in menu_new_game() (menus_start.c). */
void give_starting_gear(void)
{
    if (g_player_class == CLASS_STRONGMAN) {
        give_melee_hand_r(6); /* short sword */
        give_armor(5);        /* chainmail */
    } else if (g_player_class == CLASS_TRICKSTER) {
        inventory_add(ITEM_WEAPON_RANGED, 0, 0, 1, 4, 1, 0); /* short bow, def1=1 - put on immediately */
        g_eq_ranged = (signed char)(g_inv_count - 1);
        give_simple(ITEM_AMMO, 0, 0);
        g_inventory[g_inv_count - 1].amount = 20;
        give_armor(2); /* leather armor */
    } else { /* CLASS_INTELLECTUAL */
        unsigned char k;
        give_simple(ITEM_RING, (signed char)rnd(NUM_RING_KINDS), 0);
        for (k = 0; k < 2; k++)
            give_simple((item_type_t)(ITEM_SCROLL_MAPPING + rnd(NUM_SCROLL_KINDS)), 0, 0);
        give_simple(ITEM_WAND, (signed char)rnd(NUM_WAND_KINDS), 5); /* 5 charges */
        give_melee_hand_r(4); /* dagger */
        give_armor(1);        /* robe */
        /* knowledge */
        give_knowledge();
    }
}

/* hall of fame - the top 5 by points, including both winners and those
 * who died together, sorted in descending order. Called only from 'player_die()'
 * and from the victory sequence in main() - neither of these functions EVER
 * returns (both end with exit(0)), so loading overlay 10 from there is safe
 * regardless of where the call came from previously (even if it came from
 * another overlay via end_turn() - there's no need to return there anymore).
 *
 * points: experience (1=1) + max depth x 20 + gold (1=1) + the nominal
 * value of everything in the backpack (via item_price(), do not duplicate
 * gold) + separately for winners - a bonus for finding the ring early:
 * max(220, 620 - 20xfind_level). */

#define HOF_COUNT 5
#define HOF_FILE "RASCAL.HOF"

typedef struct {
    char name[13];
    unsigned long score;
    unsigned char level;
    unsigned char max_depth;
    unsigned char cause_id; /* 0xFF = victory; otherwise 0 = poison, 1 = starvation, 2 = trap,
                               3..14 = monster type (monster_templates[id-3]). */
} hof_entry_t;

/* reason string -> a short ID for saving the record. */
static const char *const fixed_causes[3] = { "poison", "starvation", "a trap" };

static unsigned char cause_to_id(const char *cause)
{
    unsigned char i;
    for (i = 0; i < 3; i++)
        if (strcmp(cause, fixed_causes[i]) == 0) return i;
    for (i = 0; i < NUM_MONSTER_SPECIES; i++)
        if (strcmp(cause + 2, monster_templates[i].name) == 0) return (unsigned char)(3 + i);
    return 0; /* shouldn't happen - just a safe fallback instead of garbage */
}

/* compact ID -> reason text, for display. */
static void cause_to_text(unsigned char id, char *out)
{
    if (id < 3) strcpy(out, fixed_causes[id]);
    else if (id < 3 + NUM_MONSTER_SPECIES) {
        strcpy(out, "a ");
        strcat(out, monster_templates[id - 3].name);
    }
    else out[0] = 0;
}

/* stream-based file operations using lseek() - read/write ONE record at a time;
 * never keep all 5 in memory simultaneously. One temporary record (21 bytes) on
 * the stack; this is safe even when called from deep within the code (our measured
 * peak stack size of 45 bytes, with plenty of headroom, is sufficient). */
static void hof_ensure_file(void)
{
    int f = open(HOF_FILE, O_RDONLY);
    if (f != -1) { close(f); return; } /* already exists */
    f = open(HOF_FILE, O_WRONLY | O_CREAT | O_TRUNC);
    if (f != -1) {
        hof_entry_t empty;
        unsigned char i;
        empty.score = 0;
        empty.name[0] = 0;
        for (i = 0; i < HOF_COUNT; i++) write(f, &empty, sizeof(empty));
        close(f);
    }
}

static unsigned long compute_score(unsigned char won)
{
    unsigned long score = 0;
    unsigned char i;
    score += pstat.xp;
    score += (unsigned long)g_max_depth * 20;
    score += get_gold_total();
    for (i = 0; i < g_inv_count; i++) {
        inv_item_t *it = &g_inventory[i];
        if (it->type == ITEM_GOLD) continue; /* already accounted for via get_gold_total() above */
        {
            unsigned char unit_price = item_price((item_type_t)it->type, it->mod1, it->dice_s, it->def2);
            unsigned char amt = it->amount ? it->amount : 1;
            score += (unsigned long)unit_price * amt;
        }
    }
    if (won) {
        signed int bonus = 620 - 20 * (signed int)g_ring_found_depth;
        if (bonus < 220) bonus = 220;
        score += (unsigned long)bonus;
    }
    return score;
}

/* won: 1 = reached victory (carried the ring), 0 = died (cause - reason,
 * as shown on the death screen, e.g., 'a trap'/'poison'/'a Goblin';
 * ignored when won=1). Call EXACTLY once per game, either from
 * player_die() or from a winning ending. */
void hof_update(unsigned char won, const char *cause)
{
    unsigned long score = compute_score(won);
    hof_entry_t tmp;
    unsigned char pos = HOF_COUNT;
    signed char j;
    int f;

    hof_ensure_file();
    f = open(HOF_FILE, O_RDWR);
    if (f == -1) return;

    {
        unsigned char i;
        for (i = 0; i < HOF_COUNT; i++) {
            lseek(f, (long)i * sizeof(hof_entry_t), SEEK_SET);
            read(f, &tmp, sizeof(hof_entry_t));
            if (score > tmp.score) { pos = i; break; }
        }
    }
    if (pos == HOF_COUNT) { close(f); return; } /* not in the top 5 */

    /* Shift pos..HOF_COUNT-2 down one position, starting from the end -
     * the last entry drops out of the top 5 */
    for (j = HOF_COUNT - 2; j >= (signed char)pos; j--) {
        lseek(f, (long)j * sizeof(hof_entry_t), SEEK_SET);
        read(f, &tmp, sizeof(hof_entry_t));
        lseek(f, (long)(j + 1) * sizeof(hof_entry_t), SEEK_SET);
        write(f, &tmp, sizeof(hof_entry_t));
    }

    strcpy(tmp.name, player_name);
    tmp.score = score;
    tmp.level = pstat.level;
    tmp.max_depth = g_max_depth;
    tmp.cause_id = won ? 0xFF : cause_to_id(cause);
    lseek(f, (long)pos * sizeof(hof_entry_t), SEEK_SET);
    write(f, &tmp, sizeof(hof_entry_t));
    close(f);
}

/* preview screen - accessed from the title screen via a separate
 * menu item. The overlay is loaded by the calling code in menus_start.c. */
void show_hall_of_fame(void)
{
    unsigned char i, row;
    int f = open(HOF_FILE, O_RDONLY);
    hof_entry_t e;
    clrscr();
    gotoxy(0, 0);
    cputs("Hall of Fame");
    row = 2;
    gotoxy(0, row);
    cputs("Pos Name             Score  Lvl  Depth    Result");
    for (i = 0; i < HOF_COUNT; i++) {
        row += 2;
        gotoxy(0, row);
        if (f == -1 || read(f, &e, sizeof(e)) != sizeof(e) || (e.score == 0 && e.name[0] == 0)) {
            cprintf("%u.  (empty)", i + 1);
        } else if (e.cause_id == 0xFF) {
            cprintf("%u.  %-16s %5lu  %3u  %5u    Won", i + 1, e.name, e.score,
                e.level, e.max_depth);
        } else {
            char causebuf[16];
            cause_to_text(e.cause_id, causebuf);
            cprintf("%u.  %-16s %5lu  %3u  %5u    Killed by %s", i + 1, e.name, e.score,
                e.level, e.max_depth, causebuf);
        }
    }
    if (f != -1) close(f);
    gotoxy(0, 23);
    cputs("Press any key");
    cgetc();
    clrscr();
}

/* show_end_stats/show_death_screen/show_win_screen - moved here from
 * menus_end.c (OVERLAY4) so that handle_player_death()/handle_player_win()
 * below could call both the score and the screen in a SINGLE load of this overlay,
 * instead of two in a row (first OVERLAY10 for the score, then OVERLAY4 for
 * the screen). The actual copy of 'Press any key to continue' - the one in
 * menus_end.c - is still used there by the other three screens. */
static void show_end_stats(const char *closing_msg)
{
    unsigned char row = 4;

    gotoxy(0, row++);
    cprintf("Reached dungeon level %u.", g_max_depth);
    gotoxy(0, row++);
    cprintf("Experience level %u (%lu XP), %u gold.", pstat.level, pstat.xp, get_gold_total());
    row++;

    gotoxy(0, row++);
    cputs("Equipment:");
    {
        signed char *slot_ptr[6];
        const char *slot_label[6];
        unsigned char k;
        slot_ptr[0] = &g_eq_armor;   slot_label[0] = "Armor ";
        slot_ptr[1] = &g_eq_hand_r;  slot_label[1] = "R.Hand";
        slot_ptr[2] = &g_eq_hand_l;  slot_label[2] = "L.Hand";
        slot_ptr[3] = &g_eq_ranged;  slot_label[3] = "Ranged";
        slot_ptr[4] = &g_eq_ring_r;  slot_label[4] = "R.Ring";
        slot_ptr[5] = &g_eq_ring_l;  slot_label[5] = "L.Ring";
        for (k = 0; k < 6; k++) {
            gotoxy(2, row++);
            if (*slot_ptr[k] >= 0) {
                inv_item_t *it = &g_inventory[*slot_ptr[k]];
                if (it->type == ITEM_RING) {
                    cprintf("%s: %s", slot_label[k], ring_display_name((unsigned char)it->mod1));
                } else {
                    char stat[24];
                    item_stat_str_inv(stat, *slot_ptr[k]);
                    cprintf("%s: %s %s", slot_label[k], item_name((item_type_t)it->type, it->dice_n, it->dice_s), stat);
                }
            } else {
                cprintf("%s: (empty)", slot_label[k]);
            }
        }
    }

    row++;
    revers(0); /* safety measure - if a crash occurs in the middle of a turn, the standard 'draw_status()'
                * might not have had time to disable video inversion before this screen is displayed */
    gotoxy(0, row);
    cputs(closing_msg);
    cputs("\r\n");
    cgetc();
}

void show_death_screen(const char *cause)
{
    clrscr();
    gotoxy(0, 0);
    cprintf("%s has died", player_name);
    gotoxy(0, 2);
    cprintf("Killed by %s", cause);
    show_end_stats("Better luck next time");
}

/* victory: ring has been retrieved from Level 1 and brought to the surface. */
void show_win_screen(void)
{
    clrscr();
    gotoxy(0, 0);
    cprintf("%s has won!", player_name);
    gotoxy(0, 2);
    cputs("Recovered the Ring of the Apple Signet and escaped the dungeon!");
    show_end_stats("Press any key");
}

/* Unifying overlays called from player_die()/the victory ending in
   main.c with EXACTLY one load_overlay10_safe(), instead of two separate
   loads (first for the score, then for the screen). */
void handle_player_death(const char *cause)
{
    hof_update(0, cause);
    show_death_screen(cause);
    show_hall_of_fame(); /* so that the player is sure to see it at least once */
}

void handle_player_win(void)
{
    hof_update(1, NULL);
    show_win_screen();
    show_hall_of_fame(); /* so that the player is sure to see it at least once */
}

#pragma code-name (pop)
#pragma rodata-name (pop)
