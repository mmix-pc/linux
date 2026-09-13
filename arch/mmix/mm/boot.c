// SPDX-License-Identifier: GPL-2.0-only
#include <linux/init_task.h>
#include <linux/sched.h>
#include <asm/boot.h>
#include <asm/barrier.h>
#include <asm/tlbflush.h>
#include "mmu.h"
#include "../kernel/entry.h"

pgd_t swapper_pg_dir[PTRS_PER_PGD] __aligned(PAGE_SIZE);
static pmd_t boot_pmd[PTRS_PER_PMD] __aligned(PAGE_SIZE);
static pte_t boot_pte[PTRS_PER_PTE] __aligned(PAGE_SIZE);
static unsigned long init_rstack[MMIX_RSTACK_SIZE / sizeof(unsigned long)]
	__aligned(PAGE_SIZE);
struct mmix_mmu_state mmix_mmu_state;

void __init __noreturn mmix_boot_mmu(void)
{
	const struct mmix_boot_info *boot = mmix_get_boot_info();
	unsigned long address, offset;
	unsigned long *seed = init_rstack;

	set_pgd(&swapper_pg_dir[0], __pgd(__pa(boot_pmd) | _PAGE_PRESENT));
	set_pmd(&boot_pmd[0], __pmd(__pa(boot_pte) | _PAGE_PRESENT));
	for (address = MMIX_INIT_STACK_BASE, offset = 0;
	     address < MMIX_INIT_STACK_END; address += PAGE_SIZE, offset += PAGE_SIZE)
		set_pte(&boot_pte[(address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1)],
			pfn_pte((__pa(init_stack) + offset) >> PAGE_SHIFT, PAGE_KERNEL));
	for (address = MMIX_INIT_RSTACK_BASE, offset = 0;
	     address < MMIX_INIT_RSTACK_END; address += PAGE_SIZE, offset += PAGE_SIZE)
		set_pte(&boot_pte[(address >> PAGE_SHIFT) & (PTRS_PER_PTE - 1)],
			pfn_pte((__pa(init_rstack) + offset) >> PAGE_SHIFT, PAGE_KERNEL));

	/* Zero-local SAVE image: count, 26 globals, 12 specials, packed rG/rA. */
	seed[1] = (unsigned long)&init_task;
	seed[1 + 254 - MMIX_BOOT_RG] = MMIX_INIT_STACK_END;
	seed[1 + 256 - MMIX_BOOT_RG + 12] = (unsigned long)MMIX_BOOT_RG << 56;
	init_task.stack = (void *)MMIX_INIT_STACK_BASE;
	init_task.thread.rstack_base = MMIX_INIT_RSTACK_BASE;
	init_task.thread.rstack_limit = MMIX_INIT_RSTACK_END;
	mmix_mmu_state.root = swapper_pg_dir;
	mmix_mmu_state.ram_end = boot->ram_size;
	/* Publish the tables and refill state before enabling translation. */
	mb();
	asm volatile("PUT rT,%0\n\tPUT rV,%1\n\tPUT rTT,%2\n\tPUT rC,%3\n\tSYNC 6"
		     : : "r" (mmix_refill), "r" (MMIX_RV_SOFTWARE),
		     "r" (mmix_exception_entry),
		     "r" (__pa(mmix_continuation_page) | 6) : "memory");
	mmix_enter_kernel(MMIX_INIT_RSTACK_BASE +
			  (1 + 256 - MMIX_BOOT_RG + 12) * sizeof(unsigned long));
}
