/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_PROCESS_H
#define _MMIX_PROCESS_H

#include <asm/boot.h>

/* MMIXware section 43: zero locals, globals, twelve specials, packed rG/rA. */
struct mmix_switch_seed {
	unsigned long locals;
	unsigned long globals[256 - MMIX_BOOT_RG];
	unsigned long specials[12];
	unsigned long ga;
};

struct task_struct;
int mmix_prepare_thread(struct task_struct *task, int (*function)(void *), void *argument);
void __noreturn mmix_ret_from_fork(struct task_struct *prev);
#if defined(CONFIG_MMIX_BOOT_TEST) || defined(CONFIG_MMIX_USER_TEST)
void mmix_rstack_fail_after(int steps);
#endif
#if defined(CONFIG_MMIX_BOOT_TEST) || defined(CONFIG_MMIX_USER_TEST)
unsigned long mmix_rstack_allocated(void);
unsigned long mmix_rstack_released(void);
#endif
#endif
