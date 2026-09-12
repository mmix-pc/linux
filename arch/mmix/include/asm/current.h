/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_CURRENT_H
#define _ASM_MMIX_CURRENT_H

struct task_struct;

static __always_inline struct task_struct *get_current(void)
{
	struct task_struct *task;

	asm("SET %0,r230" : "=r" (task));
	return task;
}
#define current get_current()
#endif
