// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/irqflags.h>
#include <linux/nospec.h>
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
	struct kernel_clone_args args = { .exit_signal = SIGCHLD };

	/* Reject sharing, new stacks and all unqualified flags before publication. */
	if (flags != SIGCHLD || stack)
		return -EOPNOTSUPP;
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
	unsigned long nr = regs->syscall_nr;
	unsigned long *args = regs->syscall_args;
	syscall_fn function;
	long result = -ENOSYS;

	if (entry->event != MMIX_USER_SYSCALL)
		return mmix_user_fault(entry, data);
	if (nr == __NR_exit || nr == __NR_exit_group) {
		entry->exit_requested = true;
		entry->exit_group = nr == __NR_exit_group;
		entry->exit_code = (args[0] & 0xff) << 8;
		return 1;
	}
	current->thread.exec_committed = false;
	/* The user snapshot is owned and the resident entry is released. */
	local_irq_enable();
	/* Entry-dependent services complete through the owned return path. */
	*task_pt_regs(current) = *regs;
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
	if (current->thread.exec_pending) {
		current->thread.exec_pending = false;
		entry->stack = current->thread.user_state;
		entry->shadow_physical = __pa(mmix_user_rstack_shadow(entry->stack));
		*regs = *task_pt_regs(current);
		return 0;
	}
	/* A failed exec past its commit point cannot return to the old image. */
	if (current->thread.exec_committed || fatal_signal_pending(current) ||
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
	entry->syscall_result = true;
	regs->regs[231] = result;
	mmix_user_rstack_state(entry->stack)->regs.regs[231] = result;
	return 0;
}
