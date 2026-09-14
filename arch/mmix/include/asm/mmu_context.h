/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_MMU_CONTEXT_H
#define _ASM_MMIX_MMU_CONTEXT_H

struct task_struct;
struct mm_struct;

void switch_mm(struct mm_struct *prev, struct mm_struct *next,
	       struct task_struct *task);
int init_new_context(struct task_struct *task, struct mm_struct *mm);
#define init_new_context init_new_context
void destroy_context(struct mm_struct *mm);
#define destroy_context destroy_context
#include <asm-generic/mm_hooks.h>
#include <asm-generic/mmu_context.h>
#endif
