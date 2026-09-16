/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/unistd.h>
.section .rodata,"a"
.balign 4
.globl mmix_signal_test_start
mmix_signal_test_start:
	CMPU r255,r242,27
	BNZ r255,.Lnormalstart
	SETL r237,__NR_rt_sigreturn
	TRAP 1,0,0
	JMP .Lbad
.Lnormalstart:
	SETML r238,0x60
	GETA r231,%geta(.Lhandler)
	STOU r231,r238,0
	SETML r231,0x400
	CMPU r255,r242,1
	BZ r255,.Lflags
	ORL r231,4
.Lflags:
	CMPU r255,r242,2
	BZ r255,.Lnodefer
	CMPU r255,r242,4
	BZ r255,.Lnodefer
	CMPU r255,r242,71
	BZ r255,.Lnodefer
	CMPU r255,r242,31
	BZ r255,.Lnodefer
	CMPU r255,r242,10
	BNZ r255,.Laltflag
.Lnodefer:
	ORML r231,0x4000
.Laltflag:
	CMPU r255,r242,71
	BZ r255,.Laltflagyes
	CMPU r255,r242,3
	BZ r255,.Laltflagyes
	CMPU r255,r242,4
	BNZ r255,.Laction
.Laltflagyes:
	ORML r231,0x800
.Laction:
	CMPU r255,r242,26
	BNZ r255,.Lactionready
	ORML r231,0x1000
.Lactionready:
	STOU r231,r238,8
	GETA r231,%geta(.Lrestorer)
	STOU r231,r238,16
	SETL r231,0
	STOU r231,r238,24
	SETL r231,10
	CMPU r255,r242,70
	BNZ r255,.Lsigactionnumber
	SETL r231,11
.Lsigactionnumber:
	OR r232,r238,0
	SETL r233,0
	SETL r234,8
	SETL r237,__NR_rt_sigaction
	TRAP 1,0,0
	BNZ r231,.Lbad
	CMPU r255,r242,71
	BZ r255,.Laltsetup
	CMPU r255,r242,3
	BZ r255,.Laltsetup
	CMPU r255,r242,4
	BNZ r255,.Lready
.Laltsetup:
	SETML r231,0x6f
	STOU r231,r238,64
	SETL r231,0
	CMPU r255,r242,4
	BNZ r255,.Laltmode
	SETML r231,0x8000
.Laltmode:
	STTU r231,r238,72
	SETML r231,1
	CMPU r255,r242,71
	BNZ r255,.Laltsize
	SETL r231,0x8000
.Laltsize:
	STOU r231,r238,80
	ADDU r231,r238,64
	SETL r232,0
	SETL r237,__NR_sigaltstack
	TRAP 1,0,0
	BNZ r231,.Lbad
.Lready:
	CMPU r255,r242,70
	BZ r255,.Loverflow
	CMPU r255,r242,69
	BN r255,.Lreadyregular
	CMPU r255,r242,71
	BNN r255,.Lsimple
	ADDU r254,r238,8
	JMP .Lsimple
.Lreadyregular:
	CMPU r255,r242,32
	BNN r255,.Lmixed
	CMPU r255,r242,31
	BZ r255,.Lsimple
	CMPU r255,r242,25
	BNN r255,.Lpipe
	CMPU r255,r242,22
	BN r255,.Lsimple
	SETL r0,123
	SETL r230,456
	CMPU r255,r242,24
	BNZ r255,.Ldeepgrowth
	SETL r239,64
	PUSHJ r31,.Lpushrecursive
	JMP .Ldeepdone
.Ldeepgrowth:
	SETL r32,64
	PUSHJ r31,.Lrecursive
.Ldeepdone:
	BNZ r231,.Lbad
	JMP .Laftertrap
.Lpipe:
	ADDU r231,r238,192
	SETL r232,0
	SETL r237,__NR_pipe2
	TRAP 1,0,0
	BNZ r231,.Lbad
	LDTU r231,r238,192
	ADDU r232,r238,208
	SETL r233,1
	SETL r237,__NR_read
	SETL r0,123
	SETL r230,456
	TRAP 1,0,0
	CMPU r255,r242,25
	BNZ r255,.Lreadrestarted
	NEGU r255,0,r231
	CMPU r255,r255,4
	BNZ r255,.Lbad
	JMP .Lreadchecked
.Lreadrestarted:
	CMPU r255,r231,1
	BNZ r255,.Lbad
	LDBU r255,r238,208
	CMPU r255,r255,42
	BNZ r255,.Lbad
.Lreadchecked:
	SETL r231,0
	JMP .Laftertrap
.Lsimple:
	SETL r237,__NR_getpid
	TRAP 1,0,0
	SETL r232,10
	SETL r237,__NR_kill
	CMPU r255,r242,12
	BNZ r255,.Lservice
	SETL r237,__NR_mmix_rstack_sync
.Lservice:
	SETL r0,123
	SETL r230,456
	TRAP 1,0,0
	CMPU r255,r243,75
	BZ r255,.Lbad
.Laftertrap:
	BNZ r231,.Lbad
	SETL r232,123
	CMPU r255,r242,5
	BNZ r255,.Llocalcheck
	SETL r232,321
.Llocalcheck:
	CMPU r231,r0,r232
	BNZ r231,.Lbad
	SETL r232,456
	CMPU r231,r230,r232
	BNZ r231,.Lbad
	LDOU r231,r238,32
	SETL r232,1
	CMPU r255,r242,2
	BZ r255,.Lthree
	CMPU r255,r242,4
	BNZ r255,.Lcountcheck
.Lthree:
	SETL r232,3
.Lcountcheck:
	CMPU r255,r242,31
	BNZ r255,.Lcountready
	SETL r232,2
.Lcountready:
	CMPU r231,r231,r232
	BNZ r231,.Lbad
	CMPU r255,r242,9
	BNZ r255,.Laltcheck
	ADDU r231,r238,96
	SETL r237,__NR_mmix_rstack_query
	TRAP 1,0,0
	BNZ r231,.Lbad
	LDOU r231,r238,104
	SETL r232,0x800
	CMPU r231,r231,r232
	BNZ r231,.Lbad
.Laltcheck:
	CMPU r255,r243,76
	BZ r255,.Lcheckconfig
	CMPU r255,r242,3
	BZ r255,.Lcheckconfig
	CMPU r255,r242,4
	BNZ r255,.Lsuccess
.Lcheckconfig:
	SETL r231,0
	ADDU r232,r238,128
	SETL r237,__NR_sigaltstack
	TRAP 1,0,0
	BNZ r231,.Lbad
	LDOU r231,r238,128
	SETML r232,0x6f
	CMPU r231,r231,r232
	BNZ r231,.Lbad
	LDTU r231,r238,136
	SETL r232,0
	CMPU r255,r242,4
	BNZ r255,.Lflagcheck
	SETML r232,0x8000
.Lflagcheck:
	CMPU r231,r231,r232
	BNZ r231,.Lbad
.Lsuccess:
	SETL r231,0
	SETL r237,__NR_exit
	TRAP 1,0,0
.Lbad:
	SETL r231,99
	SETL r237,__NR_exit
	TRAP 1,0,0
	JMP .
.Lrestorer:
	JMP .Lenter
	JMP .Lreturn
.Lenter:
	GO r255,r234,0
.Lreturn:
	SETL r237,__NR_rt_sigreturn
	TRAP 1,0,0
	JMPB .Lbad
.Lhandler:
	CMPU r255,r242,70
	BNZ r255,.Lregularhandler
	CMPU r255,r231,11
	BNZB r255,.Lbad
	SETL r231,0
	SETL r237,__NR_exit
	TRAP 1,0,0
.Lregularhandler:
	LDTU r255,r232,0
	CMPU r255,r255,r231
	BNZB r255,.Lbad
	CMPU r255,r231,10
	BNZB r255,.Lbad
	CMPU r255,r232,r254
	BNZB r255,.Lbad
	ADDU r255,r254,128
	CMPU r255,r233,r255
	BNZB r255,.Lbad
	LDOU r0,r238,32
	ADDU r0,r0,1
	STOU r0,r238,32
	CMPU r255,r242,5
	BNZ r255,.Lbadflags
	SETL r255,321
	STOU r255,r233,168
.Lbadflags:
	CMPU r255,r242,6
	BNZ r255,.Lbadowner
	SETL r255,1
	STOU r255,r233,0
.Lbadowner:
	CMPU r255,r242,7
	BNZ r255,.Lbadframe
	SETL r255,2240
	STOU r0,r233,r255
.Lbadframe:
	CMPU r255,r242,8
	BNZ r255,.Lnest
	ADDU r254,r254,8
.Lnest:
	CMPU r255,r242,71
	BZ r255,.Lsend
	CMPU r255,r242,9
	BNZ r255,.Leditother
	SETL r255,0x800
	STOU r255,r233,40
.Leditother:
	CMPU r255,r243,75
	BNZ r255,.Leditstack
	GETA r1,%geta(.Laftertrap)
	SETL r255,2216
	STOU r1,r233,r255
.Leditstack:
	CMPU r255,r243,76
	BNZ r255,.Leditready
	SETML r255,0x6f
	STOU r255,r233,16
	SETL r255,0
	STOU r255,r233,24
	SETML r255,1
	STOU r255,r233,32
.Leditready:
	CMPU r255,r243,28
	BN r255,.Lreplayeditdone
	CMPU r255,r243,30
	BP r255,.Lreplayeditdone
	SETL r255,999
	STOU r255,r233,168
.Lreplayeditdone:
	CMPU r255,r242,21
	BNZ r255,.Laccessible
	SETL r254,8
.Laccessible:
	CMPU r255,r242,20
	BNZ r255,.Ledit20next
	SETL r255,2008
	SETL r1,1
	STOU r1,r233,r255
.Ledit20next:
	CMPU r255,r242,19
	BNZ r255,.Ledit19next
	SETL r255,2200
	SETL r1,1
	STOU r1,r233,r255
.Ledit19next:
	CMPU r255,r242,18
	BNZ r255,.Ledit18next
	SETL r255,2216
	SETL r1,1
	STOU r1,r233,r255
.Ledit18next:
	CMPU r255,r242,17
	BNZ r255,.Ledit17next
	SETL r255,10560
	SETL r1,1
	STOU r1,r233,r255
.Ledit17next:
	CMPU r255,r242,16
	BNZ r255,.Ledit16next
	SETL r255,2352
	SETL r1,1
	STOU r1,r233,r255
.Ledit16next:
	CMPU r255,r242,15
	BNZ r255,.Ledit15next
	SETL r255,2232
	SETL r1,2
	STOU r1,r233,r255
.Ledit15next:
	CMPU r255,r242,14
	BNZ r255,.Ledit14next
	SETL r255,2224
	SETL r1,1
	STOU r1,r233,r255
.Ledit14next:
	CMPU r255,r242,13
	BNZ r255,.Ledit13next
	SETL r255,2248
	SETML r1,4
	STOU r1,r233,r255
.Ledit13next:
	CMPU r255,r242,2
	BZ r255,.Lnested
	CMPU r255,r242,4
	BZ r255,.Lnested
	CMPU r255,r242,10
	BNZ r255,.Lhandlerdone
.Lnested:
	CMPU r255,r242,10
	BZ r255,.Lsend
	CMPU r255,r0,3
	BNN r255,.Lhandlerdone
.Lsend:
	SETL r237,__NR_getpid
	TRAP 1,0,0
	SETL r232,10
	SETL r237,__NR_kill
	TRAP 1,0,0
	BNZB r231,.Lbad
.Lhandlerdone:
	CMPU r255,r243,77
	BNZ r255,.Lhandlerconfig
	SETL r231,17
	SETL r232,0
	SETL r237,__NR_clone
	TRAP 1,0,0
	BZ r231,.Lhandlerpop
	BNB r231,.Lbad
	ADDU r232,r238,224
	SETL r233,0
	SETL r234,0
	SETL r237,__NR_wait4
	TRAP 1,0,0
	BNB r231,.Lbad
	LDTU r255,r238,224
	BNZB r255,.Lbad
.Lhandlerconfig:
	CMPU r255,r242,4
	BNZ r255,.Lhandlerdeep
	CMPU r255,r0,3
	BNZ r255,.Lhandlerdeep
	SETML r255,0x6e
	STOU r255,r238,160
	SETL r255,0
	STOU r255,r238,168
	SETML r255,1
	STOU r255,r238,176
	ADDU r231,r238,160
	SETL r232,0
	SETL r237,__NR_sigaltstack
	TRAP 1,0,0
	BNZB r231,.Lbad
.Lhandlerdeep:
	CMPU r255,r242,31
	BNZ r255,.Lhandlerwrite
	CMPU r255,r0,1
	BNZ r255,.Lhandlerpop
	GET r1,rJ
	SETL r32,63
	PUSHJ r31,.Lrecursive
	PUT rJ,r1
	JMP .Lhandlerpop
.Lhandlerwrite:
	CMPU r255,r242,25
	BZ r255,.Lwritebyte
	CMPU r255,r242,26
	BNZ r255,.Lhandlerpop
.Lwritebyte:
	LDTU r231,r238,196
	SETL r255,42
	STBU r255,r238,200
	ADDU r232,r238,200
	SETL r233,1
	SETL r237,__NR_write
	TRAP 1,0,0
	CMPU r255,r231,1
	BNZB r255,.Lbad
.Lhandlerpop:
	POP 0,0
.Lrecursive:
	GET r1,rJ
	OR r2,r0,0
	.irp reg, 3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	SETL r\reg,(1000+\reg)
	.endr
	SETL r0,123
.globl mmix_signal_growth
mmix_signal_growth:
	SETL r229,77
	BZ r2,.Lleaf
	SUBU r32,r2,1
	PUSHJB r31,.Lrecursive
	BNZB r231,.Lbad
.Lleaf:
	CMPU r255,r242,31
	BZ r255,.Lpopsync
	CMPU r255,r242,22
	BNZ r255,.Lpopcheck
.Lpopsync:
	CMPU r255,r243,74
	BNZ r255,.Lpopsize
	SETL r255,31
	PUT rL,r255
.Lpopsize:
	SETL r237,__NR_mmix_rstack_sync
	TRAP 1,0,0
.Lpopcheck:
	.irp reg, 3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	SETL r255,(1000+\reg)
	CMPU r255,r\reg,r255
	BNZB r255,.Lbad
	.endr
	CMPU r231,r0,123
	BNZB r231,.Lbad
	SETL r231,0
	PUT rJ,r1
.globl mmix_signal_pop
mmix_signal_pop:
	POP 0,0
.Loverflow:
	/* Exceed backing plus even the largest admitted 1024-octa ring. */
	SETL r239,4160
	PUSHJ r31,.Lpushrecursive
	JMPB .Lbad
.Lpushrecursive:
	GET r1,rJ
	SETL r0,123
	BZ r239,.Lpushreturn
	SUBU r239,r239,1
.globl mmix_signal_push
mmix_signal_push:
	PUSHJB r31,.Lpushrecursive
.Lpushreturn:
	CMPU r231,r0,123
	BNZB r231,.Lbad
	PUT rJ,r1
	SETL r231,0
	POP 0,0
.Lmixed:
	SETL r251,4096
	ADDU r251,r238,r251
	SETL r230,456
	SETL r237,0xffff
	.irp reg, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	LDOU r\reg,r251,(\reg*8)
	TRAP 1,0,0
	.endr
	SUBU r254,r254,248
	TRAP 1,0,0
	OR r253,r254,0
	TRAP 1,0,0
	PUT rD,r0
	TRAP 1,0,0
	PUT rE,r1
	TRAP 1,0,0
	PUT rH,r2
	TRAP 1,0,0
	PUT rJ,r3
	TRAP 1,0,0
	.irp reg, 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30
	SETL r255,(1000+\reg)
	CMPU r255,r\reg,r255
	BNZB r255,.Lbad
	.endr
	LDOU r255,r238,32
	CMPU r255,r255,1
	BNZB r255,.Lbad
	SETL r231,0
	SETL r237,__NR_exit
	TRAP 1,0,0
	JMPB .Lbad
.globl mmix_signal_test_end
mmix_signal_test_end:
