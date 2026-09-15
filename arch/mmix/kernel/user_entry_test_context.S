/* SPDX-License-Identifier: GPL-2.0-only */
.section .rodata,"a"
.balign 4
.globl mmix_user_test_start
mmix_user_test_start:
	SETL r29,0x7654
	SETL r231,0x1234
	TRAP 1,0,0
	OR r233,r232,0
	TRAP 1,0,0
	JMP .
.globl mmix_user_test_deep
mmix_user_test_deep:
	SETL r32,64
	PUSHJ r31,.Lrecursive
	SETL r231,0x1234
	TRAP 1,0,0
	JMP .
.Lrecursive:
	GET r1,rJ
	XOR r29,r0,0x54
	SUBU r0,r0,1
	BZ r0,.Lspin
	OR r32,r0,0
	PUSHJB r31,.Lrecursive
	JMP .Lreturn
.Lspin:
	SETML r2,0x100
.Lspin_loop:
.globl mmix_user_test_spin
mmix_user_test_spin:
	SUBU r2,r2,1
	BNZB r2,.Lspin_loop
.Lreturn:
	ADDU r2,r0,1
	XOR r2,r2,0x54
	CMPU r2,r29,r2
	BNZ r2,.Lbad
	PUT rJ,r1
	POP 0,0
.Lbad:
	SETL r231,0
	TRAP 1,0,0
	JMP .
.globl mmix_user_test_kernel
mmix_user_test_kernel:
	SETH r232,0x2000
	LDOU r231,r232,0
	JMPB .Lbad
.globl mmix_user_test_rg
mmix_user_test_rg:
	PUT rG,255
	TRAP 1,0,0
	JMP .
.globl mmix_user_test_pointer
mmix_user_test_pointer:
	SETH r230,0x8000
	SETH r254,0x8000
	TRAP 1,0,0
	JMP .
.globl mmix_user_test_end
.globl mmix_user_test_save
mmix_user_test_save:
	SETL r0,0x4567
	SETL r63,0x6789
	SAVE r250
	UNSAVE r250
	SETL r231,0x1234
	TRAP 1,0,0
	JMP .
.globl mmix_user_test_tp
mmix_user_test_tp:
	SETH r230,0x8000
	TRAP 1,0,0
	JMP .
.globl mmix_user_test_privileged
mmix_user_test_privileged:
	PUT rK,0
	JMPB .Lbad
.globl mmix_user_test_unsave
mmix_user_test_unsave:
	SETH r232,0x8000
	UNSAVE r232
	JMPB .Lbad
.globl mmix_user_test_restore
mmix_user_test_restore:
	SETL r0,0x4567
	SETL r63,0x6789
	SAVE r250
	TRAP 1,0,0
	UNSAVE r250
	SETL r231,0x1234
	TRAP 1,0,0
	JMP .
.globl mmix_user_test_pending
mmix_user_test_pending:
	SETL r29,0x4567
	PUSHJ r31,.Lpending
	JMPB .Lbad
.Lpending:
	TRAP 1,0,0
	JMPB .Lbad
mmix_user_test_end:
