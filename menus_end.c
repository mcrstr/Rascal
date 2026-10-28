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

/* Key list screens, icon legends, help & credits: compiled
 * as a separate overlay slot (OVERLAY4). */

#include <conio.h>
#include <stdio.h>
#include "game_shared.h"
#include "rng.h"

/* message 'Press any key to continue' is a common string instead of three
 * separate instances of the same text in different functions in the file:
 * cc65 does not collapse identical literals on its own. */
static const char msg_press_continue[] = "Press any key";

/* ? - a list of keys, arranged in two columns to fit on a single page. */
void menu_help(unsigned char depth)
{
    unsigned char row = (unsigned char)(WIN_Y0 + 1);
    open_window("Keys", (unsigned char)(WIN_Y1 - WIN_Y0 - 1)); /* fixed size - the content is always the same */

    gotoxy(WIN_LCOL, row);
    cputs("g,");
    gotoxy((unsigned char)(WIN_LCOL + 2), row);	/* synchronize the firmware cursor before MouseText */
    put_mousetext((char)72); /* left arrow */
    gotoxy((unsigned char)(WIN_LCOL + 3), row);
    cputs("/j,");
    gotoxy((unsigned char)(WIN_LCOL + 6), row);	/* synchronize the firmware cursor before MouseText */
    put_mousetext((char)85); /* right arrow */
    gotoxy((unsigned char)(WIN_LCOL + 12), row);
    cputs("Move left/right");
    row++;

    gotoxy(WIN_LCOL, row);
    cputs("y,");
    gotoxy((unsigned char)(WIN_LCOL + 2), row);	/* synchronize the firmware cursor before MouseText */
    put_mousetext((char)75); /* up arrow */
    gotoxy((unsigned char)(WIN_LCOL + 3), row);
    cputs("/n,");
    gotoxy((unsigned char)(WIN_LCOL + 6), row);	/* synchronize the firmware cursor before MouseText */
    put_mousetext((char)74); /* down arrow */
    gotoxy((unsigned char)(WIN_LCOL + 12), row);
    cputs("Move up/down");
    row++;

    gotoxy(WIN_LCOL, row);   cputs("t,u,m,b");
    gotoxy((unsigned char)(WIN_LCOL + 12), row); cputs("Move diagonally"); row++;
    gotoxy(WIN_LCOL, row);   cputs("Shift+dir   Run");          row++;
    gotoxy(WIN_LCOL, row);   cputs("h           Wait");         row++;
    gotoxy(WIN_LCOL, row);   cputs("R           Last message"); row++;
    gotoxy(WIN_LCOL, row);   cputs("!           Help & credits");row++;
    gotoxy(WIN_LCOL, row);   cputs("@           Character info");row++;
    gotoxy(WIN_LCOL, row);   cputs("Q           Save & quit");  row++;
    gotoxy(WIN_LCOL, row);   cputs("k           Known items list");row++;
    gotoxy(WIN_LCOL, row);   cputs("L           Map legend");   row++;
    gotoxy(WIN_LCOL, row);   cputs("i           Inventory");

    row = (unsigned char)(WIN_Y0 + 1);
    gotoxy(WIN_RCOL, row);   cputs("s      Search");        row++;
    gotoxy(WIN_RCOL, row);   cputs("w      Equip");         row++;
    gotoxy(WIN_RCOL, row);   cputs("W      Unequip");       row++;
    gotoxy(WIN_RCOL, row);   cputs("d      Drop");          row++;
    gotoxy(WIN_RCOL, row);   cputs("e      Eat food");      row++;
    gotoxy(WIN_RCOL, row);   cputs("q      Quaff potion");  row++;
    gotoxy(WIN_RCOL, row);   cputs("r      Read scroll");   row++;
    gotoxy(WIN_RCOL, row);   cputs("z      Zap wand");      row++;
    gotoxy(WIN_RCOL, row);   cputs("f      Fire arrow");    row++;
    gotoxy(WIN_RCOL, row);   cputs("P      Buy");           row++;
    gotoxy(WIN_RCOL, row);   cputs("S      Sell");          row++;
    gotoxy(WIN_RCOL, row);   cputs(">      Use stairs");

    gotoxy(0, 0);
    cclear(80);
    gotoxy(0, 0);
    cputs(msg_press_continue);
    cgetc();
    clear_message();
    close_window(depth);
}

/* L - map icon legend (see main.c). The corridor and the trap
 * are drawn using MouseText, processed in a loop. */
typedef struct { const char *glyph; const char *name; } legend_entry_t;
static const legend_entry_t legend_table[] = {
    {"@", "Adventurer"},
    {"a - Z", "Monster"},
    {"M", "Merchant"},
    {".", "Floor"},
    {"| / -", "Wall"},
    {"+", "Door"},
    {NULL, "Corridor"},
    {NULL, "Trap"},
    {">", "Stairs down"},
    {"<", "Stairs up"},
    {"$", "Gold"},
    {")", "Melee weapon"},
    {"}", "Bow"},
    {"]", "Armor"},
    {"(", "Shield"},
    {"=", "Ring"},
    {"/", "Wand"},
    {"~", "Scroll"},
    {"!", "Potion"},
    {"%", "Food"},
    {"\\", "Arrow"},
    {"*", "Treasure"},
};
#define LEGEND_COUNT (sizeof(legend_table) / sizeof(legend_table[0]))
#define LEGEND_LEFT ((LEGEND_COUNT + 1) / 2)

void menu_legend(unsigned char depth)
{
    unsigned char i, row, col;
    open_window("Map Legend", LEGEND_LEFT); /* height exactly under the content - not WIN_Y1-WIN_Y0-1 */
    row = (unsigned char)(WIN_Y0 + 1);
    for (i = 0; i < LEGEND_COUNT; i++) {
        col = (i < LEGEND_LEFT) ? WIN_LCOL : WIN_RCOL;
        if (i == LEGEND_LEFT) row = (unsigned char)(WIN_Y0 + 1);
        gotoxy(col, row++);
        if (legend_table[i].glyph == NULL) {
            /* The corridor/trap is drawn using MouseText (see output_tile in main.c);
             * Distinguish them by the first letter of their names -
			 * it's more efficient than adding an extra field to the structure */
            put_mousetext(legend_table[i].name[0] == 'C' ? (char)87 : (char)91);
            gotoxy((unsigned char)(col + 6), (unsigned char)(row - 1));
            cprintf("%s", legend_table[i].name);
        } else {
            cprintf("%-5s %s", legend_table[i].glyph, legend_table[i].name);
        }
    }
    gotoxy(0, 0);
    cclear(80);
    gotoxy(0, 0);
    cputs(msg_press_continue);
    cgetc();
    clear_message();
    close_window(depth);
}

/* Help & Credits - a single screen for all 24 lines */
void show_instructions(void)
{
    clrscr();
    cputs("Help & Credits\r\n");
    cputs("Rascal is a roguelike game heavily inspired by classic Rogue. I think the\r\n");
    cputs("wonderful Apple II deserves a game that truly represents this genre.\r\n");
    cputs("In Rascal, you are a brave adventurer who has set out to find the Ring of the\r\n");
    cputs("Apple Signet, hidden somewhere deep in the dungeon. Minions of evil will stand\r\n");
    cputs("in your way, but you'll overcome all challenges, discover the Ring, and make\r\n");
    cputs("your way back to the surface with it. \r\n");
    cputs("\r\n");
    cputs("This is a turn-based game, so take your time and think about each move. On ev-\r\n");
    cputs("ery level of the dungeon, you'll find monsters (simply \"bump\" into them to at-\r\n");
    cputs("tack or shoot your bow with the f key), weapons and armor (equip them with the\r\n");
    cputs("w key), scrolls, wands, and potions (use them with the r, z, and q keys), or\r\n");
    cputs("gold and treasures - they'll be useful for trading with the merchant. As you\r\n");
    cputs("fight enemies, you'll gain experience and level up, which will increase your\r\n");
    cputs("health and combat skills. As for the rest, it'll be more fun for you to figure\r\n");
    cputs("it out on your own (press the ?, !, or L keys to learn more). Enjoy!\r\n");
    cputs("\r\n");
    cputs("I'd like to thank Michael Toy, Glenn Wichman, Ken Arnold, and Jon Lane - the\r\n");
    cputs("developers of Rogue. And also John Romero for his many wonderful games and\r\n");
    cputs("inspiring book \"Doom Guy: Life in First Person\".\r\n");
    cputs("\r\n");
    cputs("Rascal is dedicated to my lovely cat Pusya. Rest in peace, my brave darling.\r\n");
    cputs("\r\n");
    cputs(msg_press_continue);
    cgetc();
}