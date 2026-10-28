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
; signed char armor_pv(void). Total PV from armor and shield. Called
; for every hit in combat - both when the player attacks and when
; the player is attacked. No parameters (void), just jsr/rts.

.export _armor_pv
.export calc_inv_addr
.import _g_eq_armor, _g_eq_hand_l, _g_eq_hand_r
.import _g_inventory
.importzp ptr1, tmp1, tmp4

ITEM_SHIELD       = 12 ; item_type_t in game_shared.h - check this whenever a new item type is added!
INV_ITEM_SIZE     = 9
OFF_TYPE = 0
OFF_MOD2 = 4

.segment "CODE"

; input: A = slot index (0..MAX_INVENTORY-1, already verified to be >= 0)
; output: ptr1 = &g_inventory[A]
.proc calc_inv_addr
    sta     tmp1
    asl     a
    asl     a
    asl     a		; A = slot*8
    clc
    adc     tmp1	; A = slot*8+slot = slot*9 (17*9=153 max)
    clc
    adc     #<_g_inventory
    sta     ptr1
    lda     #0
    adc     #>_g_inventory
    sta     ptr1+1
    rts
.endproc

.proc _armor_pv
    lda     #0
    sta     tmp4	; pv = 0

    ; armor: g_eq_armor, mod2 field, type is always ITEM_ARMOR
    lda     _g_eq_armor
    bmi     @doneArmor
    jsr     calc_inv_addr
    ldy     #OFF_MOD2
    lda     (ptr1),y
    clc
    adc     tmp4
    sta     tmp4
@doneArmor:

    ; left hand: SHIELD only -> mod2
    lda     _g_eq_hand_l
    bmi     @doneHandL
    jsr     calc_inv_addr
    ldy     #OFF_TYPE
    lda     (ptr1),y
    cmp     #ITEM_SHIELD
    bne     @doneHandL
    ldy     #OFF_MOD2
    lda     (ptr1),y
    clc
    adc     tmp4
    sta     tmp4
@doneHandL:

    ; right hand: SHIELD only -> mod2
    lda     _g_eq_hand_r
    bmi     @doneHandR
    jsr     calc_inv_addr
    ldy     #OFF_TYPE
    lda     (ptr1),y
    cmp     #ITEM_SHIELD
    bne     @doneHandR
    ldy     #OFF_MOD2
    lda     (ptr1),y
    clc
    adc     tmp4
    sta     tmp4
@doneHandR:

    lda     tmp4
    bpl     @positive
    ldx     #$FF	; a negative result indicates signed extension (X=$FF, not 0!)
    rts
@positive:
    ldx     #$00
    rts
.endproc
