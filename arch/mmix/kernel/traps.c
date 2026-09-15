// SPDX-License-Identifier: GPL-2.0-only
#include <linux/extable.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <asm/boot.h>
#include <asm/current.h>
#include <asm/irq_regs.h>
#include <asm/tlbflush.h>
#include "entry.h"
#include "../mm/mmu.h"
#include "../mm/fault.h"

#ifdef CONFIG_MMIX_BOOT_TEST
static struct mmix_fault_sample last_fault;

void mmix_boot_fault_sample(struct mmix_fault_sample *sample)
{
	unsigned long flags;

	local_irq_save(flags);
	*sample = last_fault;
	local_irq_restore(flags);
}
#endif

int fixup_exception(struct pt_regs *regs)
{
	const struct exception_table_entry *fixup;

	fixup = search_exception_tables(regs->pc);
	if (!fixup || (long)fixup->fixup >= 0 || (fixup->fixup & 3))
		return 0;
#ifdef CONFIG_MMIX_BOOT_TEST
	last_fault.address = regs->r_yy;
	last_fault.cause = regs->r_xx;
	last_fault.pc = regs->pc;
	last_fault.count++;
#endif
	regs->pc = fixup->fixup;
	regs->r_ww = fixup->fixup;
	regs->r_xx = 1UL << 63;
	return 1;
}

void mmix_exception_prepare(struct mmix_entry_state *entry, struct pt_regs *regs)
{
	unsigned long *locals;
	unsigned long i;

	*regs = entry->regs;
	/* Ordinary exceptions and IRQs are not syscall stops. */
	regs->syscall_nr = -1;
	locals = (void *)regs->r_o;
	/* External IRQs save the next PC; synchronous causes name the prior insn. */
	regs->pc = regs->r_ww;
	if (regs->r_xx >> 32 == 0x03000000UL ||
	    !(regs->r_q & regs->mask & MMIX_IRQ_CONTROLLER) ||
	    (regs->r_xx & MMIX_KERNEL_FAULT_MASK))
		regs->pc -= 4;
	for (i = 0; i < regs->r_l; i++)
		regs->regs[i] = locals[i];
	for (; i < 230; i++)
		regs->regs[i] = 0;
}

void mmix_exception_finish(struct mmix_entry_state *entry, struct pt_regs *regs)
{
	unsigned long rv;

	local_irq_disable();
	WRITE_ONCE(entry->busy, 1);
	asm volatile("GET %0,rV" : "=r" (rv));
	if (mmix_mmu_state.root != (pgd_t *)regs->root || rv != regs->r_v) {
		WRITE_ONCE(mmix_mmu_state.root, (pgd_t *)regs->root);
		asm volatile("PUT rV,%0" : : "r" (regs->r_v) : "memory");
		flush_tlb_all();
	}
	/* Return assembly consumes only the resident slot after UNSAVE. */
	entry->regs = *regs;
}

void mmix_exception_dispatch(struct mmix_entry_state *entry, struct pt_regs *regs)
{
	struct pt_regs *old_regs;
	unsigned long pending;

	mmix_exception_prepare(entry, regs);
	old_regs = set_irq_regs(regs);
	pending = regs->r_q & regs->mask;
	/* The detached frame survives nested IRQs and sleeping page faults. */
	WRITE_ONCE(entry->busy, 0);
	if (regs->r_xx >> 32 == 0x03000000UL) {
		/* Missing-leaf data fault admitted by the stackless refill path. */
		if (mmix_handle_page_fault(regs) && !fixup_exception(regs))
			mmix_exception_fatal();
	} else if (pending & MMIX_KERNEL_FAULT_MASK) {
		if (mmix_handle_page_fault(regs) && !fixup_exception(regs))
			mmix_exception_fatal();
		mmix_ack_requests(pending & MMIX_KERNEL_FAULT_MASK);
	} else if (pending & MMIX_IRQ_CONTROLLER) {
		irq_enter();
		handle_arch_irq(regs);
		mmix_ack_requests(MMIX_IRQ_CONTROLLER);
		/* The complete frame is now task-owned, including across softirqs. */
		WRITE_ONCE(entry->busy, 0);
		irq_exit();
		if (IS_ENABLED(CONFIG_PREEMPTION) && !preempt_count() && need_resched())
			preempt_schedule_irq();
	} else {
		mmix_exception_fatal();
	}
	local_irq_disable();
	set_irq_regs(old_regs);
	mmix_exception_finish(entry, regs);
}
