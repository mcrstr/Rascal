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
; lfsr_next() - 16-bit LFSR, polynomial x^16+x^14+x^13+x^11+1.
;
; Exported as a standard cc65 function: returns an unsigned int in A(lo)/X(hi).
; _state is a resident variable in rng.c (2 bytes, little-endian), not static,
; so it is accessible from here via .import.

.export _lfsr_next
.import _state

.segment "CODE"

.proc _lfsr_next
    ; feedback = bit0(state) ^ bit2(state) ^ bit3(state) ^ bit5(state)
    ; all four bits are in the least significant byte of _state;
	; the most significant byte is not used.
    lda     _state
    and     #$01			; bit 0
    sta     tmp

    lda     _state
    and     #$04			; bit 2
    beq     @skip2
    lda     tmp
    eor     #$01
    sta     tmp
@skip2:
    lda     _state
    and     #$08			; bit 3
    beq     @skip3
    lda     tmp
    eor     #$01
    sta     tmp
@skip3:
    lda     _state
    and     #$20			; bit 5
    beq     @skip5
    lda     tmp
    eor     #$01
    sta     tmp
@skip5:
    ; state >>= 1 (16 bits: most significant byte first, carry goes to the least significant byte)
    lsr     _state+1
    ror     _state

    ; insert the feedback as the new most significant bit of the state (bit 15 = bit 7 of the most significant byte)
    lda     tmp
    beq     @noFeed
    lda     _state+1
    ora     #$80
    sta     _state+1
@noFeed:
    ; return an 'unsigned int' according to the cc65 convention: A = least significant byte, X = most significant byte
    lda     _state
    ldx     _state+1
    rts
.endproc

.bss
tmp:    .res 1
