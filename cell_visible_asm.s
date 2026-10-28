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
; unsigned char cell_visible(unsigned char x, unsigned char y, zone_t *pr)
; Each cell of the map is checked every time the screen is redrawn.
;
; zone_t: pos.x,pos.y,size.x,size.y,flags - offsets 0,1,2,3,4 (see room_asm.s).
; player: pt_t {x,y} - offsets 0,1 (_player, _player+1).
; FOV_RADIUS = 1 (main.c) - visibility outside the illuminated room: cells no
; further than 1 along each axis from the player.

.export _cell_visible
.import incsp2
.importzp sp, ptr1, tmp1, tmp2, tmp3
.import _player

FOV_LIMIT = 2  ; compare with FOV_RADIUS+1 = 2: 'difference >= 2' means 'not visible'

.segment "CODE"

.proc _cell_visible
    sta     ptr1
    stx     ptr1+1	; ptr1 = pr

    ldy     #0
    lda     (sp),y
    sta     tmp2	; tmp2 = y
    ldy     #1
    lda     (sp),y
    sta     tmp1	; tmp1 = x

    ; if pr == NULL, then proceed to checking the player's surroundings
    lda     ptr1
    ora     ptr1+1
    beq     @checkFOV

    ; x >= pr->pos.x ?
    ldy     #0
    lda     tmp1
    cmp     (ptr1),y
    bcc     @checkFOV

    ; x < pr->pos.x + pr->size.x ?
    lda     (ptr1),y	; pos.x
    clc
    ldy     #2
    adc     (ptr1),y	; + size.x
    sta     tmp3
    lda     tmp1
    cmp     tmp3
    bcs     @checkFOV

    ; y >= pr->pos.y ?
    ldy     #1
    lda     tmp2
    cmp     (ptr1),y
    bcc     @checkFOV

    ; y < pr->pos.y + pr->size.y ?
    lda     (ptr1),y	; pos.y
    clc
    ldy     #3
    adc     (ptr1),y	; + size.y
    sta     tmp3
    lda     tmp2
    cmp     tmp3
    bcs     @checkFOV

    ; if inside the lit room - can see the entire room
    lda     #1
    ldx     #0
    jmp     incsp2

@checkFOV:
    ; dx = |x - player.x|
    lda     tmp1
    sec
    sbc     _player
    bcs     @dxReady
    eor     #$FF
    clc
    adc     #1
@dxReady:
    cmp     #FOV_LIMIT
    bcs     @notVisible

    ; dy = |y - player.y|
    lda     tmp2
    sec
    sbc     _player+1
    bcs     @dyReady
    eor     #$FF
    clc
    adc     #1
@dyReady:
    cmp     #FOV_LIMIT
    bcs     @notVisible

    lda     #1
    ldx     #0
    jmp     incsp2

@notVisible:
    lda     #0
    ldx     #0
    jmp     incsp2
.endproc
