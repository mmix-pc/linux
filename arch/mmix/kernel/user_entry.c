// SPDX-License-Identifier: GPL-2.0-only
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>
#include <asm/irq_regs.h>
#include "user_entry.h"
#include "../mm/fault.h"

struct mmix_user_entry *mmix_active_user;
unsigned long mmix_user_shadow_active;

int mmix_user_write(void *data, unsigned long address, const void *source, size_t size)
{
	unsigned long flags;
	unsigned long left;

	local_irq_save(flags);
	local_irq_enable();
	left = copy_to_user((void __user *)address, source, size);
	local_irq_restore(flags);
	return left ? -EFAULT : 0;
}

int mmix_user_fault(struct mmix_user_entry *entry, void *data)
{
	if (entry->event != MMIX_USER_FAULT)
		return -EOPNOTSUPP;
	entry->fatal_signal = mmix_handle_page_fault(&entry->regs);
	return entry->fatal_signal ? -EFAULT : 0;
}

static const struct mmix_user_entry_ops native_ops = {
	.event = mmix_user_syscall,
	.write = mmix_user_write,
};

static int prepare_return(struct mmix_user_entry *entry)
{
	unsigned long base, top;
	int error;

	error = mmix_user_rstack_restore(entry->stack, entry->ops->write, entry->data, &base, &top);
	if (error)
		return error;
	entry->shadow_base = base;
	entry->shadow_end = PAGE_ALIGN(top + sizeof(unsigned long));
	entry->user_top = top;
	/* Backing writes may have replaced a COW page since the original fault. */
	if (entry->ops->write == mmix_user_write && mmix_refresh_translation(&entry->regs))
		return -EFAULT;
	memcpy(entry->regs.regs, mmix_user_rstack_state(entry->stack)->regs.regs,
	       sizeof(entry->regs.regs));
	return 0;
}

int mmix_user_enter(struct mmix_user_rstack_state *stack, const struct mmix_user_entry_ops *ops,
		    void *data)
{
	struct mmix_user_entry *entry;
	struct mmix_user_state *state;
	unsigned long flags;
	int error, fatal_signal = 0, exit_code = 0;
	bool exit_requested = false, exit_group = false;

	if (!ops)
		ops = &native_ops;
	if (!stack || !ops->event)
		return -EINVAL;
	entry = kzalloc_obj(*entry);
	if (!entry)
		return -ENOMEM;
	entry->stack = stack;
	current->thread.exec_pending = false;
	current->thread.exec_committed = false;
	entry->ops = ops;
	entry->data = data;
	entry->shadow_physical = __pa(mmix_user_rstack_shadow(stack));
	error = prepare_return(entry);
	if (error)
		goto out;
	state = mmix_user_rstack_state(stack);
	entry->regs.r_ww = state->regs.pc;
	entry->regs.r_xx = 1UL << 63;
	local_irq_save(flags);
	mmix_user_run(entry);
	error = entry->error;
	fatal_signal = entry->fatal_signal;
	exit_requested = entry->exit_requested;
	exit_group = entry->exit_group;
	exit_code = entry->exit_code;
	local_irq_restore(flags);
out:
	kfree(entry);
	if (exit_requested) {
		local_irq_enable();
		if (exit_group)
			do_group_exit(exit_code);
		do_exit(exit_code);
	}
	if (fatal_signal) {
		/* Fatal fault policy; signal-handler delivery belongs to the return path. */
		local_irq_enable();
		do_group_exit(fatal_signal);
	}
	return error;
}

int mmix_user_dispatch(struct mmix_user_entry *entry)
{
	struct mmix_user_state *state = mmix_user_rstack_state(entry->stack);
	struct pt_regs *regs = &entry->regs;
	struct pt_regs *old_regs;
	unsigned long pending;
	bool syscall;
	int error;

	if (entry->error) {
		entry->error = -EINVAL;
		return 1;
	}
	error = mmix_user_rstack_capture(entry->stack, &entry->capture, regs->r_ww);
	if (error)
		goto failed;
	/* SAVE's scratch globals are not the interrupted user register values. */
	memcpy(state->regs.regs + 230, regs->regs + 230, 26 * sizeof(unsigned long));
	error = mmix_user_rstack_validate(state);
	if (error)
		goto failed;
	memcpy(regs->regs, state->regs.regs, sizeof(regs->regs));
	regs->pc = regs->r_ww;
	regs->syscall_nr = -1;
	pending = regs->r_q & ((0xffUL << 32) | MMIX_IRQ_CONTROLLER);
	syscall = (regs->r_xx >> 63) && (u32)regs->r_xx == 0x00010000 &&
		  !(pending & (0xffUL << 32));
	/* Pending IRQs must not consume synchronous forced traps or translations. */
	if ((pending & MMIX_IRQ_CONTROLLER) && !(pending & (0xffUL << 32)) && !syscall &&
	    regs->r_xx >> 32 != 0x03000000UL) {
		entry->interrupts++;
		old_regs = set_irq_regs(regs);
		irq_enter();
		handle_arch_irq(regs);
		mmix_ack_requests(MMIX_IRQ_CONTROLLER);
		irq_exit();
		if (IS_ENABLED(CONFIG_PREEMPTION) && !preempt_count() && need_resched())
			preempt_schedule_irq();
		set_irq_regs(old_regs);
	} else {
		entry->event = MMIX_USER_FAULT;
		if (syscall) {
			entry->event = MMIX_USER_SYSCALL;
			regs->syscall_nr = regs->regs[237];
			memcpy(regs->syscall_args, regs->regs + 231, sizeof(regs->syscall_args));
		} else {
			/* Software instruction-fetch refill names the faulting PC directly. */
			if (!(regs->r_xx >> 32 == 0x03000000UL && (u32)regs->r_xx == 0xfd000000))
				regs->pc -= 4;
			state->regs.pc = regs->pc;
		}
		/* The caller owns syscall/fault policy and any explicit PC adjustment. */
		error = entry->ops->event(entry, entry->data);
		if (error > 0)
			return 1;
		if (error < 0)
			goto failed;
		mmix_ack_requests(pending & (0xffUL << 32));
	}
	local_irq_disable();
	error = prepare_return(entry);
	if (!error)
		return 0;
failed:
	if (!entry->fatal_signal && error == -EFAULT && entry->ops->write == mmix_user_write)
		entry->fatal_signal = SIGSEGV;
	entry->error = error;
	return 1;
}
