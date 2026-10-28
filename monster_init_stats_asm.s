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
; void monster_init_stats(monster_t *m, unsigned char species).
; Sets the monster's stats based on the template; called for every
; monster spawn (regular level spawns + up to 7 additional monsters
; in the treasure chamber).
;
; monster_template_t (14 bytes): letter, name(2), hp_lo, hp_hi, attack,
; defense, dmg_lo, dmg_hi, depth_lo, depth_hi, chance_to_drop, exp(2).
; monster_t: x,y,awake,alive,hp,attack,defense,dmg_lo,dmg_hi,
; paralyzed,is_merchant,species - offsets 0..11 (see also
; monster_at_asm.s, same MONSTER_SIZE=12).
;
; IMPORTANT note about rnd(): as in hand_weapon_damage_asm.s, calling rnd()
; can corrupt ptr1-4/tmp1-4 (the compiler does not guarantee their integrity
; when calling a third-party C function). Therefore, the only value that
; needs to survive the rnd() call (the template's hp_lo) is pushed onto
; the hardware stack via PHA: this is guaranteed to be safe regardless of
; the inner workings of rnd(). The pointer to m is also safe: it resides on
; the cc65 software stack (placed there by the caller), not in
; ptr1-4, so it survives the nested call without any problems. After the rnd()
; call, we simply recalculate the template's address - this is inexpensive
; (a few shifts and an addition) and does not require storing it through
; the risky section.
;
; Call convention (confirmed by disassembled call code in main.c): m is
; pushed onto the stack by the caller via pushax (2 bytes), species
; is loaded into A, followed by jsr.

.export _monster_init_stats
.import _monster_templates
.import _rnd
.import incsp3, pusha
.importzp sp, ptr1, ptr2, tmp1, tmp2

MT_SIZE    = 14
MT_HP_LO   = 3
MT_HP_HI   = 4
MT_ATTACK  = 5
MT_DEFENSE = 6
MT_DMG_LO  = 7
MT_DMG_HI  = 8

M_HP          = 4
M_ATTACK      = 5
M_DEFENSE     = 6
M_DMG_LO      = 7
M_DMG_HI      = 8
M_PARALYZED   = 9
M_IS_MERCHANT = 10
M_SPECIES     = 11

.segment "CODE"

.proc _monster_init_stats
    jsr     pusha				; put species on the software stack as well -
								; it will survive rnd() just like m, which is already there
    ; now on the software stack: (sp)+0 = species, (sp)+1,+2 = m (from the caller)

    ldy     #0
    lda     (sp),y				; A = species
    asl     a
    sta     tmp1				; species*2
    asl     a
    sta     tmp2				; species*4
    asl     a					; species*8
    clc
    adc     tmp1
    clc
    adc     tmp2				; = species*14 (18*14=252 max)
    clc
    adc     #<_monster_templates
    sta     ptr1
    lda     #0
    adc     #>_monster_templates
    sta     ptr1+1
    ; ptr1 = &monster_templates[species]

    ldy     #MT_HP_LO
    lda     (ptr1),y
    pha							; hp_lo - to the hardware stack; will survive rnd()

    ldy     #MT_HP_HI
    lda     (ptr1),y
    sec
    ldy     #MT_HP_LO
    sbc     (ptr1),y
    clc
    adc     #1					; A = range = hp_hi-hp_lo+1

    jsr     _rnd				; A = rnd(range) - after that, we leave ptr1/tmp1/tmp2 as they are

    sta     ptr2				; temporarily store the result of the roll (ptr1 is no longer needed for the template)
    pla							; A = hp_lo (from the hardware stack - intact to be an integer)
    clc
    adc     ptr2				; + roll = total HP
    sta     ptr2				; ptr2 = total HP (again, temporarily, as a scratch variable)

    ; restore the pointer to m from the program stack
    ldy     #1
    lda     (sp),y
    sta     ptr1
    ldy     #2
    lda     (sp),y
    sta     ptr1+1
    ; ptr1 = m

    ldy     #M_HP
    lda     ptr2
    sta     (ptr1),y			; m->hp = total hp

    ldy     #0
    lda     (sp),y				; species again from the program stack
    ldy     #M_SPECIES
    sta     (ptr1),y			; m->species = species

    ; recalculate the template address one more time - it's inexpensive and risk-free
    ldy     #0
    lda     (sp),y
    asl     a
    sta     tmp1
    asl     a
    sta     tmp2
    asl     a
    clc
    adc     tmp1
    clc
    adc     tmp2
    clc
    adc     #<_monster_templates
    sta     ptr2
    lda     #0
    adc     #>_monster_templates
    sta     ptr2+1
    ; ptr2 = &monster_templates[species] (again)

    ldy     #MT_ATTACK
    lda     (ptr2),y
    ldy     #M_ATTACK
    sta     (ptr1),y

    ldy     #MT_DEFENSE
    lda     (ptr2),y
    ldy     #M_DEFENSE
    sta     (ptr1),y

    ldy     #MT_DMG_LO
    lda     (ptr2),y
    ldy     #M_DMG_LO
    sta     (ptr1),y

    ldy     #MT_DMG_HI
    lda     (ptr2),y
    ldy     #M_DMG_HI
    sta     (ptr1),y

    lda     #0
    ldy     #M_PARALYZED
    sta     (ptr1),y
    ldy     #M_IS_MERCHANT
    sta     (ptr1),y

    jmp     incsp3				; free up species(1, our push) + m(2, the caller) = 3 bytes
.endproc
