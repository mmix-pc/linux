/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_STRING_H
#define _ASM_MMIX_STRING_H

#include <linux/types.h>

#define __HAVE_ARCH_MEMCPY
#define __HAVE_ARCH_MEMSET

void *memcpy(void *dest, const void *src, size_t count);
void *memset(void *dest, int value, size_t count);

#endif
