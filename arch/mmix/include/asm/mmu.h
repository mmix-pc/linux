/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_MMU_H
#define _ASM_MMIX_MMU_H

struct mmix_rstack_registry;
#define VM_ARCH_OWNED VM_ARCH_1

typedef struct {
	unsigned long asid;
	struct mmix_rstack_registry *rstacks;
} mm_context_t;
#endif
