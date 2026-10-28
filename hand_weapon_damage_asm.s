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
; signed int hand_weapon_damage(signed char eq_slot, unsigned char *had_flag)
; Calculates one-handed melee damage (weapon dice + bonus); called
; twice per attack (left and right hand).
;
; IMPORTANT: Inside the loop, rnd() is called - a function compiled
; directly from C whose internal implementation (modulo division, tosumoda0)
; may well use ptr1/tmp1-4 for its own purposes - so must not store anything
; in these registers that needs to 'survive' the rnd() call. Therefore, reserve
; 2 bytes on the cc65 program stack (decsp2) and access them via (sp),y.
; The program stack is separate and in no way connected to the inner workings
; of rnd(), so it is guaranteed to survive the nested call.
;
; Layout of the two registers after decsp2:
;   (sp)+0 = dice_s (cache: doesn't change during the loop, but it's safer to store
;            it right here than to read it again via ptr1 after calling rnd())
;   (sp)+1 = damage accumulator d, INITIALIZED as mod2 (not 0!): so after
;            the loop, there's no need to access ptr1 again for the weapon bonus.
; eq_slot (must be read BEFORE decsp2) is now at
; offset +2 (shifted due to the two new locals).
;
; The call convention was confirmed based on the disassembled call code in
; main.c: eq_slot is pushed, the had_flag pointer is loaded into A/X (the last
; parameter), followed by a jsr. Two local variables need to be freed
; + 1 (the caller's eq_slot) = 3 bytes - jmp incsp3 on both exits.

.export _hand_weapon_damage
.import _rnd
.import calc_inv_addr
.import decsp2, incsp3
.importzp sp, ptr1, ptr2, tmp1

ITEM_WEAPON_MELEE = 1
OFF_TYPE    = 0
OFF_MOD2    = 4
OFF_DICE_N  = 7
OFF_DICE_S  = 8

.segment "CODE"

.proc _hand_weapon_damage
    sta     ptr2		; ptr2 = had_flag (least significant byte)
    stx     ptr2+1		; ptr2 = had_flag (most significant byte)

    jsr     decsp2		; reserve 2 locals

    ldy     #2
    lda     (sp),y		; A = eq_slot (shifted by +2 after decsp2)
    bmi     @notMelee

    jsr     calc_inv_addr	; ptr1 = &g_inventory[eq_slot] (from armor_pv_asm.s)
    ldy     #OFF_TYPE
    lda     (ptr1),y
    cmp     #ITEM_WEAPON_MELEE
    bne     @notMelee

    ldy     #OFF_DICE_N
    lda     (ptr1),y
    sta     tmp1 		; tmp1 = number of dice - used until the first call to rnd()
    ldy     #OFF_DICE_S
    lda     (ptr1),y
    ldy     #0
    sta     (sp),y		; local 0 = dice_s
    ldy     #OFF_MOD2
    lda     (ptr1),y
    ldy     #1
    sta     (sp),y		; local 1 = accumulator d, immediately = mod2

    lda     tmp1
    beq     @doneDice	; dice_n == 0 -> do not enter the loop; d is already correct

@diceLoop:
    ldy     #0
    lda     (sp),y		; A = dice_s (read from our local variable)
    jsr     _rnd		; A = rnd(dice_s)
    clc
    adc     #1
    ldy     #1
    clc
    adc     (sp),y		; + accumulated d
    sta     (sp),y
    dec     tmp1
    bne     @diceLoop

@doneDice:
    ldy     #0
    lda     #1
    sta     (ptr2),y	; *had_flag = 1

    ldy     #1
    lda     (sp),y		; final d
    ldx     #0
    jmp     incsp3

@notMelee:
    lda     #0
    ldx     #0
    jmp     incsp3
.endproc
