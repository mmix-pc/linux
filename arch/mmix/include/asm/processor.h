/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_PROCESSOR_H
#define _ASM_MMIX_PROCESSOR_H

#include <asm/thread_info.h>
#include <asm/page.h>
#include <asm/ptrace.h>

#define TASK_SIZE MMIX_TASK_SIZE
#define STACK_TOP TASK_SIZE
#define STACK_TOP_MAX STACK_TOP
#define TASK_UNMAPPED_BASE PAGE_ALIGN(TASK_SIZE / 3)
#define KSTK_EIP(task) instruction_pointer(task_pt_regs(task))
#define KSTK_ESP(task) user_stack_pointer(task_pt_regs(task))

#ifndef __ASSEMBLER__
struct task_struct;
struct pt_regs;
struct mmix_rstack;
struct mmix_user_rstack_state;
struct mmix_signal_activation;
struct mmix_user_entry;
struct mmix_rstack_owner;
struct mmix_vfork;

struct thread_struct {
	unsigned long save;
	unsigned long rstack_base;
	unsigned long rstack_limit;
	unsigned long function;
	unsigned long argument;
	unsigned long started;
	bool exec_pending;
	bool exec_committed;
	struct mmix_rstack *rstack;
	struct mmix_user_rstack_state *user_state;
	u64 rstack_chain;
	struct mmix_rstack_owner *rstack_owner;
	struct mmix_vfork *vfork_child;
	struct mmix_vfork *vfork_prepare;
	struct mmix_signal_activation *signals;
	struct mmix_user_entry *user_entry;
};
#define INIT_THREAD { }

static inline void cpu_relax(void)
{
	asm volatile("SWYM 0,0,0" ::: "memory");
}

unsigned long __get_wchan(struct task_struct *task);
void start_thread(struct pt_regs *regs, unsigned long pc, unsigned long sp);
#endif
#endif
