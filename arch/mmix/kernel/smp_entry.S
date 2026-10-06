/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/boot.h>
#include <generated/asm-offsets.h>
#include "smp_boot.h"

.text
.balign 4
.globl mmix_secondary_start
mmix_secondary_start:
	/* Only private loader registers are touched before the slot is claimed. */
	PUT rK,0
	CMPU r255,r0,MMIX_BOOT_CPU_SLOTS
	BNN r255,.Lsecondary_halt
	MULU r255,r0,SECONDARY_SIZE
	GETA r250,%geta(mmix_secondary_boot)
	ADDU r250,r250,r255
	SETL r255,0
	PUT rP,r255
	SETL r255,1
	ADDU r251,r250,SECONDARY_CLAIM
	CSWAP r255,r251,0
	BZ r255,.Lsecondary_duplicate
	STOU r1,r250,SECONDARY_FDT
	GET r255,rS
	STOU r255,r250,SECONDARY_LOADER_RS
	PUT rG,MMIX_BOOT_RG
	GETA r255,%geta(.Lsecondary_early_fault)
	PUT rT,r255
	PUT rTT,r255
	SYNC 3
	SETL r255,MMIX_SECONDARY_ENTERED
	STOU r255,r250,SECONDARY_STATUS
	SYNC 3
.Lsecondary_wait:
	GETA r255,%geta(mmix_secondary_abort)
	LDOU r255,r255,0
	BNZ r255,.Lsecondary_stop
	LDOU r255,r250,SECONDARY_RELEASE
	BZB r255,.Lsecondary_wait
	SYNC 3
#ifdef CONFIG_SMP
	LDOU r255,r250,SECONDARY_VECTOR
	PUT rT,r255
	GETA r255,%geta(mmix_secondary_fault)
	PUT rTT,r255
	LDOU r255,r250,SECONDARY_RC
	PUT rC,r255
	SETL r255,1
	ORMH r255,0x0d00
	PUT rV,r255
	SYNC 6
	LDOU r255,r250,SECONDARY_SEED
	UNSAVE r255
	PUSHJ r31,mmix_secondary_park
#endif
	JMP .Lsecondary_early_fault
.Lsecondary_duplicate:
	SETL r255,MMIX_SECONDARY_FAILED
	STOU r255,r250,SECONDARY_STATUS
	GETA r250,%geta(mmix_secondary_abort)
	SETL r255,1
	SYNC 3
	STOU r255,r250,0
	JMP .Lsecondary_halt
.Lsecondary_early_fault:
	SETL r255,MMIX_SECONDARY_FAILED
	STOU r255,r250,SECONDARY_STATUS
	SYNC 3
	JMP .Lsecondary_halt
.Lsecondary_stop:
	SETL r255,MMIX_SECONDARY_STOPPED
	STOU r255,r250,SECONDARY_STATUS
	SYNC 3
.Lsecondary_halt:
	PUT rK,0
	SWYM 0,0,0
	JMPB .Lsecondary_halt

.globl mmix_secondary_fault
mmix_secondary_fault:
	PUT rK,0
	MMIX_CPU_PTR r255,MMIX_CPU_VECTOR_ID
	LDOU r255,r255,0
	MULU r255,r255,SECONDARY_SIZE
	GETA r250,%geta(mmix_secondary_boot)
	ADDU r250,r250,r255
	JMPB .Lsecondary_early_fault

#ifdef CONFIG_SMP
.globl mmix_enter_secondary
mmix_enter_secondary:
	PUT rK,0
	OR r255,r231,0
	UNSAVE r255
	GET r255,rT
	ADDU r255,r255,4
	PUT rTT,r255
	PUSHJ r31,mmix_secondary_main
	GETA r255,%geta(mmix_secondary_fault)
	GO r255,r255,0
#endif

/* QEMU loads this initialized section before starting any CPU. */
.data
.balign 64
.globl mmix_secondary_boot
mmix_secondary_boot:
.set cpu_slot,0
.rept MMIX_BOOT_CPU_SLOTS
	.space SECONDARY_CPU
	.quad cpu_slot
	.space SECONDARY_SIZE-SECONDARY_CPU-8
.set cpu_slot,cpu_slot+1
.endr
.globl mmix_secondary_abort
mmix_secondary_abort:
	.quad 0
