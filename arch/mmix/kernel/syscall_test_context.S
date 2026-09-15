/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/unistd.h>
.section .rodata,"a"
.balign 4
.globl mmix_syscall_test_start
mmix_syscall_test_start:
	TRAP 1,0,0
	TRAP 1,0,0
	JMP .
.globl mmix_syscall_test_deep
mmix_syscall_test_deep:
	SETL r32,64
	PUSHJ r31,.Lrecursive
	SETL r231,0x1234
	SETL r237,0xffff
	TRAP 1,0,0
	JMP .
.Lrecursive:
	GET r1,rJ
	XOR r29,r0,0x54
	SUBU r0,r0,1
	BZ r0,.Lsleep
	OR r32,r0,0
	PUSHJB r31,.Lrecursive
	JMP .Lreturn
.Lsleep:
	SETL r240,20
	SETML r238,0x80
.Lagain:
	OR r231,r238,0
	SETL r232,0
	SETL r237,__NR_nanosleep
	TRAP 1,0,0
	BNZ r231,.Lbad
	SUBU r240,r240,1
	BNZB r240,.Lagain
.Lreturn:
	ADDU r2,r0,1
	XOR r2,r2,0x54
	CMPU r2,r29,r2
	BNZ r2,.Lbad
	PUT rJ,r1
	POP 0,0
.Lbad:
	SETL r231,0
	SETL r237,0xffff
	TRAP 1,0,0
	JMP .
.globl mmix_syscall_test_end
mmix_syscall_test_end:
