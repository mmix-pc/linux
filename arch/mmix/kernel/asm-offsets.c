// SPDX-License-Identifier: GPL-2.0-only
#define COMPILE_OFFSETS
#include <linux/kbuild.h>
#include <linux/stddef.h>
#include <linux/sched.h>
#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/thread_info.h>
#include <asm/boot.h>
#include "boot.h"

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
	DEFINE(BOOT_FDT, offsetof(struct mmix_boot_handoff, fdt));
	DEFINE(BOOT_CPU, offsetof(struct mmix_boot_handoff, cpu));
	DEFINE(BOOT_LOADER_RS, offsetof(struct mmix_boot_handoff, loader_rs));
	DEFINE(BOOT_HANDOFF_SIZE, sizeof(struct mmix_boot_handoff));
	DEFINE(BOOT_FAULT_SP, offsetof(struct mmix_boot_fault, sp));
	DEFINE(BOOT_FAULT_PC, offsetof(struct mmix_boot_fault, pc));
	DEFINE(BOOT_FAULT_XX, offsetof(struct mmix_boot_fault, r_xx));
	DEFINE(BOOT_FAULT_YY, offsetof(struct mmix_boot_fault, r_yy));
	DEFINE(BOOT_FAULT_ZZ, offsetof(struct mmix_boot_fault, r_zz));
	DEFINE(BOOT_FAULT_RO, offsetof(struct mmix_boot_fault, r_o));
	DEFINE(BOOT_FAULT_RS, offsetof(struct mmix_boot_fault, r_s));
	DEFINE(BOOT_FAULT_BB, offsetof(struct mmix_boot_fault, r_bb));
	DEFINE(BOOT_FAULT_SIZE, sizeof(struct mmix_boot_fault));
	/* Zero-local count, globals and specials preceding the packed rG/rA. */
	DEFINE(BOOT_SEED_TOP, (1 + 256 - MMIX_BOOT_RG + 12) * sizeof(unsigned long));
	return 0;
}
