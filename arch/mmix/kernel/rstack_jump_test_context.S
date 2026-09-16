/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/unistd.h>
.section .rodata,"a"
.balign 4
.macro save_environment continuation
	STOU r253,r240,0
	GETA r255,%geta(\continuation)
	STOU r255,r240,8
	STOU r254,r240,16
	GET r255,rO
	STOU r255,r240,24
	ADDU r250,r240,32
	.irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	STOU r\reg,r250,0
	ADDU r250,r250,8
	.endr
.endm
.macro seed_locals
	.irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	SETL r\reg,(1000+\reg)
	.endr
.endm
.globl mmix_jump_test_start
mmix_jump_test_start:
	SETML r238,0x60
	ADDU r240,r238,0
	INCL r240,4096
	OR r241,r238,0
	INCL r241,256
	SETL r244,0
	GETA r255,%geta(.Lhandler)
	STOU r255,r238,0
	SETML r255,0x1400
	CMPU r231,r242,3
	BZ r231,.Lnodefer
	CMPU r231,r242,4
	BZ r231,.Lnodefer
	CMPU r231,r242,7
	BZ r231,.Lnodefer
	CMPU r231,r242,63
	BZ r231,.Lnodefer
	CMPU r231,r242,67
	BNZ r231,.Ldefer
.Lnodefer:
	ORML r255,0x4000
.Ldefer:
	ORL r255,4
	CMPU r231,r242,7
	BNZ r231,.Laction
	ORML r255,0x800
.Laction:
	STOU r255,r238,8
	GETA r255,%geta(.Lrestorer)
	STOU r255,r238,16
	SETL r255,0
	STOU r255,r238,24
	SETL r231,10
	OR r232,r238,0
	SETL r233,0
	SETL r234,8
	SETL r237,__NR_rt_sigaction
	TRAP 1,0,0
	BNZ r231,.Lbad
	SETL r231,12
	OR r232,r238,0
	SETL r237,__NR_rt_sigaction
	TRAP 1,0,0
	BNZ r231,.Lbad
	CMPU r255,r242,7
	BNZ r255,.Lquery
	SETML r255,0x6f
	STOU r255,r238,64
	SETML r255,0x8000
	STTU r255,r238,72
	SETML r255,1
	STOU r255,r238,80
	ADDU r231,r238,64
	SETL r232,0
	SETL r237,__NR_sigaltstack
	TRAP 1,0,0
	BNZ r231,.Lbad
.Lquery:
	OR r231,r240,0
	INCL r231,280
	SETL r237,__NR_mmix_rstack_query
	TRAP 1,0,0
	BNZ r231,.Lbad
	seed_locals
	save_environment .Ldone
	CMPU r255,r242,64
	BZ r255,.Ldeepstart
	CMPU r255,r242,65
	BZ r255,.Ldeepstart
	CMPU r255,r242,67
	BZ r255,.Lsignal
	CMPU r255,r242,66
	BNZ r255,.Lordinary
	.irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	SETL r\reg,(2000+\reg)
	.endr
.Lordinary:
	CMPU r255,r242,1
	BZ r255,.Lolder
	CMPU r255,r242,59
	BZ r255,.Lolder
	CMPU r255,r242,41
	BZ r255,.Lpartial
	CMPU r255,r242,42
	BZ r255,.Lpartial
	CMPU r255,r242,43
	BZ r255,.Lread
	CMPU r255,r242,63
	BZ r255,.Lsignal
	CMPU r255,r242,55
	BZ r255,.Lsignal
	CMPU r255,r242,57
	BZ r255,.Lsignal
	CMPU r255,r242,2
	BN r255,.Ljump
	CMPU r255,r242,8
	BNP r255,.Lsignal
	JMP .Ljump
.Lolder:
	PUSHJ r31,.Ljump
	JMP .Lbad
.Lpartial:
	PUSHJ r31,.Lpopcallee
	JMP .Lbad
.Lpopcallee:
	seed_locals
	CMPU r255,r242,42
	BNZ r255,.Lpopready
	save_environment .Ldone
.Lpopready:
	SETL r229,123
	SETL r237,__NR_mmix_rstack_sync
	TRAP 1,0,0
.globl mmix_jump_pop
mmix_jump_pop:
	POP 0,0
	JMP .Lbad
.Ldeepstart:
	SETL r239,63
	PUSHJ r31,.Ldeep
	JMP .Lbad
.Ldeep:
	GET r1,rJ
	SETL r0,123
	CMPU r255,r242,64
	BNZ r255,.Ldeepnext
.globl mmix_jump_growth
mmix_jump_growth:
	SETL r229,77
.Ldeepnext:
	BZ r239,.Ldeepreturn
	SUBU r239,r239,1
.globl mmix_jump_push
mmix_jump_push:
	PUSHJB r31,.Ldeep
	JMP .Lbad
.Ldeepreturn:
	CMPU r255,r242,67
	BNZ r255,.Lbad
	SETL r229,77
	SETL r237,__NR_mmix_rstack_sync
	TRAP 1,0,0
	JMPB mmix_jump_pop
.Lread:
	ADDU r231,r238,96
	SETL r232,0
	SETL r237,__NR_pipe2
	TRAP 1,0,0
	BNZ r231,.Lbad
	LDTU r231,r238,96
	ADDU r232,r238,104
	SETL r233,1
	SETL r237,__NR_read
	TRAP 1,0,0
	JMP .Lbad
.Lsignal:
	SETL r237,__NR_getpid
	TRAP 1,0,0
	SETL r232,10
	SETL r237,__NR_kill
	TRAP 1,0,0
	CMPU r255,r242,55
	BZ r255,.Lsuccess
	CMPU r255,r242,57
	BZ r255,.Ljump
	CMPU r255,r242,4
	BZ r255,.Lsuccess
	CMPU r255,r242,8
	BZ r255,.Lsuccess
	CMPU r255,r242,63
	BZ r255,.Lsuccess
	JMP .Lbad
.Lrestorer:
	JMP .Lenter
	JMP .Lreturn
.Lenter:
	GO r255,r234,0
.Lreturn:
	SETL r237,__NR_rt_sigreturn
	TRAP 1,0,0
	JMP .Lbad
.Lhandler:
	GET r30,rJ
	CMPU r255,r231,12
	BZ r255,.Lhandlerreturn
	CMPU r255,r242,57
	BZ r255,.Lhandlerreturn
	LDOU r0,r238,32
	ADDU r0,r0,1
	STOU r0,r238,32
	CMPU r255,r242,63
	BNZ r255,.Ldepthready
	CMPU r255,r0,2
	BZ r255,.Lnested
.Ldepthready:
	CMPU r255,r0,1
	BNZ r255,.Ljump
	CMPU r255,r242,67
	BZB r255,.Ldeepstart
	CMPU r255,r242,4
	BZ r255,.Lsavehandler
	CMPU r255,r242,8
	BZ r255,.Lsavehandler
	CMPU r255,r242,63
	BZ r255,.Lsavehandler
	CMPU r255,r242,3
	BZ r255,.Lnested
	CMPU r255,r242,7
	BZ r255,.Lchangealt
	JMP .Ljump
.Lsavehandler:
	OR r231,r240,0
	INCL r231,280
	SETL r237,__NR_mmix_rstack_query
	TRAP 1,0,0
	BNZ r231,.Lbad
	seed_locals
	save_environment .Ldone
	CMPU r255,r242,8
	BZ r255,.Lchangealt
	JMP .Lnested
.Lchangealt:
	SETML r255,0x6e
	STOU r255,r238,64
	SETL r255,0
	STTU r255,r238,72
	SETML r255,1
	STOU r255,r238,80
	ADDU r231,r238,64
	SETL r232,0
	SETL r237,__NR_sigaltstack
	TRAP 1,0,0
	BNZ r231,.Lbad
	CMPU r255,r242,8
	BZ r255,.Ljump
.Lnested:
	SETL r237,__NR_getpid
	TRAP 1,0,0
	SETL r232,10
	SETL r237,__NR_kill
	TRAP 1,0,0
	JMP .Lbad
.Lhandlerdone:
	GETA r255,%geta(.Lreturn)
	PUT rJ,r255
	POP 0,0
.Lhandlerreturn:
	PUT rJ,r30
	POP 0,0
.Ljump:
	OR r255,r240,0
	INCL r255,280
	LDOU r255,r255,0
	STOU r255,r241,0
	GETA r255,%geta(.Llanding)
	CMPU r231,r242,60
	BNZ r231,.Llandingaddress
	SETL r255,8192
.Llandingaddress:
	STOU r255,r241,8
	STOU r240,r241,16
	SETL r255,37
	CMPU r231,r242,61
	BNZ r231,.Lresult
	NEGU r255,0,37
.Lresult:
	STOU r255,r241,24
	SETL r255,0
	STOU r255,r241,32
	STOU r255,r241,40
	CMPU r231,r242,5
	BNZ r231,.Lrequest
	SETL r255,1
	STOU r255,r241,32
	SETL r255,0x900
	ORML r255,4
	STOU r255,r241,40
.Lrequest:
	BNZ r244,.Lsubmit
	CMPU r255,r242,10
	BN r255,.Lsubmit
	CMPU r255,r242,40
	BP r255,.Lsubmit
	.irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	SETL r\reg,(2000+\reg)
	.endr
.Lsubmit:
	OR r231,r241,0
	SETL r237,__NR_mmix_rstack_jump
	TRAP 1,0,0
	CMPU r255,r242,44
	BN r255,.Lbad
	CMPU r255,r242,58
	BP r255,.Lbad
	BNN r231,.Lbad
	CMPU r255,r242,55
	BZB r255,.Lhandlerreturn
	JMP .Lsuccess
.Llanding:
	OR r246,r231,0
	OR r247,r232,0
	LDOU r248,r246,24
.Lwalk:
	GET r255,rO
	CMPU r255,r255,r248
	BZ r255,.Linstall
	BN r255,.Lbad
	GETA r255,%geta(.Lwalk)
	PUT rJ,r255
	POP 0,0
.Linstall:
	ADDU r250,r246,32
	ADDU r244,r244,1
	.irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	CMPU r255,r242,66
	BZ r255,2f
	LDOU r\reg,r250,0
2:
	ADDU r250,r250,8
	CMPU r255,r244,1
	BNZ r255,1f
	SETL r237,0xffff
	TRAP 1,0,0
1:
	.endr
	CMPU r255,r244,1
	BNZ r255,.Lfinishinstall
	CMPU r255,r242,68
	BN r255,.Lfinishinstall
	CMPU r255,r242,73
	BP r255,.Lfinishinstall
	SUBU r254,r254,8
	SETL r237,0xffff
	TRAP 1,0,0
	OR r253,r254,0
	TRAP 1,0,0
	SETL r255,19
	PUT rD,r255
	TRAP 1,0,0
	SETL r255,20
	PUT rE,r255
	TRAP 1,0,0
	SETL r255,21
	PUT rH,r255
	TRAP 1,0,0
	GETA r255,%geta(.Lbad)
	PUT rJ,r255
	TRAP 1,0,0
.Lfinishinstall:
	SETL r255,31
	PUT rL,r255
	LDOU r253,r246,0
	LDOU r254,r246,16
	OR r231,r247,0
	LDOU r255,r246,8
	GO r255,r255,0
.Ldone:
	.irp reg,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	SETL r255,(1000+\reg)
	CMPU r255,r\reg,r255
	BNZ r255,.Lbad
	.endr
	SETL r255,37
	CMPU r232,r242,61
	BNZ r232,.Lcheckresult
	NEGU r255,0,37
.Lcheckresult:
	CMP r255,r231,r255
	BNZ r255,.Lbad
	SETL r237,0xfffe
	TRAP 1,0,0
	CMPU r255,r242,4
	BZB r255,.Lhandlerdone
	CMPU r255,r242,8
	BZB r255,.Lhandlerdone
	CMPU r255,r242,63
	BZB r255,.Lhandlerdone
.Lsuccess:
	SETL r231,0
	SETL r237,__NR_exit
	TRAP 1,0,0
.Lbad:
	SETL r231,99
	SETL r237,__NR_exit
	TRAP 1,0,0
	JMP .
.globl mmix_jump_test_end
mmix_jump_test_end:
