// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/irqflags.h>
#include <linux/nospec.h>
#include <linux/ptrace.h>
#include <linux/syscalls.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/unistd.h>
#include <linux/uaccess.h>
#include <asm/page.h>
#include <asm/rstack.h>
#include <uapi/asm/rstack.h>
#include "user_entry.h"
#include "signal.h"
#include "vfork.h"

static struct mmix_rstack_domain *rstack_syscall_begin(struct mmix_user_entry *entry)
{
	struct mmix_user_state *state;

	if (!current->thread.rstack_chain)
		return ERR_PTR(-EFAULT);
	state = mmix_user_rstack_state(entry->stack);
	if (mmix_user_rstack_validate(state))
		return ERR_PTR(-EFAULT);
	return mmix_rstack_domain_begin(current->mm, current->thread.rstack_chain,
					state->pending.start,
					state->pending.count * sizeof(unsigned long));
}

static long rstack_sync(struct mmix_user_entry *entry)
{
	struct mmix_rstack_domain *domain = rstack_syscall_begin(entry);

	if (IS_ERR(domain))
		return PTR_ERR(domain);
	mmix_rstack_domain_end(current->mm, domain);
	/* Dispatch defers success until owned materialization and return preparation. */
	return 0;
}

static long rstack_query(struct mmix_user_entry *entry, unsigned long output)
{
	struct mmix_rstack_query *query = &entry->rstack_query;
	struct mmix_rstack_domain *domain;
	unsigned long flags;

	if (output & 7)
		return -EINVAL;
	if (!access_ok((void __user *)output, sizeof(*query)))
		return -EFAULT;
	domain = rstack_syscall_begin(entry);
	if (IS_ERR(domain))
		return PTR_ERR(domain);
	spin_lock_irqsave(&current->sighand->siglock, flags);
	query->chain_id = current->thread.rstack_chain;
	query->sigmask = current->blocked.sig[0] & ~(sigmask(SIGKILL) | sigmask(SIGSTOP));
	spin_unlock_irqrestore(&current->sighand->siglock, flags);
	mmix_rstack_domain_end(current->mm, domain);
	/* Copy after materialization, including when output aliases pending backing. */
	entry->rstack_query_output = (void __user *)output;
	entry->rstack_query_pending = true;
	return 0;
}

SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len, unsigned long, prot, unsigned long,
		flags, unsigned long, fd, unsigned long, offset)
{
	if (offset & ~PAGE_MASK)
		return -EINVAL;
	return ksys_mmap_pgoff(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
}

SYSCALL_DEFINE5(clone, unsigned long, flags, unsigned long, stack,
		int __user *, parent_tid, int __user *, child_tid, unsigned long, tls)
{
	struct kernel_clone_args args = {
		.flags = flags & ~CSIGNAL,
		.exit_signal = flags & CSIGNAL,
		.stack = stack,
		.parent_tid = parent_tid,
		.pidfd = parent_tid,
		.child_tid = child_tid,
		.tls = tls,
	};

	/* Extended flags belong to clone3, not this legacy entry. */
	if (flags >> 32)
		return -EOPNOTSUPP;
	if ((args.flags & (CLONE_VM | CLONE_VFORK | CLONE_THREAD)) ==
	    (CLONE_VM | CLONE_VFORK)) {
		if (stack)
			return -EOPNOTSUPP;
		return mmix_vfork_clone(&args);
	}
	return kernel_clone(&args);
}

typedef long (*syscall_fn)(unsigned long, unsigned long, unsigned long, unsigned long,
			   unsigned long, unsigned long);

/* Exit is handled before table dispatch so entry storage can be released. */
#define sys_clone3 sys_ni_syscall
#define sys_rt_sigreturn sys_ni_syscall
#define sys_exit sys_ni_syscall
#define sys_exit_group sys_ni_syscall
#define __SYSCALL(nr, call)	[nr] = (syscall_fn)call,
#define __SYSCALL_WITH_COMPAT(nr, native, compat) __SYSCALL(nr, native)
#define __SYSCALL_NORETURN(nr, call) __SYSCALL(nr, call)
static const syscall_fn syscall_table[__NR_syscalls] = {
#include <asm/syscall_table_64.h>
};

int mmix_user_syscall(struct mmix_user_entry *entry, void *data)
{
	struct pt_regs *regs = &entry->regs;
	unsigned long nr;
	unsigned long *args = regs->syscall_args;
	syscall_fn function;
	bool trace;
	long result = -ENOSYS;

	if (entry->event != MMIX_USER_SYSCALL)
		return mmix_user_fault(entry, data);
	/* Every syscall, including nonreturning services, has one entry boundary. */
	local_irq_enable();
	mmix_ptrace_publish(entry);
	entry->syscall_entry = true;
	trace = test_syscall_work(SYSCALL_TRACE);
	if (trace &&
	    !ptrace_report_syscall_permit_entry(task_pt_regs(current))) {
		entry->syscall_entry = false;
		local_irq_disable();
		return 0;
	}
	entry->syscall_entry = false;
	entry->syscall_exit_pending = true;
	nr = regs->syscall_nr;
	if (entry->ptrace_redirect) {
		local_irq_disable();
		return 0;
	}
	if (nr == -1UL && trace)
		result = regs->regs[231];
	if (nr == __NR_exit || nr == __NR_exit_group) {
		local_irq_disable();
		entry->exit_requested = true;
		entry->exit_group = nr == __NR_exit_group;
		entry->exit_code = (args[0] & 0xff) << 8;
		return 1;
	}
	current->thread.exec_committed = false;
	/* Entry-dependent services complete through the owned return path. */
	if (nr == __NR_rt_sigreturn) {
		int error = mmix_signal_return(entry);

		local_irq_disable();
		return error;
	}
	if (nr == __NR_mmix_rstack_jump) {
		result = mmix_signal_jump(entry, args[0]);
		if (!result || entry->fatal_signal) {
			local_irq_disable();
			return result;
		}
	} else if (nr == __NR_mmix_rstack_sync) {
		result = rstack_sync(entry);
	} else if (nr == __NR_mmix_rstack_query) {
		result = rstack_query(entry, args[0]);
	} else if (nr < ARRAY_SIZE(syscall_table)) {
		function = syscall_table[array_index_nospec(nr, ARRAY_SIZE(syscall_table))];
		result = function(args[0], args[1], args[2], args[3], args[4], args[5]);
	}
	local_irq_disable();
	if (entry->fatal_signal)
		return -EFAULT;
	if (current->thread.exec_pending) {
		current->thread.exec_pending = false;
		entry->stack = current->thread.user_state;
		entry->shadow_physical = __pa(mmix_user_rstack_shadow(entry->stack));
		*regs = *task_pt_regs(current);
		return 0;
	}
	/*
	 * A failed exec past its commit point cannot return to the old image.
	 * Ordinary pending fatal signals must reach get_signal(), which marks
	 * signaled peers for inclusion in a thread-group core dump.
	 */
	if (current->thread.exec_committed ||
	    ((nr == __NR_execve || nr == __NR_execveat) && result < 0 &&
	     signal_pending(current) && sigismember(&current->pending.signal, SIGSEGV))) {
		entry->fatal_signal = fatal_signal_pending(current) ? SIGKILL : SIGSEGV;
		return -EFAULT;
	}
	if (nr == __NR_mmix_rstack_sync) {
		if (result) {
			entry->fatal_signal = SIGSEGV;
			return -EFAULT;
		}
		entry->rstack_sync_pending = true;
		return 0;
	}
	if (entry->ptrace_redirect)
		return 0;
	entry->syscall_result = nr != -1UL;
	regs->regs[231] = result;
	mmix_user_rstack_state(entry->stack)->regs.regs[231] = result;
	return 0;
}
