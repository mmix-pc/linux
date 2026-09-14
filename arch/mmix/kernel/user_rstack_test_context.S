/* SPDX-License-Identifier: GPL-2.0-only */
#include "user_rstack_test.h"
.text
.balign 4
.globl mmix_test_user_save
mmix_test_user_save:
	SAVE r255
	GETA r240,%geta(.Lcapture_saved)
	STOU r255,r240,0
	OR r248,r231,0
	GET r240,rO
	STOU r240,r248,40
	SETL r230,0x2345
	SETML r254,0x70
	SETML r240,3
	PUT rA,r240
	SETL r240,0x6789
	PUT rH,r240
	SETL r240,0x1234
	PUT rM,r240
	SETL r240,0x45
	PUT rR,r240
	SETL r240,0x5a
	PUT rP,r240
	SETL r240,0x77
	PUT rD,r240
	SETL r247,0
	SETL r32,64
	PUSHJ r31,.Lrecursive
	GETA r240,%geta(.Lcapture_saved)
	STOU r247,r240,8
	LDOU r255,r240,0
	UNSAVE r255
	GETA r255,%geta(.Lcapture_saved)
	LDOU r231,r255,8
	POP 0,0
.Lrecursive:
	GET r1,rJ
	XOR r29,r0,0x54
	SUBU r0,r0,1
	BZ r0,.Lcapture
	OR r32,r0,0
	PUSHJB r31,.Lrecursive
	JMP .Lreturn
.Lcapture:
	GET r249,rO
	STOU r249,r248,0
	GET r250,rS
	STOU r250,r248,8
	GET r251,rL
	STOU r251,r248,16
	GET r251,rG
	STOU r251,r248,24
	SAVE r255
	STOU r255,r248,32
	ADDU r251,r248,MMIX_TEST_SAVE_DATA
.Lcopy:
	LDOU r252,r250,0
	STOU r252,r251,0
	ADDU r251,r251,8
	ADDU r250,r250,8
	CMPU r252,r250,r255
	BNPB r252,.Lcopy
	UNSAVE r255
.Lreturn:
	ADDU r2,r0,1
	XOR r2,r2,0x54
	CMPU r2,r29,r2
	OR r247,r247,r2
	PUT rJ,r1
	POP 0,0

/* Staged image is kernel-owned; execute no C on its temporary register stack. */
.globl mmix_test_user_restore
mmix_test_user_restore:
	SAVE r255
	GETA r240,%geta(.Lsaved)
	STOU r255,r240,0
	OR r255,r231,0
	UNSAVE r255
	CMPU r240,r0,0
	CMPU r241,r29,0x55
	OR r240,r240,r241
	GET r241,rL
	CMPU r241,r241,30
	OR r240,r240,r241
	GET r241,rH
	SETL r242,0x6789
	CMPU r241,r241,r242
	OR r240,r240,r241
	SETL r242,0x2345
	CMPU r241,r230,r242
	OR r240,r240,r241
	GETA r255,%geta(.Lsaved)
	STOU r240,r255,8
	LDOU r255,r255,0
	UNSAVE r255
	GETA r255,%geta(.Lsaved)
	LDOU r231,r255,8
	POP 0,0
.bss
.balign 8
.Lsaved:
	.space 16

.Lcapture_saved:
	.space 16
