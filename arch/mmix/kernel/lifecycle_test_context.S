/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/unistd.h>
.section .rodata,"a"
.balign 8
.globl mmix_lifecycle_image
mmix_lifecycle_image:
.incbin "arch/mmix/kernel/lifecycle_test_user.elf"
.globl mmix_lifecycle_image_end
mmix_lifecycle_image_end:
.balign 4
.globl mmix_lifecycle_start
mmix_lifecycle_start:
	SETML r238,0x60
	SETL r241,100
	SETL r230,42
/* Three private-state allocation failures, then one kernel-stack failure. */
	SETL r240,4
.Lfailures:
	SETL r231,17
	SETL r232,0
	SETL r237,__NR_clone
	TRAP 1,0,0
	NEGU r233,0,r231
	CMPU r233,r233,12
	BNZ r233,.Lbad
	SUBU r240,r240,1
	BNZB r240,.Lfailures
.Lcycle:
	SETL r233,0x1234
	STOU r233,r238,0
	STOU r233,r254,0
	SETL r32,64
	PUSHJ r31,.Lrecursive
	BZ r239,.Lchild
/* Parent waits for its exact child and verifies both private backing areas. */
	OR r231,r239,0
	ADDU r232,r238,8
	SETL r233,0
	SETL r234,0
	SETL r237,__NR_wait4
	TRAP 1,0,0
	CMPU r233,r231,r239
	BNZ r233,.Lbad
	LDTU r233,r238,8
	SETL r234,0x2500
	CMPU r233,r233,r234
	BNZ r233,.Lbad
	LDOU r233,r238,0
	SETL r234,0x1234
	CMPU r233,r233,r234
	BNZ r233,.Lbad
	LDOU r233,r254,0
	CMPU r233,r233,r234
	BNZ r233,.Lbad
	CMPU r233,r230,42
	BNZ r233,.Lbad
	SUBU r241,r241,1
	BNZB r241,.Lcycle
	SETL r231,0
	SETL r237,__NR_exit
	TRAP 1,0,0
	JMP .
.Lchild:
	SETL r233,0x5678
	STOU r233,r238,0
	STOU r233,r254,0
	CMPU r233,r230,42
	BNZ r233,.Lbad
	OR r231,r242,0
	ADDU r232,r238,64
	ADDU r233,r238,32
	SETL r234,0
	SETL r235,0x1000
	SETL r237,__NR_execveat
	TRAP 1,0,0
	JMP .Lbad
.Lrecursive:
	GET r1,rJ
	XOR r29,r0,0x54
	SUBU r0,r0,1
	BZ r0,.Lclone
	OR r32,r0,0
	PUSHJB r31,.Lrecursive
	JMP .Lreturn
.Lclone:
	SETL r231,17
	SETL r232,0
	SETL r237,__NR_clone
	TRAP 1,0,0
	BN r231,.Lbad
	OR r239,r231,0
/* Both branches sleep with the inherited deep register stack. */
	ADDU r231,r238,80
	SETL r232,0
	SETL r237,__NR_nanosleep
	TRAP 1,0,0
	BNZ r231,.Lbad
.Lreturn:
	ADDU r2,r0,1
	XOR r2,r2,0x54
	CMPU r2,r29,r2
	BNZ r2,.Lbad
	PUT rJ,r1
	POP 0,0
.Lbad:
	SETL r231,99
	SETL r237,__NR_exit_group
	TRAP 1,0,0
	JMP .
.globl mmix_lifecycle_end
mmix_lifecycle_end:
