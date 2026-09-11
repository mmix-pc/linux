/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_MMIX_UCONTEXT_H
#define _UAPI_ASM_MMIX_UCONTEXT_H

#include <asm/signal.h>
#include <asm/sigcontext.h>

#ifndef __ASSEMBLY__
struct ucontext {
	unsigned long uc_flags;
	struct ucontext *uc_link;
	stack_t uc_stack;
	sigset_t uc_sigmask;
	/* Reserve a 128-byte signal-mask area before the machine context. */
	__u8 __unused[128 - sizeof(sigset_t)];
	struct sigcontext uc_mcontext;
};
#endif

#endif /* _UAPI_ASM_MMIX_UCONTEXT_H */
