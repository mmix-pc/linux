/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/boot.h>
#include <generated/asm-offsets.h>

/* QEMU Linux handoff: $0 = CPU ID, $1 = physical FDT, rK = 0. */
.section .boot.text,"ax"
.balign 4
.globl _start
_start:
	GETA r255,%geta(mmix_boot_handoff)
	ANDNH r255,0x8000
	STOU r1,r255,BOOT_FDT
	STOU r0,r255,BOOT_CPU
	GET r2,rS
	STOU r2,r255,BOOT_LOADER_RS
	GETA r255,%geta(mmix_head)
	GO r255,r255,0

.section .head.text,"ax"
.balign 4
.globl mmix_head
mmix_head:
	PUT rK,0
	PUT rG,MMIX_BOOT_RG
	GETA r255,%geta(mmix_boot_trap)
	PUT rT,r255
	PUT rTT,r255
	PUT rA,0
	/* Clear BSS without a stack or a C call. Handoff data is in .init.data. */
	GETA r251,%geta(__bss_start)
	GETA r252,%geta(__bss_stop)
	SETL r253,0
1:
	CMPU r255,r251,r252
	BNN r255,2f
	STOU r253,r251,0
	ADDU r251,r251,8
	JMPB 1b
2:
	/* MMIXware section 43: zero locals, 26 globals, 12 specials, rG/rA. */
	GETA r255,%geta(mmix_boot_rstack)
	SETH r254,(MMIX_BOOT_RG << 8)
	SETL r253,BOOT_SEED_TOP
	STOU r254,r255,r253
	ADDU r255,r255,r253
	UNSAVE r255
	GETA r230,%geta(init_task)
	GETA r254,%geta(mmix_boot_stack_end)
	SETL r253,0
	/* Negative direct aliases are safe before software translation exists. */
	SETMH r255,0xe4
	GETA r251,%geta(mmix_kernel_irq_mask)
	STOU r255,r251,0
	PUT rK,r255
	PUSHJ r31,mmix_early_boot
	JMP mmix_boot_trap

/* Fail-stop path: no local registers, calls, SAVE, stack or allocator. */
.section .text,"ax"
.balign 4
.globl mmix_boot_trap
mmix_boot_trap:
	PUT rK,0
	GETA r255,%geta(mmix_boot_fault)
	STOU r254,r255,BOOT_FAULT_SP
	GET r254,rWW
	STOU r254,r255,BOOT_FAULT_PC
	GET r254,rXX
	STOU r254,r255,BOOT_FAULT_XX
	GET r254,rYY
	STOU r254,r255,BOOT_FAULT_YY
	GET r254,rZZ
	STOU r254,r255,BOOT_FAULT_ZZ
	GET r254,rO
	STOU r254,r255,BOOT_FAULT_RO
	GET r254,rS
	STOU r254,r255,BOOT_FAULT_RS
	GET r254,rBB
	STOU r254,r255,BOOT_FAULT_BB
	/* A second failure must not overwrite the first record. */
	GETA r254,%geta(mmix_boot_halt)
	PUT rT,r254
	PUT rTT,r254
	GETA r253,%geta(.Ltrap_message)
	SETH r255,0x8001
	ORML r255,0x1000
3:
	LDBU r251,r253,0
	BZ r251,mmix_boot_halt
	SETL r252,65535
4:
	LDBU r254,r255,5
	AND r254,r254,32
	BNZ r254,5f
	SUBU r252,r252,1
	BNZB r252,4b
	JMP mmix_boot_halt
5:
	SYNC 3
	STBU r251,r255,0
	SYNC 3
	ADDU r253,r253,1
	JMPB 3b
.globl mmix_boot_halt
mmix_boot_halt:
	PUT rK,0
6:
	SWYM 0,0,0
	JMPB 6b
.Ltrap_message:
	.asciz "MMIX: early trap\n"

.section .init.data,"aw"
.balign 8
.globl mmix_boot_handoff
mmix_boot_handoff:
	.space BOOT_HANDOFF_SIZE

.bss
.balign 8192
.globl mmix_boot_stack
mmix_boot_stack:
	.space MMIX_BOOT_STACK_SIZE
.globl mmix_boot_stack_end
mmix_boot_stack_end:
.globl mmix_boot_rstack
mmix_boot_rstack:
	.space MMIX_BOOT_RSTACK_SIZE
.globl mmix_boot_rstack_end
mmix_boot_rstack_end:
.balign 8
.globl mmix_boot_fault
mmix_boot_fault:
	.space BOOT_FAULT_SIZE
