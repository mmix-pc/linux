/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_FUTEX_H
#define _ASM_MMIX_FUTEX_H

#ifdef CONFIG_SMP
#include <linux/types.h>
#include <asm/errno.h>

/* The UP preemption-only helpers cannot provide cross-CPU exclusion. */
static inline int arch_futex_atomic_op_inuser(int op, u32 oparg, int *oval,
					    u32 __user *uaddr)
{
	return -ENOSYS;
}

static inline int futex_atomic_cmpxchg_inatomic(u32 *uval, u32 __user *uaddr,
					     u32 oldval, u32 newval)
{
	return -ENOSYS;
}
#else
#include <asm-generic/futex.h>
#endif
#endif
