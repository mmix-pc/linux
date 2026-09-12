/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_PROCESSOR_H
#define _ASM_MMIX_PROCESSOR_H

#include <asm/thread_info.h>

#ifndef __ASSEMBLER__
struct task_struct;
struct pt_regs;

struct thread_struct {
	unsigned long save;
	unsigned long rstack_base;
	unsigned long rstack_limit;
	unsigned long function;
	unsigned long argument;
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
