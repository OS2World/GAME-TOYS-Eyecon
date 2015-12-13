; INITDLL.ASM

; The dll initialization routine that has to be
; in stinking machine language

extrn	Initdll:near
	assume	cs:_TEXT
_TEXT	segment byte public 'CODE'

START	proc	far
	push	di			;save module ID
	call	Initdll
	ret
START	endp

_TEXT	ends
	end	START
