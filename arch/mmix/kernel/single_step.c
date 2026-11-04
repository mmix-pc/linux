// SPDX-License-Identifier: GPL-2.0-only
#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/uaccess.h>
#include "user_entry.h"

/* Decode a rejected public RESUME without changing its saved operands. */
int mmix_resume_sigill(u32 insn, const struct user_regs_struct *user)
{
	unsigned long exec = user->r_x, where = user->r_w;
	unsigned int rop = exec >> 56, op = (u32)exec >> 24;
	unsigned int x = (exec >> 16) & 255;
	bool eligible = (op <= 0x3f) ||
			(op >= 0x60 && op <= 0x7f) || (op >= 0xc0 && op <= 0xef);
	int code = 0;

	/* MMIXware 38: distinguish the rejected operation from its operands. */
	if (insn == 0xf9000001) {
		code = ILL_PRVOPC;
	} else if (insn != 0xf9000000) {
		code = ILL_ILLOPN;
	} else if ((long)where < 0) {
		code = ILL_ILLADR;
	} else if (!(exec >> 63)) {
		/* Translation ropcode 3 is not an operand of public RESUME 0. */
		if (rop > 2)
			code = ILL_ILLOPN;
		else if ((rop == 0 && op == 0xf9) || (rop == 1 && !eligible))
			code = ILL_ILLOPC;
		else if ((rop == 1 || rop == 2) && x >= user->r_l && x < user->r_g)
			code = ILL_ILLOPN;
	}
	return code;
}

/* Translate a validated public RESUME into a private continuation. */
int mmix_step_resume(struct mmix_user_entry *entry, u32 insn)
{
	const struct user_regs_struct *user = &mmix_user_rstack_state(entry->stack)->regs;
	unsigned long exec = user->r_x, where = user->r_w;
	unsigned int rop = exec >> 56;
	int code = mmix_resume_sigill(insn, user);

	if (code) {
		entry->step_state = MMIX_STEP_DISARMED;
		local_irq_enable();
		force_sig_fault(SIGILL, code, (void __user *)entry->regs.pc);
		local_irq_disable();
		return 1;
	}
	entry->regs.r_ww = where;
	entry->regs.r_xx = exec >> 63 ? 1UL << 63 :
		((unsigned long)rop << 56) | (u32)exec |
		(rop == 2 ? exec & (0xffUL << 40) : 0);
	entry->regs.r_yy = user->r_y;
	entry->regs.r_zz = user->r_z;
	entry->step_state = MMIX_STEP_IN_FLIGHT;
	return 0;
}

/* Fetch one instruction under the executable VMA and leaf mapping locks. */
int mmix_step_fetch(struct mmix_user_entry *entry, u32 *insn)
{
	struct mm_struct *mm = current->mm;
	unsigned long pc = entry->regs.pc, address = pc & ~3UL;
	unsigned int flags = FAULT_FLAG_DEFAULT | FAULT_FLAG_USER | FAULT_FLAG_INSTRUCTION;
	struct vm_area_struct *vma;
	struct page *page;
	vm_fault_t fault;
	void *mapped;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *ptep, pte;
	spinlock_t *ptl; /* Serializes the instruction read against PTE replacement. */
	int signal = SIGSEGV, code = SEGV_MAPERR, error = 1;

	local_irq_enable();
	if (!access_ok((void __user *)address, sizeof(*insn)))
		goto failed;
retry:
	if (fatal_signal_pending(current))
		goto out;
	vma = lock_mm_and_find_vma(mm, pc, &entry->regs);
	if (!vma) {
		code = SEGV_MAPERR;
		goto failed;
	}
	if (!(vma->vm_flags & VM_EXEC)) {
		code = SEGV_ACCERR;
		mmap_read_unlock(mm);
		goto failed;
	}
	fault = handle_mm_fault(vma, pc, flags, &entry->regs);
	if (fault & VM_FAULT_COMPLETED)
		goto retry;
	if (fault & VM_FAULT_RETRY) {
		flags |= FAULT_FLAG_TRIED;
		goto retry;
	}
	if (fault & VM_FAULT_ERROR) {
		mmap_read_unlock(mm);
		if (fault & VM_FAULT_OOM) {
			pagefault_out_of_memory();
			goto retry;
		}
		if (fault & (VM_FAULT_SIGBUS | VM_FAULT_HWPOISON | VM_FAULT_HWPOISON_LARGE)) {
			signal = SIGBUS;
			code = BUS_ADRERR;
		}
		goto failed;
	}
	/* Fetch linearizes while both the executable VMA and leaf are stable. */
	pgd = pgd_offset(mm, address);
	p4d = p4d_offset(pgd, address);
	pud = pud_offset(p4d, address);
	if (pud_none(*pud) || pud_bad(*pud))
		goto retry_mapping;
	pmd = pmd_offset(pud, address);
	if (pmd_none(*pmd) || pmd_bad(*pmd))
		goto retry_mapping;
	ptep = pte_offset_map_lock(mm, pmd, address, &ptl);
	if (!ptep)
		goto retry_mapping;
	pte = ptep_get(ptep);
	if (!pte_present(pte) || !(pte_val(pte) & _PAGE_EXEC)) {
		pte_unmap_unlock(ptep, ptl);
		goto retry_mapping;
	}
	page = vm_normal_page(vma, address, pte);
	if (!page && is_zero_pfn(pte_pfn(pte)))
		page = pfn_to_page(pte_pfn(pte));
	/*
	 * A PFN mapping can be executable without a normal struct page. The
	 * architecture's user translation PTE carries a 48-bit physical address;
	 * use its direct alias, with one aligned tetra read and exception fixup.
	 * The leaf lock retains the mapping through acquisition in either case.
	 */
	if (!page) {
		unsigned long physical = (pte_pfn(pte) << PAGE_SHIFT) +
					 offset_in_page(address);

		error = get_kernel_nofault(*insn, (u32 *)__va(physical));
	} else {
		mapped = kmap_local_page(page);
		*insn = READ_ONCE(*(u32 *)(mapped + offset_in_page(address)));
		kunmap_local(mapped);
		error = 0;
	}
	pte_unmap_unlock(ptep, ptl);
	mmap_read_unlock(mm);
	if (error) {
		signal = SIGBUS;
		code = BUS_ADRERR;
		error = 1;
		goto failed;
	}
	goto out;
retry_mapping:
	mmap_read_unlock(mm);
	cond_resched();
	goto retry;
failed:
	entry->step_state = MMIX_STEP_DISARMED;
	force_sig_fault(signal, code, (void __user *)pc);
out:
	local_irq_disable();
	return error;
}

/* The target is stopped, or current owns its return state. */
void mmix_step_cancel(struct mmix_user_entry *entry)
{
	entry->step_state = MMIX_STEP_DISARMED;
	entry->step_completed = false;
	if (entry->step_tracer) {
		put_task_struct(entry->step_tracer);
		entry->step_tracer = NULL;
	}
}

void mmix_step_complete(struct mmix_user_entry *entry)
{
	entry->step_state = MMIX_STEP_DISARMED;
	entry->step_completed = true;
	entry->step_detach_signal = -1;
}

void user_enable_single_step(struct task_struct *task)
{
	struct mmix_user_entry *entry = task->thread.user_entry;
	struct pt_regs *regs = task_pt_regs(task);

	/* arch_ptrace admitted an owned context before the generic resume hook. */
	mmix_step_cancel(entry);
	get_task_struct(current);
	entry->step_tracer = current;
	if ((entry->syscall_entry || entry->syscall_exit_pending) &&
	    !task->thread.exec_pending && !entry->ptrace_redirect)
		entry->step_state = MMIX_STEP_SYSCALL;
	else if (regs->r_xx >> 63)
		entry->step_state = MMIX_STEP_REQUESTED;
	else
		entry->step_state = MMIX_STEP_REPLAY;
}

void user_disable_single_step(struct task_struct *task)
{
	if (task->thread.user_entry)
		mmix_step_cancel(task->thread.user_entry);
}
