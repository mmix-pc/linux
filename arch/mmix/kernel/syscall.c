// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/irqflags.h>
#include <linux/nospec.h>
#include <linux/syscalls.h>
#include <linux/sched/signal.h>
#include <linux/unistd.h>
#include <asm/page.h>
#include "user_entry.h"

SYSCALL_DEFINE6(mmap, unsigned long, addr, unsigned long, len, unsigned long, prot, unsigned long,
		flags, unsigned long, fd, unsigned long, offset)
{
	if (offset & ~PAGE_MASK)
		return -EINVAL;
	return ksys_mmap_pgoff(addr, len, prot, flags, fd, offset >> PAGE_SHIFT);
}

typedef long (*syscall_fn)(unsigned long, unsigned long, unsigned long, unsigned long,
			   unsigned long, unsigned long);

/* Context-changing entry/return paths have not been implemented yet. */
#define sys_clone sys_ni_syscall
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
	current->thread.exec_committed = false;
	/* The user snapshot is owned and the resident entry is released. */
	local_irq_enable();
	if (nr < ARRAY_SIZE(syscall_table)) {
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
	regs->regs[231] = result;
	mmix_user_rstack_state(entry->stack)->regs.regs[231] = result;
	return 0;
}
