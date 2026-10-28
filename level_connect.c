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

/* OVERLAY9. second half of the level generator (connectivity).
 * see level_shapes.c for the first half and a general explanation. */

#include <conio.h>
#include <string.h>
#include "level.h"
#include "rng.h"
#include "game_shared.h"

static unsigned char in_graph[NUM_ZONES];
static unsigned char connected[NUM_ZONES][NUM_ZONES];

static unsigned char has_walls(zone_t *z)
{
    return !(z->flags & RF_GONE) || (z->flags & RF_MAZE);
}

/* where a corridor should touch this zone. zones with no walls (empty
 * 'gone' zones, or maze zones) just get their center, there's no door
 * to place, the corridor runs straight through open space. zones with
 * real walls get a random point on whichever wall faces the
 * destination zone. */
static coord_t zone_edge_point(zone_t *z, signed char toward_dx, signed char toward_dy)
{
    coord_t c;
    if (!has_walls(z)) {
        c.x = z->pos.x + z->size.x / 2;
        c.y = z->pos.y + z->size.y / 2;
        return c;
    }
    if (toward_dx > 0) { c.x = z->pos.x + z->size.x - 1; c.y = z->pos.y + 1 + rnd(z->size.y - 2); }
    else if (toward_dx < 0) { c.x = z->pos.x; c.y = z->pos.y + 1 + rnd(z->size.y - 2); }
    else if (toward_dy > 0) { c.y = z->pos.y + z->size.y - 1; c.x = z->pos.x + 1 + rnd(z->size.x - 2); }
    else { c.y = z->pos.y; c.x = z->pos.x + 1 + rnd(z->size.x - 2); }
    return c;
}

#define MAZE_CONN_MAXCELLS 144 /* exactly zone_w-2=24 x zone_h-1=6. the size of the maze zone is fixed */
#define MAZE_MAX_W 32
#define MAZE_MAX_H 6 /* actual height of the maze area */
static unsigned char conn_dist[MAZE_MAX_H][MAZE_MAX_W];
static unsigned char conn_qy[MAZE_CONN_MAXCELLS], conn_qx[MAZE_CONN_MAXCELLS];

/* same reason as dy4_val/dx4_val in level_shapes.c: a switch compiles
 * to clean code, a static const lookup table placed inside an overlay
 * function does not. */
static signed char dy4c_val(unsigned char i)
{
    switch (i) { case 0: return -1; case 1: return 1; default: return 0; }
}
static signed char dx4c_val(unsigned char i)
{
    switch (i) { case 2: return -1; case 3: return 1; default: return 0; }
}

/* in a maze zone, a door on the boundary appears only after the maze itself
 * has been randomly dug out - its position in no way guarantees that there is
 * an actual internal passage nearby (let alone in a straight line). search
 * for the nearest already-dug maze cell using BFS within the zone's boundaries
 * and dig the shortest path to it. otherwise, part of the maze will remain
 * physically unreachable. */
static void connect_into_maze(zone_t *z, coord_t door)
{
    unsigned char w = z->size.x, h = z->size.y;
    unsigned char qh = 0, qt = 0;
    unsigned char x, y, guard;
    unsigned int ncells = (unsigned int)w * h;
    signed int lx, ly;

    if (ncells == 0 || w > MAZE_MAX_W || h > MAZE_MAX_H)
        return; /* protection against static buffer overflows */

    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            conn_dist[y][x] = 255;

    /* multi-source BFS: start immediately from all cells in the zone that have already been explored */
    for (y = 0; y < h; y++)
        for (x = 0; x < w; x++)
            if (get((unsigned char)(z->pos.y + y), (unsigned char)(z->pos.x + x)) == T_PASSAGE) {
                conn_dist[y][x] = 0;
                conn_qy[qt] = y;
                conn_qx[qt] = x;
                qt++;
            }

    while (qh != qt) {
        unsigned char cy = conn_qy[qh], cx = conn_qx[qh], k, d;
        d = conn_dist[cy][cx];
        qh++;
        for (k = 0; k < 4; k++) {
            signed int ny = (signed int)cy + dy4c_val(k), nx = (signed int)cx + dx4c_val(k);
            if (ny >= 0 && ny < h && nx >= 0 && nx < w) {
                if (conn_dist[(unsigned char)ny][(unsigned char)nx] == 255 && qt < MAZE_CONN_MAXCELLS) {
                    conn_dist[(unsigned char)ny][(unsigned char)nx] = d + 1;
                    conn_qy[qt] = (unsigned char)ny;
                    conn_qx[qt] = (unsigned char)nx;
                    qt++;
                }
            }
        }
    }

    /* move down from the door along the descending path dist to the nearest passage, clearing a path */
    lx = (signed int)door.x - (signed int)z->pos.x;
    ly = (signed int)door.y - (signed int)z->pos.y;
    if (lx < 0 || lx >= w || ly < 0 || ly >= h)
        return;

    {
        unsigned char cx = (unsigned char)lx, cy = (unsigned char)ly;
        for (guard = 0; guard < MAZE_CONN_MAXCELLS; guard++) {
            unsigned char d = conn_dist[cy][cx];
            unsigned char k, bestk = 255, bestd = 255;

            put((unsigned char)(z->pos.y + cy), (unsigned char)(z->pos.x + cx), T_PASSAGE);
            if (d == 0)
                return;

            for (k = 0; k < 4; k++) {
                signed int ny = (signed int)cy + dy4c_val(k), nx = (signed int)cx + dx4c_val(k);
                if (ny >= 0 && ny < h && nx >= 0 && nx < w) {
                    unsigned char nd = conn_dist[(unsigned char)ny][(unsigned char)nx];
                    if (nd < bestd) { bestd = nd; bestk = k; }
                }
            }
            if (bestk == 255)
                return;
            cy = (unsigned char)(cy + dy4c_val(bestk));
            cx = (unsigned char)(cx + dx4c_val(bestk));
        }
    }
}

/* dig a corridor from the door of Room A to the door of Room B.
 * move along the main axis that divides the zones, strictly within the space
 * between Walls A and B, and at exactly one random point along this
 * gap, we turn onto a perpendicular axis to reach the desired coordinate,
 * the entire maneuver takes place in the 'neutral' zone between the rooms
 * and never runs along another room's wall. */
static void dig_corridor(unsigned char a, unsigned char b, unsigned char depth)
{
    zone_t *za = &g_zones[a], *zb = &g_zones[b];
    signed char ax = a % ZONE_COLS, ay = a / ZONE_COLS;
    signed char bx = b % ZONE_COLS, by = b / ZONE_COLS;
    signed char ddx = bx - ax, ddy = by - ay;
    coord_t p1, p2, cur;
    signed char pd_main, pd_cross;
    int distance, turn_spot, turn_distance, i;

    p1 = zone_edge_point(za, ddx, ddy);
    p2 = zone_edge_point(zb, -ddx, -ddy);

    /* doors are only in actual rooms; in 'skipped' areas and mazes simply a passageway */
    if (!(za->flags & RF_GONE))
        put(p1.y, p1.x, (rnd(10) < depth && rnd(5) == 0) /* rnd(10) < depth: secret-door chance grows with dungeon depth */
            ? (ddx != 0 ? T_DOOR_SECRET_V : T_DOOR_SECRET_H) : T_DOOR);
    else
        put(p1.y, p1.x, T_PASSAGE);
    if (za->flags & RF_MAZE)
        connect_into_maze(za, p1);

    if (!(zb->flags & RF_GONE))
        put(p2.y, p2.x, (rnd(10) < depth && rnd(5) == 0) /* rnd(10) < depth: secret-door chance grows with dungeon depth */
            ? (ddx != 0 ? T_DOOR_SECRET_V : T_DOOR_SECRET_H) : T_DOOR);
    else
        put(p2.y, p2.x, T_PASSAGE);
    if (zb->flags & RF_MAZE)
        connect_into_maze(zb, p2);

    cur = p1;

    if (ddx != 0) {
        /* axis X (rooms arranged horizontally) */
        pd_main = (p2.x > p1.x) ? 1 : -1;
        distance = (((int)p2.x - (int)p1.x) * pd_main) - 1;
        if (distance < 1)
            return;

        turn_distance = (int)p2.y - (int)p1.y;
        if (turn_distance < 0) turn_distance = -turn_distance;
        pd_cross = (p2.y > p1.y) ? 1 : -1;
        turn_spot = 1 + rnd((unsigned char)(distance > 1 ? distance - 1 : 1));

        for (i = 1; i <= distance; i++) {
            cur.x += pd_main;
            put(cur.y, cur.x, T_PASSAGE);
            if (i == turn_spot)
                while (turn_distance--) {
                    cur.y += pd_cross;
                    put(cur.y, cur.x, T_PASSAGE);
                }
        }
    } else {
        /* axis Y (rooms arranged vertically) */
        pd_main = (p2.y > p1.y) ? 1 : -1;
        distance = (((int)p2.y - (int)p1.y) * pd_main) - 1;
        if (distance < 1)
            return;

        turn_distance = (int)p2.x - (int)p1.x;
        if (turn_distance < 0) turn_distance = -turn_distance;
        pd_cross = (p2.x > p1.x) ? 1 : -1;
        turn_spot = 1 + rnd((unsigned char)(distance > 1 ? distance - 1 : 1));

        for (i = 1; i <= distance; i++) {
            cur.y += pd_main;
            put(cur.y, cur.x, T_PASSAGE);
            if (i == turn_spot)
                while (turn_distance--) {
                    cur.x += pd_cross;
                    put(cur.y, cur.x, T_PASSAGE);
                }
        }
    }
}

/* builds a random spanning tree connecting all NUM_ZONES zones (this
 * alone guarantees every zone is reachable), then digs a handful
 * of extra corridors between already-adjacent zones so the level
 * has loops and alternate routes instead of being one single path. */
void build_connectivity(unsigned char depth)
{
    unsigned char i, j, roomcount, start;
    unsigned char cand[NUM_ZONES], ncand;

    memset(in_graph, 0, sizeof(in_graph));
    memset(connected, 0, sizeof(connected));

    start = rnd(NUM_ZONES);
    in_graph[start] = 1;
    roomcount = 1;

    while (roomcount < NUM_ZONES) {
        unsigned char from = 0xFF, to = 0xFF;
        /* search for any pair of neighbors (included -> not included) */
        for (i = 0; i < NUM_ZONES && from == 0xFF; i++) {
            if (!in_graph[i]) continue;
            ncand = 0;
            for (j = 0; j < NUM_ZONES; j++)
                if (!in_graph[j] && zone_adjacent(i, j))
                    cand[ncand++] = j;
            if (ncand > 0) {
                from = i;
                to = cand[rnd(ncand)];
            }
        }
        if (from == 0xFF) break; /* should not happen with a 3x3 connected grid */
        dig_corridor(from, to, depth);
        connected[from][to] = connected[to][from] = 1;
        in_graph[to] = 1;
        roomcount++;
    }

    /* few extra edges for loops/branches */
    {
        unsigned char extra = rnd(5);
        while (extra--) {
            i = rnd(NUM_ZONES);
            ncand = 0;
            for (j = 0; j < NUM_ZONES; j++)
                if (zone_adjacent(i, j) && !connected[i][j])
                    cand[ncand++] = j;
            if (ncand == 0) continue;
            j = cand[rnd(ncand)];
            dig_corridor(i, j, depth);
            connected[i][j] = connected[j][i] = 1;
        }
    }
}
