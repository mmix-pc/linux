/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_MMU_INTERNAL_H
#define _MMIX_MMU_INTERNAL_H

#include <asm/pgtable.h>

#define MMIX_INIT_STACK_BASE (VMALLOC_START + PAGE_SIZE)
#define MMIX_INIT_STACK_END (MMIX_INIT_STACK_BASE + THREAD_SIZE)
#define MMIX_INIT_RSTACK_BASE (MMIX_INIT_STACK_END + 2 * PAGE_SIZE)
#define MMIX_INIT_RSTACK_END (MMIX_INIT_RSTACK_BASE + MMIX_RSTACK_SIZE)
#define MMIX_RV_SOFTWARE ((13UL << 40) | 1)

/* CPU-resident state, shared exclusively with that CPU's stackless refill. */
struct mmix_mmu_state {
	unsigned long globals[15]; /* Scratch registers 240-254. */
	unsigned long busy;
	unsigned long r_p; /* Interrupted CSWAP state, preserved across refill RMW. */
	pgd_t *root; /* Local selector for shared kernel tables. */
	pgd_t *user_root; /* Active user root; NULL for init_mm. */
	unsigned long ram_end;
};
void mmix_refill(void);
void __noreturn mmix_enter_kernel(unsigned long seed);
#endif
