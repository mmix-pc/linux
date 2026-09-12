/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_ACCESS_OK_H
#define _ASM_MMIX_ACCESS_OK_H

#include <linux/compiler.h>

/* An address-space bound, not a promise that a userspace mapping exists. */
static inline int __access_ok(const void __user *ptr, unsigned long size)
{
	unsigned long addr = (unsigned long)ptr;
	unsigned long limit = 1UL << 63;

	return size <= limit && addr <= limit - size;
}
#define __access_ok __access_ok
#define access_ok(addr, size) likely(__access_ok(addr, size))
#endif
