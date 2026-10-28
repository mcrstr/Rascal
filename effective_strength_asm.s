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
; unsigned char effective_strength(void). Strength including the Ring of Power,
; called for every hit roll in combat. ring_worn_effect() is called with a
; parameter passed directly to A, without a stack. No parameters (void), just jsr/rts.
;
; pstat (player_stats_t): hp, hp_max, strength, level, xp(4), defense,
; hunger(2) - strength at offset 2 (see also effective_defense_asm.s,
; where defense at offset 8 is the same structure).

.export _effective_strength
.import _pstat
.import _ring_worn_effect

RING_EFFECT_STRENGTH = 2
PSTAT_STRENGTH_OFFSET = 2

.segment "CODE"

.proc _effective_strength
    lda     #RING_EFFECT_STRENGTH
    jsr     _ring_worn_effect   ; A = 0/1/2 (how many rings are worn)
    asl     a                   ; A = 0/2/4
    clc
    adc     _pstat + PSTAT_STRENGTH_OFFSET
    ldx     #0
    rts
.endproc
