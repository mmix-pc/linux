/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_SYSCALL_H
#define _ASM_MMIX_SYSCALL_H

#include <linux/err.h>
#include <linux/string.h>
#include <asm/ptrace.h>
#include <uapi/linux/audit.h>

static inline int syscall_get_nr(struct task_struct *task, struct pt_regs *regs)
{
	return regs->syscall_nr;
}

static inline void syscall_set_nr(struct task_struct *task,
				  struct pt_regs *regs, int nr)
{
	regs->syscall_nr = nr;
	regs->regs[237] = nr;
}

static inline void syscall_get_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 unsigned long *args)
{
	memcpy(args, regs->syscall_args, sizeof(regs->syscall_args));
}

static inline void syscall_set_arguments(struct task_struct *task,
					 struct pt_regs *regs,
					 const unsigned long *args)
{
	memcpy(regs->syscall_args, args, sizeof(regs->syscall_args));
	memcpy(&regs->regs[231], args, sizeof(regs->syscall_args));
}

static inline long syscall_get_error(struct task_struct *task,
				     struct pt_regs *regs)
{
	return IS_ERR_VALUE(regs->regs[231]) ? regs->regs[231] : 0;
}

static inline long syscall_get_return_value(struct task_struct *task,
					    struct pt_regs *regs)
{
	return regs->regs[231];
}

static inline void syscall_set_return_value(struct task_struct *task,
					    struct pt_regs *regs, int error,
					    long value)
{
	regs->regs[231] = error ? error : value;
}

static inline void syscall_rollback(struct task_struct *task,
				    struct pt_regs *regs)
{
	regs->regs[231] = regs->syscall_args[0];
}

static inline int syscall_get_arch(struct task_struct *task)
{
	return AUDIT_ARCH_MMIX;
}
#endif
