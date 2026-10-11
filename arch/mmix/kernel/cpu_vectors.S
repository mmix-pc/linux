/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/cpu_local.h>
#include <generated/asm-offsets.h>

/* rT/rTT select the first/second branch; the remaining record is immutable. */
.section .rodata,"a"
.balign 8
.globl mmix_cpu_vectors
mmix_cpu_vectors:
.set cpu_slot,0
.rept CONFIG_NR_CPUS
	JMP mmix_refill
	JMP mmix_exception_entry
	.quad mmix_cpu_states+cpu_slot*CPU_SIZE+CPU_ENTRY
	.quad mmix_cpu_states+cpu_slot*CPU_SIZE+CPU_MMU
	.quad mmix_cpu_states+cpu_slot*CPU_SIZE+CPU_USER
	.quad mmix_cpu_states+cpu_slot*CPU_SIZE+CPU_IRQ
	.quad mmix_cpu_states+cpu_slot*CPU_SIZE+CPU_SHADOW
	.quad mmix_cpu_states+cpu_slot*CPU_SIZE+CPU_PREV
	.quad mmix_cpu_states+cpu_slot*CPU_SIZE+CPU_CONT
	.quad mmix_cpu_states+cpu_slot*CPU_SIZE
	.quad mmix_cpu_states+cpu_slot*CPU_SIZE+CPU_ID
.set cpu_slot,cpu_slot+1
.endr
