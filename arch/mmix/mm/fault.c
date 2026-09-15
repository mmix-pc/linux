// SPDX-License-Identifier: GPL-2.0-only
#include <linux/interrupt.h>
#include <linux/extable.h>
#include <linux/mm.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>
#include <asm/tlbflush.h>
#include "fault.h"

static int complete_translation(struct mm_struct *mm, struct pt_regs *regs)
{
	unsigned long address = regs->r_yy;
	pgd_t *pgd = pgd_offset(mm, address);
	p4d_t *p4d = p4d_offset(pgd, address);
	pud_t *pud = pud_offset(p4d, address);
	pmd_t *pmd;
	pte_t *ptep, pte;
	/* Leaf page-table lock acquired by pte_offset_map_lock(). */
	spinlock_t *ptl;

	if (regs->r_xx >> 32 != 0x03000000UL)
		return 0;
	if (pud_none(*pud) || pud_bad(*pud))
		return SIGSEGV;
	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		return SIGSEGV;
	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	if (!ptep)
		return SIGSEGV;
	pte = ptep_get(ptep);
	if (pte_present(pte)) {
		pte = pte_mkyoung(pte);
		if (pte_val(pte) & _PAGE_WRITE)
			pte = pte_mkdirty(pte);
		set_pte(ptep, pte);
		regs->r_zz = pte_val(pte) & (PTE_PHYS_MASK | 7);
	}
	pte_unmap_unlock(ptep, ptl);
	asm volatile("SYNC 3" ::: "memory");
	return pte_present(pte) ? 0 : SIGSEGV;
}

int mmix_refresh_translation(struct pt_regs *regs)
{
	unsigned long flags;
	int signal;

	if (regs->r_xx >> 32 != 0x03000000UL)
		return 0;
	if (!current->mm || !access_ok((void __user *)regs->r_yy, 1))
		return SIGSEGV;
	local_irq_save(flags);
	local_irq_enable();
	mmap_read_lock(current->mm);
	local_irq_disable();
	signal = complete_translation(current->mm, regs);
	mmap_read_unlock(current->mm);
	local_irq_restore(flags);
	return signal;
}

int mmix_handle_page_fault(struct pt_regs *regs)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long address = regs->r_yy;
	unsigned long requests = regs->r_q & (0xffUL << 32);
	unsigned int flags = FAULT_FLAG_DEFAULT;
	unsigned int opcode = (regs->r_xx >> 24) & 0xff;
	unsigned long access = VM_READ;
	vm_fault_t fault;
	int signal = SIGSEGV;
	bool user = user_mode(regs);

	if (requests & (1UL << 36))
		return SIGSEGV;
	if (requests & (1UL << 37)) {
		access = VM_EXEC;
	} else if (requests & (1UL << 38)) {
		access = VM_WRITE;
	} else if (!(requests & (1UL << 39))) {
		if (regs->r_xx >> 32 != 0x03000000UL)
			return SIGILL;
		if ((u32)regs->r_xx == 0xfd000000)
			access = VM_EXEC;
		else if (opcode >= 0xa0 && opcode <= 0xbf)
			access = VM_WRITE;
	}
	/* Protection traps do not promise a fault address in rYY (MMIX, section 36). */
	if (regs->r_xx >> 32 != 0x03000000UL) {
		if (access == VM_EXEC) {
			address = regs->pc;
		} else if ((opcode >= 0x80 && opcode <= 0xbf) &&
			   (access != VM_WRITE || opcode >= 0xa0 || (opcode & ~1U) == 0x94)) {
			unsigned int y = (regs->r_xx >> 8) & 0xff;
			unsigned int z = regs->r_xx & 0xff;

			address = regs->regs[y] + ((opcode & 1) ? z : regs->regs[z]);
		} else if (access == VM_WRITE) {
			/* An implicit spill writes the first not-yet-backed ring octa. */
			address = regs->r_s;
		} else {
			return signal;
		}
	}
	if (!access_ok((void __user *)address, 1) || !mm || faulthandler_disabled())
		return signal;
	/* A kernel access may sleep only if its interrupted IRQ state allowed it. */
	if (!user && (!(regs->mask & MMIX_IRQ_CONTROLLER) || !search_exception_tables(regs->pc)))
		return signal;
	if (user)
		flags |= FAULT_FLAG_USER;
	if (access == VM_WRITE)
		flags |= FAULT_FLAG_WRITE;
	if (access == VM_EXEC)
		flags |= FAULT_FLAG_INSTRUCTION;
	local_irq_enable();
retry:
	vma = lock_mm_and_find_vma(mm, address, regs);
	if (!vma)
		goto out;
	if (!(vma->vm_flags & access)) {
		mmap_read_unlock(mm);
		goto out;
	}
	fault = handle_mm_fault(vma, address, flags, regs);
	if ((fault & VM_FAULT_RETRY) && fatal_signal_pending(current)) {
		signal = SIGKILL;
		goto out;
	}
	if (fault & VM_FAULT_COMPLETED) {
		signal = 0;
		goto out;
	}
	if (fault & VM_FAULT_RETRY) {
		flags |= FAULT_FLAG_TRIED;
		goto retry;
	}
	mmap_read_unlock(mm);
	if (fault & VM_FAULT_OOM) {
		if (user) {
			pagefault_out_of_memory();
			if (!fatal_signal_pending(current))
				goto retry;
		}
		signal = SIGKILL;
	} else if (fault & (VM_FAULT_SIGBUS | VM_FAULT_HWPOISON | VM_FAULT_HWPOISON_LARGE)) {
		signal = SIGBUS;
	} else if (!(fault & VM_FAULT_ERROR)) {
		flush_tlb_all();
		signal = 0;
	}
out:
	if (!signal) {
		mmap_read_lock(mm);
		local_irq_disable();
		signal = complete_translation(mm, regs);
		mmap_read_unlock(mm);
	}
	local_irq_disable();
	return signal;
}
