// SPDX-License-Identifier: GPL-2.0-only
#define COMPILE_OFFSETS
#include <linux/kbuild.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/thread_info.h>

int main(void)
{
	DEFINE(TASK_THREAD, offsetof(struct task_struct, thread));
	DEFINE(PT_PC, offsetof(struct pt_regs, pc));
	DEFINE(PT_MASK, offsetof(struct pt_regs, mask));
	DEFINE(PT_SAVE, offsetof(struct pt_regs, save));
	DEFINE(PT_SIZE, sizeof(struct pt_regs));
	DEFINE(TI_FLAGS, offsetof(struct thread_info, flags));
	DEFINE(TI_PREEMPT_COUNT, offsetof(struct thread_info, preempt_count));
	DEFINE(THREAD_SAVE, offsetof(struct thread_struct, save));
	DEFINE(THREAD_RSTACK_BASE, offsetof(struct thread_struct, rstack_base));
	DEFINE(THREAD_RSTACK_LIMIT, offsetof(struct thread_struct, rstack_limit));
	DEFINE(MMIX_THREAD_SIZE, THREAD_SIZE);
	DEFINE(MMIX_REGISTER_STACK_SIZE, MMIX_RSTACK_SIZE);
	return 0;
}
