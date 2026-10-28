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
 
/* Resident. A 16-bit LFSR-based random number generator (the actual shift
 * happens in rng_asm.s, see lfsr_next) - used everywhere the game needs
 * a random number: level generation, combat, item drops, and so on. */

#include "rng.h"

/* not static: rng_asm.s (lfsr_next) reads and writes this variable directly
 * by address, it isn't visible to it otherwise */
unsigned int state = 0xACE1u;

/* called once, when a new character is created. The Apple IIe has
 * no hardware clock to seed from, so the caller (menus_start.c) uses
 * the number of idle loop iterations until the player pressed a key
 * at the 'Press any key' prompt instead */
void rng_seed(unsigned int seed)
{
    if (seed == 0) seed = 1; /* LFSR cannot start from zero */
    state = seed;
}

/* saved and restored on save/load (see menus_start.c), so a loaded game
 * continues the exact same sequence of random numbers */
unsigned int rng_get_state(void)
{
    return state;
}

/* same LFSR constraint as rng_seed, this check keeps a corrupted save
   value from slipping through. */
void rng_set_state(unsigned int s)
{
    if (s == 0) s = 1;
    state = s;
}

extern unsigned int lfsr_next(void);

/* not perfectly uniform for n that isn't a power of two (the classic
 * modulo-bias issue) - a deliberate tradeoff for speed and simplicity
 * on 6502 hardware. */
unsigned char rnd(unsigned char n)
{
    if (n == 0) return 0;
    return (unsigned char)(lfsr_next() % n);
}