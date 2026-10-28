; Copyright (C) 2026 Fedor Melnikov
; Rascal v1.0
;
; This program is free software; you can redistribute it and/or modify
; it under the terms of the GNU General Public License as published by
; the Free Software Foundation; either version 2 of the License, or
; (at your option) any later version.
; 
; This program is distributed in the hope that it will be useful,
; but WITHOUT ANY WARRANTY; without even the implied warranty of
; MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
; GNU General Public License for more details.
;
; You should have received a copy of the GNU General Public License
; along with this program; if not, see <https://www.gnu.org/licenses/>.
;
; unsigned char ring_worn_effect(unsigned char effect). How many of the two
; rings being worn have this effect (0/1/2). The most frequently called
; function related to rings: called from at least 13 locations for every
; combat calculation (accuracy/damage), for every turn (regeneration, hunger,
; door search, aggression), and even from two other assembly files
; (effective_defense_asm.s - defense ring, effective_strength_asm.s - strength ring).
;
; A = input effect; the ring slot is compared directly by index in
; ring_effect[12] (the mod1 value for rings is guaranteed to be 0..11, unsigned in
; practice, although the field is formally a signed char).

.export _ring_worn_effect
.import calc_inv_addr
.import _g_eq_ring_l, _g_eq_ring_r, _g_inventory, _ring_effect
.importzp ptr1, tmp2, tmp3

ITEM_RING = 13 ; item_type_t in game_shared.h - CHECK this whenever a new item type is added!
OFF_TYPE = 0
OFF_MOD1 = 3

.segment "CODE"

.proc _ring_worn_effect
    sta     tmp3			; retained the effect (parameter from A)
    lda     #0
    sta     tmp2			; count = 0

    ; left ring
    lda     _g_eq_ring_l
    bmi     @doneL
    jsr     calc_inv_addr
    ldy     #OFF_TYPE
    lda     (ptr1),y
    cmp     #ITEM_RING
    bne     @doneL
    ldy     #OFF_MOD1
    lda     (ptr1),y
    tax
    lda     _ring_effect,x
    cmp     tmp3
    bne     @doneL
    inc     tmp2
@doneL:

    ; right ring
    lda     _g_eq_ring_r
    bmi     @doneR
    jsr     calc_inv_addr
    ldy     #OFF_TYPE
    lda     (ptr1),y
    cmp     #ITEM_RING
    bne     @doneR
    ldy     #OFF_MOD1
    lda     (ptr1),y
    tax
    lda     _ring_effect,x
    cmp     tmp3
    bne     @doneR
    inc     tmp2
@doneR:

    lda     tmp2
    ldx     #0
    rts
.endproc
