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
; unsigned char weighted_pick(const unsigned char *weights, unsigned char n)
; Weighted random selection of an index from 0 to n-1 - a common rarity point for
; all item categories (weapons, armor, rings, wands, potions,
; scrolls, treasures). Called when generating any item.
;
; IMPORTANT note about rnd(): as in hand_weapon_damage_asm.s/
; monster_init_stats_asm.s, calling rnd() can corrupt ptr1-4/tmp1-4.
; Here, TWO values must survive this call (the weights pointer and n -
; both are needed for the second pass) - we push them onto the HARDWARE stack
; using PHA before the call, and pop them back immediately afterward.
;
; Calling convention: weights (pointer, 2 bytes) is pushed by the caller
; via pushax; n is loaded into A. We free up the program stack via
; incsp2 (exactly what the caller pushed - we didn't perform a decsp ourselves).

.export _weighted_pick
.import _rnd
.import incsp2
.importzp sp, ptr1, tmp1, tmp2, tmp3

.segment "CODE"

.proc _weighted_pick
    sta     tmp1			; tmp1 = n

    ldy     #0
    lda     (sp),y
    sta     ptr1
    ldy     #1
    lda     (sp),y
    sta     ptr1+1
    ; ptr1 = weights, tmp1 = n

    ; total = sum of weights[0..n-1]
    lda     #0
    sta     tmp2			; tmp2 = total
    ldy     #0
@sumLoop:
    cpy     tmp1
    bcs     @sumDone
    lda     (ptr1),y
    clc
    adc     tmp2
    sta     tmp2
    iny
    jmp     @sumLoop
@sumDone:

    lda     tmp2
    bne     @haveTotal
    lda     #0				; if the sum of the weights is 0, return index 0
    ldx     #0
    jmp     incsp2
@haveTotal:
    ; save n and ptr1(weights) to the hardware stack - they'll survive rnd()
    lda     tmp1
    pha
    lda     ptr1
    pha
    lda     ptr1+1
    pha

    lda     tmp2
    jsr     _rnd			; A = rnd(total)
    sta     tmp3			; roll immediately after the return

    ; restore in reverse order (LIFO)
    pla
    sta     ptr1+1
    pla
    sta     ptr1
    pla
    sta     tmp1

    lda     #0
    sta     tmp2			; acc = 0
    ldy     #0
@pickLoop:
    lda     (ptr1),y
    clc
    adc     tmp2
    sta     tmp2			; acc += weights[y]
    cmp     tmp3			; compare with roll
    beq     @next			; acc == roll -> not yet strictly greater; continue
    bcc     @next			; acc < roll -> next
    tya						; acc > roll -> found it, return the current y
    ldx     #0
    jmp     incsp2
@next:
    iny
    cpy     tmp1
    bcc     @pickLoop

    ; this shouldn't happen if the weights are correct - just in case, n-1
    lda     tmp1
    sec
    sbc     #1
    ldx     #0
    jmp     incsp2
.endproc
