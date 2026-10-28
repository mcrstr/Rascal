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
; unsigned char effective_defense(void). Final AC (how hard to hit
; the player): armor, left/right hand (the shield provides mod1), plus
; the defense ring. Called for every attack - whether your own or the enemy's.
; calc_inv_addr is shared with armor_pv_asm.s and exported from there.

.export _effective_defense
.import _g_eq_armor, _g_eq_hand_l, _g_eq_hand_r
.import _g_inventory, _pstat
.import _ring_worn_effect
.import calc_inv_addr
.importzp ptr1, tmp4

ITEM_SHIELD = 12				; item_type_t in game_shared.h - check this whenever a new item type is added!
OFF_TYPE = 0
OFF_MOD1 = 3
PSTAT_DEFENSE_OFFSET = 8		; hp,hp_max,strength,level(1)+xp(4 byte)+defense - see main.c player_stats_t
RING_EFFECT_PROTECTION = 0

.segment "CODE"

.proc _effective_defense
    lda     _pstat + PSTAT_DEFENSE_OFFSET
    sta     tmp4 ; d = pstat.defense (now 0 only)

	; armor: mod1, type is always ITEM_ARMOR
    lda     _g_eq_armor
    bmi     @doneArmor
    jsr     calc_inv_addr
    ldy     #OFF_MOD1
    lda     (ptr1),y
    clc
    adc     tmp4
    sta     tmp4
@doneArmor:

	; left hand: SHIELD only -> mod1
    lda     _g_eq_hand_l
    bmi     @doneHandL
    jsr     calc_inv_addr
    ldy     #OFF_TYPE
    lda     (ptr1),y
    cmp     #ITEM_SHIELD
    bne     @doneHandL
    ldy     #OFF_MOD1
    lda     (ptr1),y
    clc
    adc     tmp4
    sta     tmp4
@doneHandL:

	; right hand: SHIELD only -> mod1
    lda     _g_eq_hand_r
    bmi     @doneHandR
    jsr     calc_inv_addr
    ldy     #OFF_TYPE
    lda     (ptr1),y
    cmp     #ITEM_SHIELD
    bne     @doneHandR
    ldy     #OFF_MOD1
    lda     (ptr1),y
    clc
    adc     tmp4
    sta     tmp4
@doneHandR:

	; + 2 * ring_worn_effect(RING_EFFECT_PROTECTION)
    lda     #RING_EFFECT_PROTECTION
    jsr     _ring_worn_effect	; returns 0/1/2 to A
    asl     a					; *2
    clc
    adc     tmp4
    sta     tmp4

    lda     tmp4
    ldx     #0
    rts
.endproc
