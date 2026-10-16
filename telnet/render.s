; SymTerm - fast text-cell renderer for 16-colour (4bpp) pixel rows.
;
; void tr_render(char* dst, char* text, char* attr, unsigned char n)
;
; Draws n consecutive 6x8 character cells into the scratch row buffer.
; The scratch buffer is 256-byte aligned with a 256-byte line stride, so
; moving to the next pixel line is just "inc d".  Each cell is 3 bytes
; (6 pixels, high nibble = left pixel).  dst = scratch + column*3.
;
; Globals (set up from C):
;   _tr_font3    - pointer to the expanded font: 24 bytes per glyph, one
;                  byte per pixel pair holding a 0..3 lookup index, stored in
;                  "snake" order: even lines left->right, odd lines right->left
;   _tr_ctab     - pointer to 1024-byte colour table: for each attribute,
;                  4 bytes = pixel pair (bg,bg) (bg,fg) (fg,bg) (fg,fg)
;   _tr_curpage  - high byte of a 256-aligned 4-byte scratch area
;
; Register use in the inner loop: HL = glyph data, DE = destination,
; B = lookup page, C = lookup index.  Preserves BC, IX, IY for SCC.

.z80
.code

.export _tr_render
_tr_render:
	push bc
	push ix
	push iy
	ld hl,#8
	add hl,sp            ; (hl)=dst, +2=text, +4=attr, +6=n
	ld e,(hl)
	inc hl
	ld d,(hl)
	inc hl
	ld c,(hl)
	inc hl
	ld b,(hl)
	inc hl
	push bc
	pop iy               ; IY = text pointer
	ld c,(hl)
	inc hl
	ld b,(hl)
	inc hl
	push bc
	pop ix               ; IX = attr pointer
	ld a,(hl)
	ld (tr_count),a
	; force a colour reload for the first cell
	ld a,(ix+0)
	cpl
	ld (tr_lastattr),a

tr_charloop:
	; ---- attribute -> 4-byte pixel-pair table at (curpage*256)
	ld a,(ix+0)
	inc ix
	ld hl,#tr_lastattr
	cp (hl)
	jr z,tr_samecol
	ld (hl),a
	push de
	ld l,a
	ld h,#0
	add hl,hl
	add hl,hl
	ld bc,(_tr_ctab)
	add hl,bc
	ld a,(_tr_curpage)
	ld d,a
	ld e,#0
	ldi
	ldi
	ldi
	ldi
	pop de
tr_samecol:
	; ---- glyph pointer = font3 + ch*24 -> HL
	ld l,(iy+0)
	inc iy
	ld h,#0
	add hl,hl
	add hl,hl
	add hl,hl            ; ch*8
	ld b,h
	ld c,l
	add hl,hl
	add hl,bc            ; ch*24
	ld bc,(_tr_font3)
	add hl,bc
	ld a,(_tr_curpage)
	ld b,a

	; line 0 (left -> right)
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	inc e
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	inc e
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	inc d
	; line 1 (right -> left)
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	dec e
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	dec e
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	inc d
	; line 2
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	inc e
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	inc e
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	inc d
	; line 3
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	dec e
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	dec e
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	inc d
	; line 4
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	inc e
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	inc e
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	inc d
	; line 5
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	dec e
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	dec e
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	inc d
	; line 6
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	inc e
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	inc e
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	inc d
	; line 7
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	dec e
	ld c,(hl)
	inc hl
	ld a,(bc)
	ld (de),a
	dec e
	ld c,(hl)
	ld a,(bc)
	ld (de),a
	; back to line 0, next cell (+3 bytes)
	ld a,d
	sub #7
	ld d,a
	inc e
	inc e
	inc e
	ld hl,#tr_count
	dec (hl)
	jp nz,tr_charloop

	pop iy
	pop ix
	pop bc
	ret

tr_lastattr:
	.ds 1
tr_count:
	.ds 1
