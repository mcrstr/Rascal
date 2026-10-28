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
; void draw_victory_picture(void). Draws a victory screen without a single
; ROM access. Only direct access to screen memory ($400-$7FF, regular
; RAM; the Language Card does not touch it) and soft switches ($C000-$C057,
; also outside the LC). Not a single JSR or address in the range $D000-$FFFF.
;
; Line address formula (confirmed by two independent sources):
;   addr = $400 + (row%8)*$80 + (row/8)*$28,  row = 0..23 (text line)
; Low-order row (0..47) = text_string*2 + parity:
;   even low-order row -> lower nibble of the byte, odd -> upper nibble.
;
; Use $2E (in the original ROM it was called MASK) as a temporary byte for
; merging nibbles - this cell is now unused, since we no longer
; call the actual ROM PLOT.

.export _draw_victory_picture
.importzp ptr1, ptr2, tmp1, tmp2, tmp3, tmp4

MASKTMP = $2E

.segment "CODE"

; Table of base addresses for text strings 0..23 (for low-resolution mode: string/2)
row_lo:
    .byte <$400,<$480,<$500,<$580,<$600,<$680,<$700,<$780
    .byte <$428,<$4a8,<$528,<$5a8,<$628,<$6a8,<$728,<$7a8
    .byte <$450,<$4d0,<$550,<$5d0,<$650,<$6d0,<$750,<$7d0
row_hi:
    .byte >$400,>$480,>$500,>$580,>$600,>$680,>$700,>$780

; Input: X = text string (0..23). Output: ptr1 = address of the string.
; IMPORTANT: does not modify X (only reads via TXA) and does not modify carry
; (no instruction here affects C) - can be called from anywhere,
; without saving these two registers/flags via JSR/RTS. BUT: Y IS OVERWRITTEN
; (used by 'tay' for indexing 'row_hi') - if the calling code
; needs Y after the call (such as in 'plot_one', where Y is the column), save
; it yourself on the stack BEFORE the call.
.proc calc_row
    lda row_lo,x
    sta ptr1
    txa
    and #7
    tay
    lda row_hi,y
    sta ptr1+1
    rts
.endproc

; One pixel: Y=column (0..39), tmp1=low-resolution row (0..47), tmp3=color (0..15)
.proc plot_one
    lda tmp1
    lsr a			; carry = initial bit 0 (parity of the low-order bit of the string), A = text string
    tax
    tya
    pha				; keep the column - calc_row messes up Y (it uses its own tay)!
    jsr calc_row	; ptr1 = string address; X and carry are unchanged, but Y is
    pla
    tay				; restored the column in Y
    bcs odd_row		; 'carry' survives: neither PHA/PLA/TAY nor calc_row affect it
    lda (ptr1),y
    and #$F0
    ora tmp3
    sta (ptr1),y
    rts
odd_row:
    lda (ptr1),y
    and #$0F
    sta MASKTMP
    lda tmp3
    asl a
    asl a
    asl a
    asl a
    ora MASKTMP
    sta (ptr1),y
    rts
.endproc

.proc _draw_victory_picture
    ; low-res, full screen, page 1, without 80STORE
    lda #0
    sta $C050 ; TXTCLR - graphics
    sta $C053 ; MIXSET - mixed mode: graphics + 4 lines of text
    sta $C054 ; LOWSCR - page 1
    sta $C056 ; LORES
    sta $C000 ; 80STOREOFF - a screen page in main memory

    ; clear the screen: lines 0-19 in black (low resolution), lines 20-23 with spaces
    ; (text below the image), one loop instead of two similar ones
    ldx #0
clearrows:
    jsr calc_row	; X (the loop counter) is not modified inside calc_row
    cpx #20
    bcc fillblack
    lda #$A0
    jmp dofill
fillblack:
    lda #0
dofill:
    sta tmp3
    ldy #39
clearcols:
    lda tmp3
    sta (ptr1),y
    dey
    bpl clearcols
    inx
    cpx #24
    bne clearrows

    ; Draw segments from ring_data. Compact format:
	; byte >= $F0 (except $FF) - change the current color (color = byte-$F0),
	; otherwise - start of a segment (y, x0, x1), drawn in the current color,
	; $FF - end of data.
    lda #<ring_data
    sta ptr2
    lda #>ring_data
    sta ptr2+1
segloop:
    ldy #0
    lda (ptr2),y
    bpl readseg		; 0..39 (positive) - the start of the segment; otherwise, a marker
    cmp #$FF
    beq alldone
    ; color-change marker
    sec
    sbc #$F0
    sta tmp3
    clc
    lda ptr2
    adc #1
    sta ptr2
    bcc segloop
    inc ptr2+1
    jmp segloop
readseg:
    sta tmp1		; A still holds y
    ldy #1
    lda (ptr2),y
    sta tmp2		; x0 (the current column in the loop below)
    ldy #2
    lda (ptr2),y
    sta tmp4		; x1 (right boundary, inclusive)
hlin:
    ldy tmp2
    jsr plot_one
    lda tmp2
    cmp tmp4
    beq hlindone
    inc tmp2
    jmp hlin
hlindone:
    clc
    lda ptr2
    adc #3
    sta ptr2
    bcc segloop
    inc ptr2+1
    jmp segloop
alldone:
    ; text 'YOU WON!' in text line 21
    ldx #21
    jsr calc_row
    ldy #16			; alignment: (40-8)/2
    ldx #0
caption_loop:
    lda caption,x
    beq caption_done
    ora #$80		; regular (non-inverted) text
    sta (ptr1),y
    iny
    inx
    jmp caption_loop
caption_done:
    rts
.endproc

.rodata
caption:
    .byte "YOU WON!", 0

ring_data:
    .byte $F8 ; change the color to 8
    .byte 10, 22, 22
    .byte 11, 17, 18
    .byte 11, 21, 23
    .byte 12, 16, 22
    .byte 13, 16, 18
    .byte 13, 21, 21
    .byte 14, 15, 19
    .byte 15, 15, 21
    .byte 16, 14, 22
    .byte 17, 14, 22
    .byte 18, 14, 20
    .byte 18, 27, 27
    .byte 19, 14, 20
    .byte 19, 26, 27
    .byte 20, 15, 19
    .byte 20, 26, 27
    .byte 21, 16, 19
    .byte 21, 25, 27
    .byte 22, 17, 19
    .byte 22, 25, 27
    .byte 23, 17, 18
    .byte 23, 25, 27
    .byte 24, 25, 26
    .byte 25, 25, 26
    .byte 26, 24, 26
    .byte 27, 24, 25
    .byte 28, 23, 25
    .byte 29, 22, 24
    .byte 30, 21, 24
    .byte 31, 18, 23
    .byte 32, 17, 22
    .byte 33, 17, 21
    .byte $F9 ; change the color to 9
    .byte 5, 19, 21
    .byte 6, 18, 23
    .byte 7, 17, 24
    .byte 8, 16, 25
    .byte 9, 15, 26
    .byte 10, 14, 26
    .byte 11, 13, 16
    .byte 11, 19, 20
    .byte 11, 24, 27
    .byte 12, 12, 15
    .byte 12, 19, 19
    .byte 12, 23, 27
    .byte 13, 12, 15
    .byte 13, 19, 27
    .byte 14, 12, 14
    .byte 14, 20, 28
    .byte 15, 11, 14
    .byte 15, 22, 28
    .byte 16, 11, 13
    .byte 16, 23, 28
    .byte 17, 11, 13
    .byte 17, 23, 28
    .byte 18, 11, 13
    .byte 18, 21, 28
    .byte 19, 11, 13
    .byte 19, 21, 25
    .byte 19, 28, 28
    .byte 20, 11, 14
    .byte 20, 20, 25
    .byte 20, 28, 28
    .byte 21, 11, 15
    .byte 21, 20, 24
    .byte 21, 28, 28
    .byte 22, 11, 16
    .byte 22, 20, 24
    .byte 22, 28, 28
    .byte 23, 11, 16
    .byte 23, 19, 24
    .byte 23, 28, 28
    .byte 24, 11, 23
    .byte 24, 27, 27
    .byte 25, 11, 22
    .byte 25, 27, 27
    .byte 26, 11, 22
    .byte 26, 27, 27
    .byte 27, 11, 21
    .byte 27, 26, 26
    .byte 28, 12, 20
    .byte 28, 26, 26
    .byte 29, 12, 19
    .byte 29, 25, 25
    .byte 30, 13, 17
    .byte 30, 25, 25
    .byte 31, 13, 17
    .byte 31, 24, 24
    .byte 32, 14, 16
    .byte 32, 23, 23
    .byte 33, 15, 16
    .byte 33, 22, 22
    .byte 34, 16, 21
    .byte $FF ; end of data
