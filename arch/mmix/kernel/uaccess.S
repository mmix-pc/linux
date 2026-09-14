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
