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
; ground_item_t *item_at(unsigned char x, unsigned char y). Linear search
; for an item on the ground in cell (x, y): called for every cell being
; drawn (draw_cell) and when an item is picked up or discarded.
;
; ground_item_t (game_shared.h): x, y, active, type, amount, mod1, mod2,
; def1, def2, dice_n, dice_s - 11 bytes; offsets 0, 1, 2 are needed here
; (x, y, active).
;
; Calling convention - same as for monster_at/is_seen/room_at: the caller
; pushes x onto the stack via pusha, places y in A, then executes jsr.
; Result - ground_item_t* (or NULL) in A (low byte)/X (high byte).

.export _item_at
.import _g_ground_items, _g_ground_count
.import incsp1
.importzp sp, ptr1, tmp1, tmp2, tmp3

ITEM_SIZE  = 11
OFF_X      = 0
OFF_Y      = 1
OFF_ACTIVE = 2

.segment "CODE"

.proc _item_at
    sta     tmp2              ; tmp2 = y
    ldy     #0
    lda     (sp),y
    sta     tmp1              ; tmp1 = x

    lda     #<_g_ground_items
    sta     ptr1
    lda     #>_g_ground_items
    sta     ptr1+1

    lda     _g_ground_count
    sta     tmp3              ; tmp3 = number of iterations remaining

@loop:
    lda     tmp3
    beq     @notFound

    ldy     #OFF_ACTIVE
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

    ; if found, return a pointer to the current item
    lda     ptr1
    ldx     ptr1+1
    jmp     incsp1

@next:
    lda     ptr1
    clc
    adc     #ITEM_SIZE
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
