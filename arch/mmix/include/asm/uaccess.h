/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_UACCESS_H
#define _ASM_MMIX_UACCESS_H

#include <linux/compiler.h>
#include <linux/types.h>

/* Implemented with exception fixups by the user-access entry work. */
unsigned long raw_copy_from_user(void *to, const void __user *from,
				unsigned long size);
unsigned long raw_copy_to_user(void __user *to, const void *from,
			      unsigned long size);

unsigned long __clear_user(void __user *to, unsigned long size);
#define __clear_user __clear_user

#include <asm/access_ok.h>
#include <asm-generic/uaccess.h>
#endif
