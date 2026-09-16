/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_MMU_CONTEXT_H
#define _ASM_MMIX_MMU_CONTEXT_H

#include <asm/rstack.h>

struct task_struct;
struct mm_struct;
struct vm_area_struct;

void switch_mm(struct mm_struct *prev, struct mm_struct *next,
	       struct task_struct *task);
int init_new_context(struct task_struct *task, struct mm_struct *mm);
#define init_new_context init_new_context
void destroy_context(struct mm_struct *mm);
#define destroy_context destroy_context

static inline int arch_dup_mmap(struct mm_struct *oldmm, struct mm_struct *mm)
{
	return mmix_rstack_dup_mmap(oldmm, mm);
}

static inline void arch_exit_mmap(struct mm_struct *mm)
{
}

static inline bool arch_vma_access_permitted(struct vm_area_struct *vma,
					     bool write, bool execute, bool foreign)
{
	return true;
}

#include <asm-generic/mmu_context.h>
#endif
