/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_MMIX_SIGCONTEXT_H
#define _UAPI_ASM_MMIX_SIGCONTEXT_H

#include <asm/ptrace.h>

#ifndef __ASSEMBLY__
struct sigcontext {
	struct user_regs_struct sc_regs;
	struct mmix_user_rstack sc_rstack;
	/* Zero on signal delivery; must be zero on signal return. */
	__u64 reserved[8];
};
#endif

#endif /* _UAPI_ASM_MMIX_SIGCONTEXT_H */
