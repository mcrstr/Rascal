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
; monster_t *monster_at(unsigned char x, unsigned char y). Linear search
; for a live monster in cell (x, y), called at every step (collision
; check) and in many other places.
;
; monster_t (game_shared.h): x, y, awake, alive, hp, attack, defense, dmg_lo,
; dmg_hi, paralyzed, is_merchant, species - 12 bytes; offsets 0, 1, 3 are needed
; here (x, y, alive).
;
; Calling convention - same as for is_seen/room_at (see seen_asm.s):
; the caller pushes x onto the stack via pusha, places y in A, then executes jsr.
; The result is a monster_t* (or NULL) in A (low byte)/X (high byte), as a standard
; cc65 near pointer.

.export _monster_at
.import _g_monsters, _g_monster_count
.import incsp1
.importzp sp, ptr1, tmp1, tmp2, tmp3

MONSTER_SIZE = 12
OFF_X     = 0
OFF_Y     = 1
OFF_ALIVE = 3

.segment "CODE"

.proc _monster_at
    sta     tmp2              ; tmp2 = y
    ldy     #0
    lda     (sp),y
    sta     tmp1              ; tmp1 = x

    lda     #<_g_monsters
    sta     ptr1
    lda     #>_g_monsters
    sta     ptr1+1

    lda     _g_monster_count
    sta     tmp3              ; tmp3 = number of iterations remaining

@loop:
    lda     tmp3
    beq     @notFound

    ldy     #OFF_ALIVE
    lda     (ptr1),y
    beq     @next

    ldy     #OFF_X
    lda     (ptr1),y
    cmp     tmp1
    bne     @next

    ldy     #OFF_Y
    lda     (ptr1),y
    cmp     tmp2
    bne     @next

    ; if found, return a pointer to the current monster
    lda     ptr1
    ldx     ptr1+1
    jmp     incsp1

@next:
    lda     ptr1
    clc
    adc     #MONSTER_SIZE
    sta     ptr1
    bcc     @noCarry
    inc     ptr1+1
@noCarry:
    dec     tmp3
    jmp     @loop

@notFound:
    lda     #0
    tax
    jmp     incsp1
.endproc
