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
; is_seen(x,y) / set_seen(x,y) - bitwise per map cell in g_seen[MAP_H][MAP_W/8].
; Called VERY frequently - for every visible cell during every screen redraw
; (render_full, draw_cell) and when reading a cartography scroll (immediately
; MAP_H*MAP_W = 1,680 cells at a time).
;
; The cc65 calling convention for (unsigned char, unsigned char) is confirmed by
; the compiler's disassembled code: the caller pushes x onto the program
; stack via 'pusha', then places 'y' into the accumulator, and executes 'jsr'. Thus, at the start:
;   A = y (the second argument; the 'last' one is in the register)
;   (sp) = x (the first argument, saved by a single 'incsp1' before the return)

.export _is_seen
.export _set_seen
.import _g_seen
.import incsp1
.importzp sp, ptr1, tmp1, tmp2

.segment "CODE"

; General part: for each (x, y) in tmp1(x)/tmp2(y), compute ptr1 = &g_seen[y][x>>3]
; and leave x in A for further computation of (x&7).
.proc calc_seen_addr
    ; row = y*10 = (y<<3) + (y<<1)
    lda     tmp2
    asl     a
    asl     a
    asl     a			; A = y*8
    sta     ptr1		; we'll temporarily use ptr1 as a byte storage location
    lda     tmp2
    asl     a			; A = y*2
    clc
    adc     ptr1		; A = y*8 + y*2 = y*10 (no more than 200 - fits into a byte)

    ; + byte_index = x>>3
    pha
    lda     tmp1
    lsr     a
    lsr     a
    lsr     a
    sta     tmp2		; tmp2 is no longer needed as y - will reuse it for byte_index
    pla
    clc
    adc     tmp2		; A = row + byte_index, maximum 200+9=209 - without a byte overflow

    ; address = &_g_seen + A (16-bit addition of a single-byte offset)
    clc
    adc     #<_g_seen
    sta     ptr1
    lda     #0
    adc     #>_g_seen
    sta     ptr1+1

    lda     tmp1		; return x to A to calculate the bit for the caller
    rts
.endproc

bit_table:
    .byte   $01, $02, $04, $08, $10, $20, $40, $80

.proc _is_seen
    sta     tmp2		; tmp2 = y
    ldy     #0
    lda     (sp),y
    sta     tmp1		; tmp1 = x

    jsr     calc_seen_addr	; ptr1 = byte address; A = x

    and     #7
    tax
    lda     bit_table,x
    ldy     #0
    and     (ptr1),y
    beq     @zero
    lda     #1
    jmp     @done
@zero:
    lda     #0
@done:
    ldx     #0			; cc65 always returns an unsigned char with X cleared
    jmp     incsp1		; decrement x, as set by the caller (1 byte), and return
.endproc

.proc _set_seen
    sta     tmp2		; tmp2 = y
    ldy     #0
    lda     (sp),y
    sta     tmp1		; tmp1 = x

    jsr     calc_seen_addr	; ptr1 = byte address; A = x

    and     #7
    tax
    lda     bit_table,x
    ldy     #0
    ora     (ptr1),y
    sta     (ptr1),y

    jmp     incsp1		; decrement x and return (void - no return register needed)
.endproc
