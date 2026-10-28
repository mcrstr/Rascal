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

/* level data structure, 80x24, cc65 (Apple2Enh target). */

#ifndef LEVEL_H
#define LEVEL_H

#define MAP_W       80
#define MAP_H       21	/* line 0 - messages, 22 - effects, 23 - status bar */

#define ZONE_COLS   3
#define ZONE_ROWS   3
#define NUM_ZONES   (ZONE_COLS * ZONE_ROWS)

/* map tiles - one byte per cell */
typedef enum {
    T_EMPTY = 0,
    T_FLOOR,
    T_WALL_H,
    T_WALL_V,
    T_WALL_UL,
    T_WALL_UR,
    T_WALL_LL,
    T_WALL_LR,
    T_DOOR,
    T_DOOR_SECRET_H,	/* secret door on the horizontal wall, disguised as a '-' */
    T_DOOR_SECRET_V,	/* secret door on the vertical wall, disguised as a '|' */
    T_PASSAGE,
    T_STAIRS_DOWN,
    T_FRONTIER_TMP,		/* 'already in the maze boundary queue' note, never included in the final map */
    T_TRAP_HIDDEN,		/* trap on the floor of a room; if not found, it is disguised as '.'.
						   Only in rooms (T_FLOOR), never in hallways. */
    T_TRAP_REVEALED		/* trap found - MouseText code 91 */
} tile_t;

typedef struct {
    unsigned char x, y;
} coord_t;

/* zone flags */
#define RF_GONE   0x01	/* empty area - only the corridor junction point */
#define RF_MAZE   0x02	/* maze, not a rectangular room */
#define RF_DARK   0x04	/* dark room - it's only visible when the player is close to it */

typedef struct {
    coord_t pos;	/* top-left corner */
    coord_t size;	/* width, height */
    unsigned char flags;
} zone_t;

extern unsigned char g_map[MAP_H][MAP_W];
extern zone_t g_zones[NUM_ZONES];

void level_generate(unsigned char depth);
void place_traps(void); /* resident (level.c) - places 0/1/2 traps on T_FLOOR (rooms only) */
unsigned char zone_adjacent(unsigned char a, unsigned char b);

#endif
