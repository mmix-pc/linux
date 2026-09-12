/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_PTRACE_H
#define _ASM_MMIX_PTRACE_H

#include <uapi/asm/ptrace.h>

#ifndef __ASSEMBLER__
/* Private entry state; the SAVE image and user register view are distinct. */
struct pt_regs {
	unsigned long regs[256];
	unsigned long pc;
	unsigned long r_g, r_l, r_o, r_s;
	unsigned long r_a, r_b, r_d, r_e, r_h, r_j, r_m, r_p, r_r;
	unsigned long r_w, r_x, r_y, r_z;
	unsigned long r_bb, r_ww, r_xx, r_yy, r_zz;
	unsigned long mask, r_q, r_v, root, save;
};

struct task_struct;
struct pt_regs *task_pt_regs(struct task_struct *task);

#define user_mode(regs) ((long)(regs)->pc >= 0)
#define instruction_pointer(regs) ((regs)->pc)
#define instruction_pointer_set(regs, value) ((regs)->pc = (value))
#define user_stack_pointer(ctx) ((ctx)->regs[254])
#define kernel_stack_pointer(ctx) ((ctx)->regs[254])

static inline unsigned long regs_return_value(struct pt_regs *regs)
{
	return regs->regs[231];
}
#endif
#endif
