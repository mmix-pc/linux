/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_KERNEL_SMP_BOOT_H
#define _MMIX_KERNEL_SMP_BOOT_H

#define MMIX_SECONDARY_WAITING	0
#define MMIX_SECONDARY_ENTERED	1
#define MMIX_SECONDARY_PARKED	2
#define MMIX_SECONDARY_STOPPED	3
#define MMIX_SECONDARY_FAILED	4

#ifndef __ASSEMBLER__
#include <asm/cpu_local.h>
#include <linux/cache.h>
#include <linux/init.h>
#include <linux/types.h>

/* Image-owned, never cleared with BSS or reclaimed after a failed release. */
struct mmix_secondary_boot {
	unsigned long fdt;
	unsigned long loader_rs;
	unsigned long claim;
	unsigned long release;
	unsigned long status;
	unsigned long seed;
	unsigned long vector;
	unsigned long root;
	unsigned long ram_end;
	unsigned long sp;
	unsigned long r_o;
	unsigned long r_s;
	unsigned long r_t;
	unsigned long r_tt;
	unsigned long r_v;
	unsigned long r_c;
	unsigned long cpu;
} __aligned(64);

extern struct mmix_secondary_boot mmix_secondary_boot[MMIX_BOOT_CPU_SLOTS];
extern unsigned long mmix_secondary_abort;
void mmix_secondary_start(void);
void mmix_secondary_fault(void);
void __noreturn mmix_secondary_park(void);
void __init mmix_smp_discover(void);
void __noreturn mmix_enter_secondary(unsigned long seed);
void __noreturn mmix_secondary_main(void);
#endif
#endif
