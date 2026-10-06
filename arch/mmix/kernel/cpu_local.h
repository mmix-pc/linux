/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_KERNEL_CPU_LOCAL_H
#define _MMIX_KERNEL_CPU_LOCAL_H

#include <asm/cpu_local.h>
#include "entry.h"
#include "../mm/mmu.h"

struct mmix_user_entry;
struct task_struct;

struct mmix_boot_ipi_regs {
	unsigned long r253, sp, r_ww, r_xx, r_yy, r_zz, r_bb, save;
};

struct mmix_cpu_state {
	/* The stackless boot IPI entry needs immediate-offset access using r255. */
	struct mmix_boot_ipi_regs boot_ipi;
	struct mmix_entry_state entry;
	struct mmix_mmu_state mmu;
	struct mmix_user_entry *active_user;
	unsigned long irq_mask;
	unsigned long shadow_active;
	struct task_struct *switch_prev;
	unsigned long id;
#ifdef CONFIG_MMIX_BOOT_TEST
	struct mmix_fault_sample last_fault;
#endif
	unsigned char continuation[PAGE_SIZE] __aligned(PAGE_SIZE);
} __aligned(PAGE_SIZE);

extern struct mmix_cpu_state mmix_cpu_states[MMIX_BOOT_CPU_SLOTS];
extern unsigned char mmix_cpu_vectors[];

/* rT is privileged, CPU-owned and unaffected by SAVE/UNSAVE or user globals. */
static __always_inline struct mmix_cpu_state *mmix_this_cpu(void)
{
	struct mmix_cpu_state *state;

	asm volatile("GET %0,rT\n\tLDOU %0,%0,64" : "=r" (state) :: "memory");
	return state;
}
#endif
