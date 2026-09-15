/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/unistd.h>
.text
.balign 4
.globl _start
_start:
	TRAP 1,0,0
	GETA r240,%geta(bss_value)
	LDOU r241,r240,0
	BNZ r241,.Lbad
	SETL r241,0x1234
	STOU r241,r240,0
	SETL r230,0x5678
	OR r231,r239,0
	GETA r232,%geta(empty)
	GETA r233,%geta(argv)
	GETA r234,%geta(envp)
	SETL r235,0x1000
	SETL r237,__NR_execveat
	TRAP 1,0,0
.Lbad:
	SETL r237,0xffff
	TRAP 1,0,0
	JMP .
.data
.balign 8
empty: .byte 0
.balign 8
argv: .8byte name,arg,0
envp: .8byte env,0
name: .asciz "mmix-exec"
arg: .asciz "probe"
env: .asciz "MMIX_EXEC=1"
.bss
.balign 8192
bss_value: .zero 8192
