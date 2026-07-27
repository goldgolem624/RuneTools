extern lpRemain:QWORD
extern RSExe:QWORD
extern Hook_test:proto
extern asm_Gpointer:QWORD
extern asm_Gcounter:QWORD
extern asm_Gx:QWORD
extern asm_Gy:QWORD

.code 
hkGetValue proc
	add asm_Gcounter,r15	;rs internal timer
	mov asm_Gpointer,rbx	;pointer to currently worked on tile 				
	mov [rsp+68h],rax		;now original call
	jmp lpRemain			;jump back
hkGetValue endp
end