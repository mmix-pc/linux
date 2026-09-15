/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_UACCESS_H
#define _ASM_MMIX_UACCESS_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/errno.h>

/* Byte-granular exception fixups preserve exact uncopied lengths. */
unsigned long raw_copy_from_user(void *to, const void __user *from, unsigned long size);
unsigned long raw_copy_to_user(void __user *to, const void *from, unsigned long size);

unsigned long __clear_user(void __user *to, unsigned long size);
#define __clear_user __clear_user

#include <asm/access_ok.h>
/* A failed scalar read must not expose a partially copied value. */
static inline int __get_user_fn(size_t size, const void __user *ptr, void *value)
{
	if (!raw_copy_from_user(value, ptr, size))
		return 0;
	__builtin_memset(value, 0, size);
	return -EFAULT;
}

#define __get_user_fn(sz, u, k) __get_user_fn(sz, u, k)

#include <asm-generic/uaccess.h>
#endif
