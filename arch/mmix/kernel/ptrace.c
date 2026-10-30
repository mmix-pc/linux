// SPDX-License-Identifier: GPL-2.0-only
#include <linux/ptrace.h>
#include <linux/elf.h>
#include <linux/sched/debug.h>
#include <linux/regset.h>
#include <linux/sched/task_stack.h>
#include <linux/slab.h>
#include <linux/sched/signal.h>
#include <asm/elf.h>
#include "user_entry.h"

struct pt_regs *task_pt_regs(struct task_struct *task)
{
	return (struct pt_regs *)((char *)task_stack_page(task) + THREAD_SIZE) -
	       1;
}

void ptrace_disable(struct task_struct *task)
{
	clear_tsk_thread_flag(task, TIF_SYSCALL_TRACE);
}

/* Publish the logical bank without exposing or replacing privileged state. */
static void copy_user_regs(struct pt_regs *to, const struct user_regs_struct *from)
{
	memcpy(to->regs, from->regs, sizeof(to->regs));
	to->pc = from->pc;
	to->r_g = from->r_g;
	to->r_l = from->r_l;
	to->r_o = from->r_o;
	to->r_a = from->r_a;
	to->r_b = from->r_b;
	to->r_d = from->r_d;
	to->r_e = from->r_e;
	to->r_h = from->r_h;
	to->r_j = from->r_j;
	to->r_m = from->r_m;
	to->r_p = from->r_p;
	to->r_r = from->r_r;
	to->r_w = from->r_w;
	to->r_x = from->r_x;
	to->r_y = from->r_y;
	to->r_z = from->r_z;
}

void mmix_ptrace_publish(struct mmix_user_entry *entry)
{
	copy_user_regs(&entry->regs, &mmix_user_rstack_state(entry->stack)->regs);
	*task_pt_regs(current) = entry->regs;
}

static void commit_state(struct task_struct *target, const struct mmix_user_state *next)
{
	struct mmix_user_state *state = mmix_user_rstack_state(target->thread.user_state);
	struct mmix_user_entry *entry = target->thread.user_entry;
	struct pt_regs *regs = task_pt_regs(target);
	bool redirect = next->regs.pc != state->regs.pc;
	bool cancel_restart = redirect ||
		memcmp(next->regs.regs + 231, state->regs.regs + 231,
		       7 * sizeof(unsigned long));

	if (!memcmp(state, next, sizeof(*state)))
		return;
	*state = *next;
	copy_user_regs(regs, &state->regs);
	/* A changed context must never reuse hidden instruction operands. */
	regs->r_ww = state->regs.pc;
	regs->r_xx = 1UL << 63;
	regs->r_yy = 0;
	regs->r_zz = 0;
	if (cancel_restart)
		target->restart_block.fn = do_no_restart_syscall;
	/* Exec can stop after replacing user_state, before adopting the new entry. */
	if (entry && entry->stack == target->thread.user_state) {
		if (entry->syscall_entry) {
			regs->syscall_nr = state->regs.regs[237];
			memcpy(regs->syscall_args, state->regs.regs + 231,
			       sizeof(regs->syscall_args));
		}
		entry->regs = *regs;
		if (cancel_restart)
			entry->syscall_result = false;
		entry->rstack_sync_pending = false;
		entry->rstack_query_pending = false;
		entry->ptrace_redirect |= redirect;
	}
}

static int mmix_regset_set(struct task_struct *target, const struct user_regset *regset,
			   unsigned int pos, unsigned int count,
			   const void *kbuf, const void __user *ubuf)
{
	const struct mmix_user_state *old = mmix_user_rstack_snapshot(target);
	struct mmix_user_state *next;
	void *bank;
	unsigned int size;
	int error;

	if (IS_ERR(old))
		return PTR_ERR(old);
	next = kvmalloc_obj(*next);
	if (!next)
		return -ENOMEM;
	*next = *old;
	if (regset->core_note_type == NT_PRSTATUS) {
		bank = &next->regs;
		size = sizeof(next->regs);
	} else {
		bank = &next->pending;
		size = sizeof(next->pending);
	}
	error = user_regset_copyin(&pos, &count, &kbuf, &ubuf, bank, 0, size);
	if (error)
		goto out;
	/* No chain transfer or silent loss of older frames through a regset write. */
	if (next->regs.r_o != old->regs.r_o ||
	    next->pending.start != old->pending.start ||
	    next->pending.count != old->pending.count) {
		error = -EINVAL;
		goto out;
	}
	error = mmix_user_rstack_validate(next);
	if (error)
		goto out;
	/* An unchanged partial instruction retains its private replay exactly. */
	if (!(task_pt_regs(target)->r_xx >> 63) &&
	    next->regs.pc == old->regs.pc && memcmp(old, next, sizeof(*old))) {
		error = -EOPNOTSUPP;
		goto out;
	}
	commit_state(target, next);
out:
	kvfree(next);
	return error;
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

/* Both notes share one owned snapshot; reading must not flush or alter it. */
static const struct user_regset mmix_user_regsets[] = {
	{
		USER_REGSET_NOTE_TYPE(PRSTATUS),
		.n = ELF_NGREG,
		.size = sizeof(elf_greg_t),
		.align = sizeof(elf_greg_t),
		.regset_get = mmix_gpr_get,
		.set = mmix_regset_set,
	},
	{
		USER_REGSET_NOTE_TYPE(MMIX_RSTACK),
		.n = sizeof(struct mmix_user_rstack) / sizeof(u64),
		.size = sizeof(u64),
		.align = sizeof(u64),
		.regset_get = mmix_rstack_get,
		.set = mmix_regset_set,
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

long arch_ptrace(struct task_struct *child, long request, unsigned long addr,
		 unsigned long data)
{
	long error;

	/* A sigreturn exit stop can expose an unfinished user instruction. */
	if (request == PTRACE_SET_SYSCALL_INFO && !(task_pt_regs(child)->r_xx >> 63))
		return -EOPNOTSUPP;
	error = ptrace_request(child, request, addr, data);

	if (!error && request == PTRACE_SET_SYSCALL_INFO) {
		struct mmix_user_state *state = mmix_user_rstack_state(child->thread.user_state);
		struct mmix_user_entry *entry = child->thread.user_entry;
		struct pt_regs *regs = task_pt_regs(child);
		bool changed = memcmp(state->regs.regs + 231, regs->regs + 231,
				      7 * sizeof(unsigned long));

		/* Generic syscall helpers edit pt_regs, not the logical return image. */
		memcpy(state->regs.regs + 231, regs->regs + 231, 7 * sizeof(unsigned long));
		if (entry && entry->stack == child->thread.user_state) {
			entry->regs = *regs;
			if (changed)
				entry->syscall_result = false;
		}
		if (changed)
			child->restart_block.fn = do_no_restart_syscall;
	}
	return error;
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
