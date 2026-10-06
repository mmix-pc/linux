/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/cpu_local.h>
#include <generated/asm-offsets.h>

.text
.balign 4
.globl mmix_boot_ipi_entry
mmix_boot_ipi_entry:
	/* The parked CPU has private resident stacks but no current task. */
	PUT rK,0
	MMIX_CPU_PTR r255,MMIX_CPU_VECTOR_STATE
	STOU r253,r255,BOOT_IPI_R253
	STOU r254,r255,BOOT_IPI_SP
	GET r253,rWW
	STOU r253,r255,BOOT_IPI_WW
	GET r253,rXX
	STOU r253,r255,BOOT_IPI_XX
	GET r253,rYY
	STOU r253,r255,BOOT_IPI_YY
	GET r253,rZZ
	STOU r253,r255,BOOT_IPI_ZZ
	GET r253,rBB
	STOU r253,r255,BOOT_IPI_BB
	GET r253,rQ
	SETMH r254,0xe4
	AND r254,r253,r254
	BNZ r254,.Lboot_ipi_fault
	SETL r254,512
	AND r253,r253,r254
	BZ r253,.Lboot_ipi_fault
	/* SAVE preserves locals/globals/specials; SP must remain usable by C. */
	LDOU r254,r255,BOOT_IPI_SP
	SAVE r253
	MMIX_CPU_PTR r255,MMIX_CPU_VECTOR_STATE
	STOU r253,r255,BOOT_IPI_SAVE
	PUSHJ r31,mmix_ipi_interrupt
	PUT rK,0
	MMIX_CPU_PTR r255,MMIX_CPU_VECTOR_STATE
	LDOU r253,r255,BOOT_IPI_SAVE
	UNSAVE r253
	MMIX_CPU_PTR r255,MMIX_CPU_VECTOR_STATE
	LDOU r253,r255,BOOT_IPI_WW
	PUT rWW,r253
	LDOU r253,r255,BOOT_IPI_XX
	PUT rXX,r253
	LDOU r253,r255,BOOT_IPI_YY
	PUT rYY,r253
	LDOU r253,r255,BOOT_IPI_ZZ
	PUT rZZ,r253
	LDOU r253,r255,BOOT_IPI_BB
	PUT rBB,r253
	LDOU r254,r255,BOOT_IPI_SP
	LDOU r253,r255,BOOT_IPI_R253
	MMIX_CPU_PTR r255,MMIX_CPU_VECTOR_IRQ
	LDOU r255,r255,0
	RESUME 1
.Lboot_ipi_fault:
	GETA r255,%geta(mmix_secondary_fault)
	GO r255,r255,0
