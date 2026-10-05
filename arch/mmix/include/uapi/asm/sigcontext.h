/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_MMIX_SIGCONTEXT_H
#define _UAPI_ASM_MMIX_SIGCONTEXT_H

#include <asm/ptrace.h>

#define MMIX_SIGCONTEXT_HAS_CHAIN_ID 1

#ifndef __ASSEMBLY__
struct sigcontext {
	struct user_regs_struct sc_regs;
	struct mmix_user_rstack sc_rstack;
	/* Interrupted chain; must remain unchanged on signal return. */
	__u64 sc_chain_id;
	/* Zero on signal delivery; must be zero on signal return. */
	__u64 reserved[7];
};
#endif

#endif /* _UAPI_ASM_MMIX_SIGCONTEXT_H */
