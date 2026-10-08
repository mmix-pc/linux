/* SPDX-License-Identifier: GPL-2.0-only */
.text
.balign 4
.globl mmix_futex_cmpxchg
mmix_futex_cmpxchg:
	OR r0,r231,0
	OR r1,r232,0
	OR r2,r233,0
	OR r3,r234,0
	GET r14,rP
	ANDN r4,r1,7
	AND r6,r1,4
	SLU r6,r6,3
	SETL r7,32
	SUBU r6,r7,r6
	SETML r7,0xffff
	ORL r7,0xffff
	AND r2,r2,r7
	AND r3,r3,r7
1:	LDOU r8,r4,0
	SRU r9,r8,r6
	AND r9,r9,r7
	STTU r9,r0,0
	CMPU r10,r9,r2
	BNZ r10,3f
	SLU r11,r7,r6
	ANDN r12,r8,r11
	SLU r13,r3,r6
	OR r12,r12,r13
	PUT rP,r8
	SYNC 3
2:	CSWAP r12,r4,0
	BZB r12,1b
	SYNC 3
3:	SETL r231,0
4:	PUT rP,r14
	POP 0,0
5:	SETL r231,14
	NEGU r231,0,r231
	JMPB 4b

.pushsection __ex_table,"a"
.balign 8
.quad 1b,5b
.quad 2b,5b
.popsection
