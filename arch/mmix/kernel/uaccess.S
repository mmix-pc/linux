/* SPDX-License-Identifier: GPL-2.0-only */
.text
.balign 4
.globl raw_copy_from_user
.globl raw_copy_to_user
raw_copy_from_user:
raw_copy_to_user:
	OR r234,r231,0
	OR r231,r233,0
	BZ r231,3f
1:	LDBU r235,r232,0
2:	STBU r235,r234,0
	ADDU r232,r232,1
	ADDU r234,r234,1
	SUBU r231,r231,1
	BNZB r231,1b
3:	POP 0,0

.pushsection __ex_table,"a"
.balign 8
.quad 1b,3b
.quad 2b,3b
.popsection

.text
.globl __clear_user
__clear_user:
	OR r234,r231,0
	OR r231,r232,0
	SETL r235,0
	BZ r231,2f
1:	STBU r235,r234,0
	ADDU r234,r234,1
	SUBU r231,r231,1
	BNZB r231,1b
2:	POP 0,0
.pushsection __ex_table,"a"
.balign 8
.quad 1b,2b
.popsection

/* Aligned futex/TID words need one indivisible access, including kernel clears. */
.text
.balign 4
.globl mmix_user_load32
mmix_user_load32:
	OR r0,r231,0
	SETL r1,0
1:	LDTU r1,r0,0
2:	STTU r1,r232,0
	SETL r231,0
	POP 0,0
3:	SETL r1,0
	STTU r1,r232,0
	SETL r231,14
	NEGU r231,0,r231
	POP 0,0

.globl mmix_user_store32
mmix_user_store32:
	OR r0,r231,0
	LDTU r1,r232,0
4:	STTU r1,r0,0
	SETL r231,0
	POP 0,0
5:	SETL r231,14
	NEGU r231,0,r231
	POP 0,0

.pushsection __ex_table,"a"
.balign 8
.quad 1b,3b
.quad 4b,5b
.popsection
