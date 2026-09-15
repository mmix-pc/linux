/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_MMIX_RSTACK_H
#define _UAPI_ASM_MMIX_RSTACK_H

#define MMIX_RSTACK_JUMP_SETMASK	0x1

#ifndef __ASSEMBLY__
#include <linux/types.h>

/* Output of mmix_rstack_query; valid only after a successful return. */
struct mmix_rstack_query {
	__u64 chain_id;
	__u64 sigmask;
};

/*
 * Input to mmix_rstack_jump. Addresses are userspace virtual addresses.
 * result is a sign-extended, nonzero signed 32-bit longjmp result.
 * All flags except MMIX_RSTACK_JUMP_SETMASK must be zero. sigmask must
 * be zero unless that flag is set. Success transfers control to landing.
 */
struct mmix_rstack_jump {
	__u64 chain_id;
	__u64 landing;
	__u64 environment;
	__u64 result;
	__u64 flags;
	__u64 sigmask;
};
#endif /* !__ASSEMBLY__ */

#endif /* _UAPI_ASM_MMIX_RSTACK_H */
