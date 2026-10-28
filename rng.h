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

/* various uses of a random number generator */

#ifndef RNG_H
#define RNG_H

void rng_seed(unsigned int seed);
unsigned char rnd(unsigned char n);	/* 0..n-1 */
unsigned int rng_get_state(void);	/* to save the game - so you can continue with the same sequence of numbers */
void rng_set_state(unsigned int s);

#endif
