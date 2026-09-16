// SPDX-License-Identifier: GPL-2.0-only
#include <linux/irqflags.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <asm/mmu_context.h>
#include <asm/rstack.h>
#include <asm/tlbflush.h>
#include "mmu.h"

void flush_tlb_all(void)
{
	asm volatile("SYNC 3\n\tSYNC 6" ::: "memory");
}

void flush_tlb_mm(struct mm_struct *mm)
{
	flush_tlb_all();
}

void flush_tlb_page(struct vm_area_struct *vma, unsigned long address)
{
	flush_tlb_all();
}

void flush_tlb_range(struct vm_area_struct *vma, unsigned long start,
		     unsigned long end)
{
	flush_tlb_all();
}

void flush_tlb_kernel_range(unsigned long start, unsigned long end)
{
	flush_tlb_all();
}

void update_mmu_cache_range(struct vm_fault *vmf, struct vm_area_struct *vma,
			   unsigned long address, pte_t *ptep, unsigned int nr)
{
	flush_tlb_all();
}

int init_new_context(struct task_struct *task, struct mm_struct *mm)
{
	mm->context.asid = 0;
	return mmix_rstack_mm_init(mm);
}

void destroy_context(struct mm_struct *mm)
{
	mmix_rstack_mm_destroy(mm);
	flush_tlb_all();
}

void switch_mm(struct mm_struct *prev, struct mm_struct *next,
	       struct task_struct *task)
{
	unsigned long flags;

	local_irq_save(flags);
	WRITE_ONCE(mmix_mmu_state.user_root,
		   next == &init_mm ? NULL : next->pgd);
	flush_tlb_all();
	local_irq_restore(flags);
}

void flush_cache_all(void)
{
	unsigned long flags;

	/* Clean before clearing, with no intervening interrupt cache writes. */
	local_irq_save(flags);
	asm volatile("SYNC 5\n\tSYNC 7" ::: "memory");
	local_irq_restore(flags);
}

void flush_cache_mm(struct mm_struct *mm)
{
	flush_cache_all();
}

void flush_cache_dup_mm(struct mm_struct *mm)
{
	flush_cache_all();
}

void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
		       unsigned long end)
{
	flush_cache_all();
}

void flush_cache_page(struct vm_area_struct *vma, unsigned long addr,
		      unsigned long pfn)
{
	flush_cache_all();
}

void flush_dcache_page(struct page *page)
{
	flush_cache_all();
}

void flush_icache_range(unsigned long start, unsigned long end)
{
	flush_cache_all();
}
