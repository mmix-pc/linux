// SPDX-License-Identifier: GPL-2.0-only
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/ptrace.h>
#include <linux/resume_user_mode.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>
#include <asm/irq_regs.h>
#include <asm/rstack.h>
#include "user_entry.h"
#include "signal.h"
#include "ipi.h"
#include "../mm/fault.h"

#ifdef CONFIG_MMIX_USER_TEST
static atomic_t entry_fail_step = ATOMIC_INIT(-1);

void mmix_user_entry_fail_after(int steps)
{
	atomic_set(&entry_fail_step, steps);
}

static bool entry_fail(void)
{
	return atomic_read(&entry_fail_step) >= 0 &&
		atomic_dec_return(&entry_fail_step) < 0;
}
#else
static inline bool entry_fail(void)
{
	return false;
}
#endif

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
	struct mmix_fault_info info;

	if (entry->event != MMIX_USER_FAULT)
		return -EOPNOTSUPP;
	entry->fatal_signal = mmix_handle_page_fault(&entry->regs, &info);
	if (entry->fatal_signal == SIGILL && (entry->regs.r_xx & (1UL << 34)) &&
	    (u32)entry->regs.r_xx >> 24 == 0xf9) {
		const struct user_regs_struct *user = &mmix_user_rstack_state(entry->stack)->regs;
		int code = mmix_resume_sigill((u32)entry->regs.r_xx, user);

		if (code)
			info.code = code;
	}
	if (entry->fatal_signal && current->thread.rstack_chain) {
		entry->step_state = MMIX_STEP_DISARMED;
		local_irq_enable();
		if (entry->fatal_signal == SIGSEGV || entry->fatal_signal == SIGBUS ||
		    (entry->fatal_signal == SIGILL && info.code != SI_KERNEL))
			force_sig_fault(entry->fatal_signal, info.code,
					(void __user *)info.address);
		else
			force_sig(entry->fatal_signal);
		local_irq_disable();
		entry->fatal_signal = 0;
	}
	return entry->fatal_signal ? -EFAULT : 0;
}

static const struct mmix_user_entry_ops native_ops = {
	.event = mmix_user_syscall,
	.write = mmix_user_write,
};

static int prepare_return(struct mmix_user_entry *entry)
{
	struct mmix_user_state *state = mmix_user_rstack_state(entry->stack);
	struct mmix_rstack_domain *domain = NULL;
	unsigned long base, top, flags;
	int error;

	if (entry->rstack_sync_pending) {
		if (entry->ops->write != mmix_user_write || !current->thread.rstack_chain)
			return -EFAULT;
		/* This result is still private; failure below must never return to user. */
		state->regs.regs[231] = 0;
	}
	local_irq_save(flags);
	local_irq_enable();
	if (entry->ops->write == mmix_user_write && current->thread.rstack_chain) {
		domain = mmix_rstack_domain_begin(current->mm, current->thread.rstack_chain,
						  state->pending.start,
						  state->pending.count * sizeof(unsigned long));
		if (IS_ERR(domain)) {
			local_irq_restore(flags);
			return PTR_ERR(domain);
		}
	}
	error = mmix_user_rstack_materialize(entry->stack, entry->ops->write, entry->data);
	if (domain)
		mmix_rstack_domain_end(current->mm, domain);
	if (!error && (entry->rstack_sync_pending || entry->rstack_query_pending)) {
		/* Backing is now authoritative, including a query output that aliases it. */
		memset(&state->pending, 0, sizeof(state->pending));
		state->pending.start = state->regs.r_o;
	}
	if (!error && entry->rstack_query_pending) {
		/* Arbitrary output may fault; never hold the owner mutex across this copy. */
		state->regs.regs[231] = copy_to_user(entry->rstack_query_output,
						     &entry->rstack_query,
						     sizeof(entry->rstack_query)) ? -EFAULT : 0;
		entry->rstack_query_pending = false;
	}
	if (!error)
		error = mmix_user_rstack_prepare(entry->stack, &base, &top);
	local_irq_restore(flags);
	if (error)
		return error;
	entry->shadow_base = base;
	entry->shadow_end = PAGE_ALIGN(top + sizeof(unsigned long));
	entry->user_top = top;
	/* Backing writes may have replaced a COW page since the original fault. */
	if (entry->ops->write == mmix_user_write && mmix_refresh_translation(&entry->regs))
		return -EFAULT;
	if (entry->step_state && entry->regs.r_xx >> 32 == 0x03000000UL)
		entry->regs.r_zz &= ~1UL;
	memcpy(entry->regs.regs, mmix_user_rstack_state(entry->stack)->regs.regs,
	       sizeof(entry->regs.regs));
	entry->rstack_sync_pending = false;
	return 0;
}

/* Called with IRQs disabled; every sleepable operation is followed by a recheck. */
static int return_to_user(struct mmix_user_entry *entry)
{
	bool native = entry->ops->write == mmix_user_write && current->thread.rstack_chain;
	bool regs_published = false;
	int error;

	if (!native)
		return prepare_return(entry);
	/* Complete deferred services before a syscall-exit or signal-delivery stop. */
	if (entry->rstack_sync_pending || entry->rstack_query_pending) {
		error = prepare_return(entry);
		if (error)
			return error;
	}
	if (entry->syscall_exit_pending) {
		entry->syscall_exit_pending = false;
		mmix_ptrace_publish(entry);
		local_irq_enable();
		if (test_syscall_work(SYSCALL_TRACE))
			ptrace_report_syscall_exit(task_pt_regs(current), 0);
		else
			regs_published = true;
		local_irq_disable();
	}
	/* A request made at syscall entry/event finishes the current syscall,
	 * without inserting its successor. Interrupted restart results instead
	 * return through signal selection before a fresh operation is armed.
	 */
	if (entry->step_state == MMIX_STEP_SYSCALL) {
		long result = entry->regs.regs[231];

		if (entry->syscall_result &&
		    (result == -ERESTARTSYS || result == -ERESTARTNOINTR ||
		     result == -ERESTARTNOHAND || result == -ERESTART_RESTARTBLOCK))
			entry->step_state = MMIX_STEP_REQUESTED;
		else
			mmix_step_complete(entry);
	}
	for (;;) {
		u32 insn = 0;
		unsigned long pc;

		local_irq_enable();
		if (need_resched()) {
			schedule();
			regs_published = false;
		}
		if (test_thread_flag(TIF_NOTIFY_RESUME)) {
			mmix_ptrace_publish(entry);
			resume_user_mode_work(task_pt_regs(current));
			regs_published = false;
		}
		/* An untraced syscall exit already published this register image. */
		if (!regs_published)
			mmix_ptrace_publish(entry);
		regs_published = false;
		error = mmix_signal_pending(entry, false);
		local_irq_disable();
		if (error < 0)
			return error;
		/* Tracer exit unlinks a task without calling ptrace_disable(). */
		if (!READ_ONCE(current->ptrace) ||
		    (entry->step_tracer && READ_ONCE(current->parent) != entry->step_tracer))
			mmix_step_cancel(entry);
		error = prepare_return(entry);
		if (error)
			return error;
		/* Signal delivery can replace a requested partial return with fresh code. */
		if (entry->step_state == MMIX_STEP_REPLAY && entry->regs.r_xx >> 63)
			entry->step_state = MMIX_STEP_REQUESTED;
		if (entry->step_state == MMIX_STEP_REQUESTED) {
			pc = entry->regs.pc;
			if (!(entry->regs.r_xx >> 63))
				return -EOPNOTSUPP;
			error = mmix_step_fetch(entry, &insn);
			if (error < 0)
				return error;
			if (error)
				continue;
		}
		if (!(read_thread_flags() & (_TIF_NEED_RESCHED | _TIF_SIGPENDING |
					     _TIF_NOTIFY_SIGNAL | _TIF_NOTIFY_RESUME))) {
			if (entry->step_state == MMIX_STEP_REQUESTED) {
				if (insn >> 24 == 0xf9) {
					error = mmix_step_resume(entry, insn);
					if (error < 0)
						return error;
					if (error)
						continue;
					break;
				}
				entry->regs.r_ww = pc + 4;
				entry->regs.r_xx = insn;
				entry->regs.r_yy = 0;
				entry->regs.r_zz = 0;
				entry->step_state = MMIX_STEP_IN_FLIGHT;
			}
			/* Resume the saved partial operation without reinsertion. */
			if (entry->step_state == MMIX_STEP_REPLAY)
				entry->step_state = MMIX_STEP_IN_FLIGHT;
			break;
		}
	}
	/* prepare_return refreshed translations before the final IRQ-off recheck. */
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
	if (current->thread.user_entry)
		return -EBUSY;
	if (!stack || !ops->event)
		return -EINVAL;
	if (entry_fail())
		return -ENOMEM;
	entry = kzalloc_obj(*entry);
	if (!entry)
		return -ENOMEM;
	current->thread.user_entry = entry;
	entry->stack = stack;
	current->thread.exec_pending = false;
	current->thread.exec_committed = false;
	entry->ops = ops;
	entry->data = data;
	entry->shadow_physical = __pa(mmix_user_rstack_shadow(stack));
	state = mmix_user_rstack_state(stack);
	entry->regs.pc = state->regs.pc;
	entry->regs.r_ww = state->regs.pc;
	entry->regs.r_xx = 1UL << 63;
	entry->regs.syscall_nr = -1;
	memcpy(entry->regs.regs, state->regs.regs, sizeof(entry->regs.regs));
	if (ops->write == mmix_user_write && current->thread.rstack_chain) {
		local_irq_save(flags);
		local_irq_enable();
		mmix_ptrace_publish(entry);
		error = mmix_signal_pending(entry, false);
		local_irq_restore(flags);
		if (error < 0) {
			fatal_signal = entry->fatal_signal;
			goto out;
		}
	}
	/* A traced fork child observes its initial stop before the syscall return. */
	entry->syscall_exit_pending = test_syscall_work(SYSCALL_TRACE);
	local_irq_save(flags);
	error = return_to_user(entry);
	if (error) {
		fatal_signal = entry->fatal_signal;
		local_irq_restore(flags);
		goto out;
	}
	mmix_user_run(entry);
	error = entry->error;
	fatal_signal = entry->fatal_signal;
	exit_requested = entry->exit_requested;
	exit_group = entry->exit_group;
	exit_code = entry->exit_code;
	local_irq_restore(flags);
out:
	current->thread.user_entry = NULL;
	if (entry->step_tracer)
		put_task_struct(entry->step_tracer);
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
	bool syscall, breakpoint;
	int error;

	if (entry->error) {
		entry->error = -EINVAL;
		return 1;
	}
	error = mmix_user_rstack_capture(entry->stack, &entry->capture, regs->r_ww,
					 regs->regs + 230);
	if (error)
		goto failed;
	/* The interrupted globals are already in the entry frame. */
	memcpy(regs->regs, state->regs.regs, 230 * sizeof(*regs->regs));
	regs->pc = regs->r_ww;
	regs->syscall_nr = -1;
	entry->ptrace_redirect = false;
	pending = regs->r_q & ((0xffUL << 32) | MMIX_IRQ_EXTERNAL);
	syscall = (regs->r_xx >> 63) && (u32)regs->r_xx == 0x00010000 &&
		  !(pending & (0xffUL << 32));
	breakpoint = (regs->r_xx >> 63) && (u32)regs->r_xx == MMIX_BREAKPOINT_INSN &&
		     !(pending & (0xffUL << 32));
	if (entry->step_state && (u32)regs->r_xx == 0xfd000000 &&
	    (regs->r_xx >> 32 == 0x03000000UL || (pending & BIT_ULL(37)))) {
		if (regs->r_xx >> 32 != 0x03000000UL)
			regs->pc -= 4;
		state->regs.pc = regs->pc;
		regs->r_ww = regs->pc;
		regs->r_xx = 1UL << 63;
		regs->r_yy = 0;
		regs->r_zz = 0;
		entry->step_state = MMIX_STEP_DISARMED;
		mmix_ack_requests(pending & (0xffUL << 32));
		local_irq_enable();
		if (READ_ONCE(current->ptrace))
			mmix_step_complete(entry);
		local_irq_disable();
		goto return_work;
	}
	/* Pending IRQs must not consume synchronous forced traps or translations. */
	if (IS_ENABLED(CONFIG_MMIX_SIGNAL_TEST) && (regs->r_q & BIT_ULL(6)) &&
	    (regs->r_xx >> 63) && !(pending & (0xffUL << 32)) && !syscall && !breakpoint) {
		/* Optional asynchronous injection; never replace a fault or syscall. */
		mmix_ack_requests(BIT_ULL(6));
		local_irq_enable();
		force_sig(SIGUSR2);
		local_irq_disable();
	} else if ((pending & MMIX_IRQ_EXTERNAL) && !(pending & (0xffUL << 32)) &&
		   !syscall && !breakpoint && regs->r_xx >> 32 != 0x03000000UL) {
		entry->interrupts++;
		old_regs = set_irq_regs(regs);
		irq_enter();
		if (pending & MMIX_IRQ_IPI)
			mmix_ipi_interrupt();
		if (pending & MMIX_IRQ_CONTROLLER) {
			handle_arch_irq(regs);
			mmix_ack_requests(MMIX_IRQ_CONTROLLER);
		}
		irq_exit();
		/* The prior pointer belongs to this CPU; restore it before possible migration. */
		set_irq_regs(old_regs);
		if (IS_ENABLED(CONFIG_PREEMPTION) && !preempt_count() && need_resched())
			preempt_schedule_irq();
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
		if (breakpoint && current->thread.rstack_chain &&
		    entry->ops->write == mmix_user_write) {
			/* Suppressing SIGTRAP retries the patched instruction at this PC. */
			regs->r_ww = regs->pc;
			regs->r_xx = 1UL << 63;
			local_irq_enable();
			force_sig_fault(SIGTRAP, TRAP_BRKPT, (void __user *)regs->pc);
			local_irq_disable();
			error = 0;
		} else {
			/* The caller owns syscall/fault policy and explicit PC adjustment. */
			error = entry->ops->event(entry, entry->data);
		}
		if (error > 0)
			return 1;
		if (error < 0)
			goto failed;
		mmix_ack_requests(pending & (0xffUL << 32));
	}
return_work:
	local_irq_disable();
	error = return_to_user(entry);
	if (!error)
		return 0;
failed:
	if (!entry->fatal_signal && (error == -EFAULT || entry->rstack_sync_pending ||
				     entry->rstack_query_pending) &&
	    entry->ops->write == mmix_user_write)
		entry->fatal_signal = SIGSEGV;
	entry->error = error;
	return 1;
}
