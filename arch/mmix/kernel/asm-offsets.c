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
#include "entry.h"
#include "../mm/mmu.h"

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
	DEFINE(MMU_GLOBALS, offsetof(struct mmix_mmu_state, globals));
	DEFINE(MMU_BUSY, offsetof(struct mmix_mmu_state, busy));
	DEFINE(MMU_ROOT, offsetof(struct mmix_mmu_state, root));
	DEFINE(MMU_RAM_END, offsetof(struct mmix_mmu_state, ram_end));
	DEFINE(TASK_STACK, offsetof(struct task_struct, stack));
	DEFINE(ENTRY_BUSY, offsetof(struct mmix_entry_state, busy));
	DEFINE(ENTRY_PHASE, offsetof(struct mmix_entry_state, phase));
	DEFINE(ENTRY_REGS, offsetof(struct mmix_entry_state, regs));
	DEFINE(ENTRY_FAULT_PC, offsetof(struct mmix_entry_state, fault_pc));
	DEFINE(ENTRY_FAULT_VA, offsetof(struct mmix_entry_state, fault_va));
	DEFINE(ENTRY_SIZE, sizeof(struct mmix_entry_state));
	DEFINE(PT_REGS, offsetof(struct pt_regs, regs));
	DEFINE(PT_R_G, offsetof(struct pt_regs, r_g));
	DEFINE(PT_R_L, offsetof(struct pt_regs, r_l));
	DEFINE(PT_R_O, offsetof(struct pt_regs, r_o));
	DEFINE(PT_R_S, offsetof(struct pt_regs, r_s));
	DEFINE(PT_R_A, offsetof(struct pt_regs, r_a));
	DEFINE(PT_R_B, offsetof(struct pt_regs, r_b));
	DEFINE(PT_R_D, offsetof(struct pt_regs, r_d));
	DEFINE(PT_R_E, offsetof(struct pt_regs, r_e));
	DEFINE(PT_R_H, offsetof(struct pt_regs, r_h));
	DEFINE(PT_R_J, offsetof(struct pt_regs, r_j));
	DEFINE(PT_R_M, offsetof(struct pt_regs, r_m));
	DEFINE(PT_R_P, offsetof(struct pt_regs, r_p));
	DEFINE(PT_R_R, offsetof(struct pt_regs, r_r));
	DEFINE(PT_R_W, offsetof(struct pt_regs, r_w));
	DEFINE(PT_R_X, offsetof(struct pt_regs, r_x));
	DEFINE(PT_R_Y, offsetof(struct pt_regs, r_y));
	DEFINE(PT_R_Z, offsetof(struct pt_regs, r_z));
	DEFINE(PT_R_BB, offsetof(struct pt_regs, r_bb));
	DEFINE(PT_R_WW, offsetof(struct pt_regs, r_ww));
	DEFINE(PT_R_XX, offsetof(struct pt_regs, r_xx));
	DEFINE(PT_R_YY, offsetof(struct pt_regs, r_yy));
	DEFINE(PT_R_ZZ, offsetof(struct pt_regs, r_zz));
	DEFINE(PT_R_Q, offsetof(struct pt_regs, r_q));
	DEFINE(PT_R_V, offsetof(struct pt_regs, r_v));
	DEFINE(PT_ROOT, offsetof(struct pt_regs, root));
	return 0;
}
