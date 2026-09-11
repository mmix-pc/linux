/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_MMIX_PTRACE_H
#define _UAPI_ASM_MMIX_PTRACE_H

#define MMIX_RSTACK_MAX_WORDS 1024

#ifndef __ASSEMBLY__
#include <linux/types.h>

/* Logical user registers; independent of the kernel's trap frame. */
struct user_regs_struct {
	__u64 regs[256];
	__u64 pc;
	__u64 r_g;
	__u64 r_l;
	__u64 r_o;
	__u64 r_a;
	__u64 r_b;
	__u64 r_d;
	__u64 r_e;
	__u64 r_h;
	__u64 r_j;
	__u64 r_m;
	__u64 r_p;
	__u64 r_r;
	__u64 r_w;
	__u64 r_x;
	__u64 r_y;
	__u64 r_z;
};

/*
 * Older local-register values not yet written to user backing memory.
 * start + count * 8 equals r_o; unused data entries must be zero.
 * This is an address-ordered payload, not a hardware register-ring dump.
 */
struct mmix_user_rstack {
	__u64 start;
	__u64 count;
	__u64 data[MMIX_RSTACK_MAX_WORDS];
};
#endif /* __ASSEMBLY__ */

#endif /* _UAPI_ASM_MMIX_PTRACE_H */
