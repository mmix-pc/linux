/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_BOOT_H
#define _ASM_MMIX_BOOT_H

#define MMIX_BOOT_RG		230
#define MMIX_BOOT_STACK_SIZE	32768
#define MMIX_BOOT_RSTACK_SIZE	65536
#define MMIX_BOOT_FDT_MAX		0x200000
#define MMIX_BOOT_UART		0x8001000010000000

#ifndef __ASSEMBLER__
#include <linux/init.h>
#include <linux/types.h>

/* Validated platform information, published before the MMU handoff. */
struct mmix_boot_info {
	unsigned long fdt;
	unsigned long cpu;
	unsigned long ram_size;
	unsigned long fdt_size;
	unsigned long stack_base;
	unsigned long stack_size;
	unsigned long uart;
};

/* Call only after mmix_early_boot has validated and published the information. */
const struct mmix_boot_info * __init mmix_get_boot_info(void);
extern char mmix_boot_stack[], mmix_boot_stack_end[];
extern char mmix_boot_rstack[], mmix_boot_rstack_end[];
extern char __boot_start[], __boot_end[];

void __init __noreturn mmix_early_boot(void);
void __init __noreturn mmix_boot_fail(const char *reason);
void __init mmix_boot_puts(const char *text);

/* Supplied by memory setup: install refill and guarded stacks, then start Linux. */
void __init __noreturn mmix_boot_mmu(void);
void __init paging_init(void);
#endif
#endif
