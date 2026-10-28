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
; zone_t *room_at(unsigned char x, unsigned char y). Linear search for the zone
; to which the cell (x, y) belongs, based on the bounding rectangle.
;
; zone_t (level.h): { pos.x, pos.y, size.x, size.y, flags } - 5 bytes,
; offsets 0, 1, 2, 3, 4, respectively. NUM_ZONES = 9 (ZONE_COLS * ZONE_ROWS).
;
; Calling convention - same as for is_seen/set_seen (see seen_asm.s):
; the caller pushes x onto the stack using 'pusha', places y in register A, then executes 'jsr'.
; The result is a 'zone_t*' (or NULL) in A (lowest byte)/X (highest byte), as a standard
; cc65 near pointer.

.export _room_at
.import _g_zones
.import incsp1
.importzp sp, ptr1, tmp1, tmp2, tmp3

RF_GONE = $01
ZONE_SIZE = 5
NUM_ZONES = 9

.segment "CODE"

.proc _room_at
    sta     tmp2			; tmp2 = y
    ldy     #0
    lda     (sp),y
    sta     tmp1			; tmp1 = x

    lda     #<_g_zones
    sta     ptr1
    lda     #>_g_zones
    sta     ptr1+1

    ldx     #NUM_ZONES
@loop:
    ldy     #4				; flags
    lda     (ptr1),y
    and     #RF_GONE
    bne     @next			; 'skipped' zone (only the junction point)

    ; x >= pos.x ?
    ldy     #0
    lda     tmp1
    cmp     (ptr1),y
    bcc     @next

    ; x < pos.x + size.x ?
    lda     (ptr1),y		; pos.x
    clc
    ldy     #2
    adc     (ptr1),y		; + size.x (the coordinates are small; byte overflow does not occur)
    sta     tmp3
    lda     tmp1
    cmp     tmp3
    bcs     @next

    ; y >= pos.y ?
    ldy     #1
    lda     tmp2
    cmp     (ptr1),y
    bcc     @next

    ; y < pos.y + size.y ?
    lda     (ptr1),y		; pos.y
    clc
    ldy     #3
    adc     (ptr1),y		; + size.y
    sta     tmp3
    lda     tmp2
    cmp     tmp3
    bcs     @next

    ; if found, return a pointer to the current zone
    lda     ptr1
    ldx     ptr1+1
    jmp     incsp1

@next:
    lda     ptr1
    clc
    adc     #ZONE_SIZE
    sta     ptr1
    bcc     @noCarry
    inc     ptr1+1
@noCarry:
    dex
    bne     @loop

    ; if not found, return NULL
    lda     #0
    tax
    jmp     incsp1
.endproc
