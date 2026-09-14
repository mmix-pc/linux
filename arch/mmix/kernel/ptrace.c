// SPDX-License-Identifier: GPL-2.0-only
#include <linux/ptrace.h>
#include <linux/elf.h>
#include <linux/sched/debug.h>
#include <linux/regset.h>
#include <linux/sched/task_stack.h>
#include <asm/elf.h>

struct pt_regs *task_pt_regs(struct task_struct *task)
{
	return (struct pt_regs *)((char *)task_stack_page(task) + THREAD_SIZE) -
	       1;
}

void ptrace_disable(struct task_struct *task)
{
	clear_tsk_thread_flag(task, TIF_SYSCALL_TRACE);
}

long arch_ptrace(struct task_struct *child, long request, unsigned long addr,
		 unsigned long data)
{
	return ptrace_request(child, request, addr, data);
}

/* User register capture/restore is not enabled by the kernel-thread profile. */
static const struct user_regset_view mmix_regsets = {
	.name = "mmix",
	.e_machine = EM_MMIX,
};

const struct user_regset_view *task_user_regset_view(struct task_struct *task)
{
	return &mmix_regsets;
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
