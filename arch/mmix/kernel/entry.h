/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_KERNEL_ENTRY_H
#define _MMIX_KERNEL_ENTRY_H

#include <asm/ptrace.h>

/* Resident UP capture slot; detached frames live on the task software stack. */
struct mmix_entry_state {
	unsigned long busy;
	unsigned long phase;
	struct pt_regs regs;
	unsigned long fault_pc;
	unsigned long fault_va;
};
extern struct mmix_entry_state mmix_entry_state;
extern unsigned char mmix_continuation_page[];
void mmix_exception_entry(void);
void mmix_exception_prepare(struct mmix_entry_state *entry, struct pt_regs *regs);
void mmix_exception_finish(struct mmix_entry_state *entry, struct pt_regs *regs);
void mmix_exception_dispatch(struct mmix_entry_state *entry, struct pt_regs *regs);
void __noreturn mmix_exception_fatal(void);
#ifdef CONFIG_MMIX_BOOT_TEST
struct mmix_fault_sample {
	unsigned long address, cause, pc, count;
};

void mmix_boot_fault_sample(struct mmix_fault_sample *sample);
#endif
/* Clear only selected live requests; hardware preserves intervening arrivals. */
static inline void mmix_ack_requests(unsigned long mask)
{
	unsigned long requests;

	asm volatile("GET %0,rQ\n\tANDN %0,%0,%1\n\tPUT rQ,%0"
		     : "=&r" (requests) : "r" (mask) : "memory");
}
#endif
