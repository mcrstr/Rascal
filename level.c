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
 
/* Resident. level generation + map/zone accessors (put/get) + trap placement.
 * generation sets up the zones, then loads the two overlay halves that
 * do the actual drawing one at a time (level_shapes.c (OVERLAY8) for room
 * and maze shapes, level_connect.c (OVERLAY9) for corridors). */

#include <string.h>
#include "level.h"
#include "rng.h"
#include "game_shared.h"

static unsigned char zone_w, zone_h;

/* zone indices 0-8 encode a position in a 3x3 grid: index / ZONE_COLS
 * is the row, index % ZONE_COLS is the column. two zones are neighbors
 * if their Manhattan distance is exactly 1 (share an edge, not just
 * a corner). */
unsigned char zone_adjacent(unsigned char a, unsigned char b)
{
    signed char ax = a % ZONE_COLS, ay = a / ZONE_COLS;
    signed char bx = b % ZONE_COLS, by = b / ZONE_COLS;
    signed char dx = ax - bx, dy = ay - by;
    if (dx < 0) dx = -dx;
    if (dy < 0) dy = -dy;
    return (dx + dy) == 1;
}

void put(unsigned char y, unsigned char x, tile_t t)
{
    if (y < MAP_H && x < MAP_W)
        g_map[y][x] = (unsigned char)t;
}

tile_t get(unsigned char y, unsigned char x)
{
    if (y >= MAP_H || x >= MAP_W) return T_EMPTY;
    return (tile_t)g_map[y][x];
}

static unsigned char is_walkable(unsigned char t)
{
    return t == T_FLOOR || t == T_PASSAGE || t == T_DOOR ||
           t == T_DOOR_SECRET_H || t == T_DOOR_SECRET_V || t == T_STAIRS_DOWN;
}

/* occasionally, two independently dug passages happen to intersect only
 * diagonally. this is only possible near the boundary of the maze zone.
 * so instead of checking the entire map, we check only its vicinity
 * by directly accessing the array without calling any functions. */
static void fix_diagonal_touches_region(unsigned char y0, unsigned char x0,
                                          unsigned char y1, unsigned char x1)
{
    unsigned char x, y;

    if (y0 > 0) y0--;
    if (x0 > 0) x0--;
    if (y1 < MAP_H - 1) y1++;
    if (x1 < MAP_W - 1) x1++;

    for (y = y0; y < y1; y++) {
        for (x = x0; x < x1; x++) {
            unsigned char a = g_map[y][x],     d = g_map[y + 1][x + 1];
            unsigned char b = g_map[y][x + 1], c = g_map[y + 1][x];

            if (is_walkable(a) && is_walkable(d) && !is_walkable(b) && !is_walkable(c)) {
                if (g_map[y][x + 1] == T_EMPTY) g_map[y][x + 1] = T_PASSAGE;
                else if (g_map[y + 1][x] == T_EMPTY) g_map[y + 1][x] = T_PASSAGE;
            }
            if (is_walkable(b) && is_walkable(c) && !is_walkable(a) && !is_walkable(d)) {
                if (g_map[y][x] == T_EMPTY) g_map[y][x] = T_PASSAGE;
                else if (g_map[y + 1][x + 1] == T_EMPTY) g_map[y + 1][x + 1] = T_PASSAGE;
            }
        }
    }
}

unsigned char load_overlay_retry(unsigned char (*loader)(void))
{
    unsigned char loaded = 0, t;
    for (t = 0; t < 3 && !loaded; t++) loaded = loader();
    return loaded;
}

/* picks a random rectangle for a room inside zone i's grid cell, leaving
 * at least a 1-cell margin on every side so rooms never touch the edge
 * of their cell (and therefore never touch a neighboring zone's room
 * directly. corridors always dig the connection between them). */
static void randomize_zone_rect(zone_t *z, unsigned char i, unsigned char zone_w, unsigned char zone_h)
{
    unsigned char maxw = (unsigned char)(zone_w - 3);
    z->size.x = (unsigned char)(4 + rnd(maxw > 4 ? (unsigned char)(maxw - 4) : 1));
    z->size.y = (unsigned char)(4 + rnd(3));
    z->pos.x = (unsigned char)((i % ZONE_COLS) * zone_w + 1 + rnd((unsigned char)(zone_w - z->size.x)));
    z->pos.y = (unsigned char)((i / ZONE_COLS) * zone_h + rnd((unsigned char)(zone_h - z->size.y)));
}

void level_generate(unsigned char depth)
{
    unsigned char i, skip_count, tries;

    memset(g_map, T_EMPTY, sizeof(g_map));
    memset(g_zones, 0, sizeof(g_zones));
    
    zone_w = MAP_W / ZONE_COLS;
    zone_h = MAP_H / ZONE_ROWS;

    for (i = 0; i < NUM_ZONES; i++) {
        g_zones[i].pos.x = (i % ZONE_COLS) * zone_w + 1;
        g_zones[i].pos.y = (i / ZONE_COLS) * zone_h;
    }

    skip_count = rnd(4);
    {
        /* mazes */
        unsigned char maze_target = 0, maze_chosen = 0, guard;
        static const unsigned char MAZE_CAP = 3; /* no more than 3 mazes per level */

        if (depth > 10) {
            unsigned int t = (unsigned int)(depth - 10) / 6; /* reach upper limit at level 30 */
            if (t > MAZE_CAP) t = MAZE_CAP;
            maze_target = (unsigned char)t;
        }

        for (guard = 0; guard < 40 && maze_chosen < maze_target; guard++) {
            unsigned char idx = rnd(NUM_ZONES);
            if (g_zones[idx].flags & RF_GONE) continue;
            g_zones[idx].flags |= (RF_GONE | RF_MAZE);
            maze_chosen++;
        }
		
        /* isolation only matters for plain 'gone' zones (empty voids with no
         * walls at all). if two of them end up adjacent, the corridor digger
         * would carve straight through both, producing one giant meaningless
         * corridor across empty space. maze zones don't have this problem:
         * they have no walls either, but they're filled with actual maze
         * passages, so two adjacent mazes just merge into one bigger maze,
         * which is fine. */
        for (i = 0; i < skip_count; i++) {
            unsigned char idx = 0, tries2, ok = 0, j;
            for (tries2 = 0; tries2 < 20; tries2++) {
                idx = rnd(NUM_ZONES);
                if (g_zones[idx].flags & RF_GONE)
                    continue;
                ok = 1;
                for (j = 0; j < NUM_ZONES; j++)
                    if ((g_zones[j].flags & RF_GONE) && !(g_zones[j].flags & RF_MAZE) &&
                        zone_adjacent(idx, j)) {
                        ok = 0;
                        break;
                    }
                if (ok) break;
            }
            if (!ok) continue; /* if no isolated location is found, skip this attempt */
            g_zones[idx].flags |= RF_GONE;
        }
    }

    if (!load_overlay_retry(load_shapes_overlay)) return;
    /* if it didn't work at all (there's nothing left to draw with) stop and don't execute junk */
    for (i = 0; i < NUM_ZONES; i++) {
        zone_t *z = &g_zones[i];

        if (z->flags & RF_GONE) {
            if (z->flags & RF_MAZE) {
                
                z->size.x = zone_w - 2;
                z->size.y = zone_h - 1;
                draw_maze(z);
                
            } else {
                /* connection point */
                randomize_zone_rect(z, i, zone_w, zone_h);
            }
            continue;
        }

        if (rnd(25) < depth)
            z->flags |= RF_DARK;

        for (tries = 0; tries < 20; tries++) {
            randomize_zone_rect(z, i, zone_w, zone_h);
            if (z->pos.y > 0) break;
        }
        draw_room(z);
    }

    if (!load_overlay_retry(load_connect_overlay)) return; /* if it didn't work at all - rooms were left without corridors */
    build_connectivity(depth);

    /* generate stairs down. this loop always terminates: skip_count
     * is capped well below NUM_ZONES, so at least one 'non-gone' zone always exists. */
    for (;;) {
        unsigned char idx = rnd(NUM_ZONES);
        zone_t *z = &g_zones[idx];
        if (z->flags & RF_GONE) continue;
        put(z->pos.y + 1 + rnd(z->size.y - 2), z->pos.x + 1 + rnd(z->size.x - 2), T_STAIRS_DOWN);
        break;
    }

    /* diagonal joints are only possible at the boundaries of maze zones; check only their surroundings */
    for (i = 0; i < NUM_ZONES; i++) {
        zone_t *z = &g_zones[i];
        if (z->flags & RF_MAZE)
            fix_diagonal_touches_region(z->pos.y, z->pos.x,
                (unsigned char)(z->pos.y + z->size.y - 1),
                (unsigned char)(z->pos.x + z->size.x - 1));
    }
}

/* one hidden trap on a random room floor cell (T_FLOOR only - never a door,
 * staircase or corridor). A hidden trap looks like a plain floor dot, but
 * corridors are drawn with a distinct MouseText glyph, so a dot in the
 * middle of one would give the trap away. */
static unsigned char place_one_trap(void)
{
    unsigned char tries;
    for (tries = 0; tries < 100; tries++) {
        unsigned char x = rnd(MAP_W);
        unsigned char y = rnd(MAP_H);
        if ((tile_t)g_map[y][x] == T_FLOOR) {
            g_map[y][x] = T_TRAP_HIDDEN;
            return 1;
        }
    }
    return 0;
}

/* 25% one trap, 25% two, 50% none. Called once per level,
 * from generate_and_populate() immediately after level_generate() */
void place_traps(void)
{
    unsigned char roll = rnd(100);
    unsigned char count = (roll < 25) ? 1 : (roll < 50) ? 2 : 0;
    unsigned char i;
    for (i = 0; i < count; i++) place_one_trap();
}
