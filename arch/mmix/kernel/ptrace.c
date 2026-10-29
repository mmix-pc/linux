// SPDX-License-Identifier: GPL-2.0-only
#include <linux/ptrace.h>
#include <linux/elf.h>
#include <linux/sched/debug.h>
#include <linux/regset.h>
#include <linux/sched/task_stack.h>
#include <linux/uio.h>
#include <asm/elf.h>
#include "user_rstack.h"

struct pt_regs *task_pt_regs(struct task_struct *task)
{
	return (struct pt_regs *)((char *)task_stack_page(task) + THREAD_SIZE) -
	       1;
}

void ptrace_disable(struct task_struct *task)
{
	clear_tsk_thread_flag(task, TIF_SYSCALL_TRACE);
}

static int mmix_gpr_get(struct task_struct *target, const struct user_regset *regset,
			struct membuf to)
{
	const struct mmix_user_state *state = mmix_user_rstack_snapshot(target);

	if (IS_ERR(state))
		return PTR_ERR(state);
	return membuf_write(&to, &state->regs, sizeof(state->regs));
}

static int mmix_rstack_get(struct task_struct *target, const struct user_regset *regset,
			   struct membuf to)
{
	const struct mmix_user_state *state = mmix_user_rstack_snapshot(target);

	if (IS_ERR(state))
		return PTR_ERR(state);
	return membuf_write(&to, &state->pending, sizeof(state->pending));
}

/* Both notes read the same owned snapshot; dumping must not flush or alter it. */
static const struct user_regset mmix_user_regsets[] = {
	{
		USER_REGSET_NOTE_TYPE(PRSTATUS),
		.n = ELF_NGREG,
		.size = sizeof(elf_greg_t),
		.align = sizeof(elf_greg_t),
		.regset_get = mmix_gpr_get,
	},
	{
		USER_REGSET_NOTE_TYPE(MMIX_RSTACK),
		.n = sizeof(struct mmix_user_rstack) / sizeof(u64),
		.size = sizeof(u64),
		.align = sizeof(u64),
		.regset_get = mmix_rstack_get,
	},
};

static const struct user_regset_view mmix_regsets = {
	.name = "mmix",
	.e_machine = EM_MMIX,
	.regsets = mmix_user_regsets,
	.n = ARRAY_SIZE(mmix_user_regsets),
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &mmix_regsets;
}

/* Read-only regsets do not imply the full HAVE_ARCH_TRACEHOOK contract. */
static int mmix_ptrace_getregset(struct task_struct *child, unsigned long type,
				struct iovec __user *user_iov)
{
	struct iovec iov;
	unsigned int i;
	int error;

	if (copy_from_user(&iov, user_iov, sizeof(iov)))
		return -EFAULT;
	for (i = 0; i < ARRAY_SIZE(mmix_user_regsets); i++) {
		const struct user_regset *regset = &mmix_user_regsets[i];

		if (regset->core_note_type != type)
			continue;
		if (iov.iov_len % regset->size)
			return -EINVAL;
		iov.iov_len = min_t(size_t, iov.iov_len, regset->n * regset->size);
		error = copy_regset_to_user(child, &mmix_regsets, i, 0,
					    iov.iov_len, iov.iov_base);
		if (error)
			return error;
		return put_user(iov.iov_len, &user_iov->iov_len);
	}
	return -EINVAL;
}

long arch_ptrace(struct task_struct *child, long request, unsigned long addr,
		 unsigned long data)
{
	switch (request) {
	case PTRACE_GETREGSET:
		return mmix_ptrace_getregset(child, addr, (struct iovec __user *)data);
	case PTRACE_SETREGSET:
		return -EOPNOTSUPP;
	default:
		return ptrace_request(child, request, addr, data);
	}
}

void show_regs(struct pt_regs *regs)
{
	unsigned int i;

	pr_info("PC: %016lx SP: %016lx rJ: %016lx\n", regs->pc, regs->regs[254],
		regs->r_j);
	pr_info("rO: %016lx rS: %016lx rG: %lu rL: %lu\n", regs->r_o, regs->r_s,
		regs->r_g, regs->r_l);
	for (i = 0; i < 256; i += 4)
		pr_info("r%03u: %016lx %016lx %016lx %016lx\n", i,
			regs->regs[i], regs->regs[i + 1], regs->regs[i + 2],
			regs->regs[i + 3]);
}

unsigned long __get_wchan(struct task_struct *task)
{
	/* No qualified caller-chain walker yet; zero means unavailable. */
	return 0;
}

void show_stack(struct task_struct *task, unsigned long *sp, const char *loglvl)
{
	if (!task)
		task = current;
	printk("%sMMIX: task %s/%d, caller-chain unwinding unavailable\n",
	       loglvl, task->comm, task_pid_nr(task));
}
