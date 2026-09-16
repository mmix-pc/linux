/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_RSTACK_H
#define _ASM_MMIX_RSTACK_H

#include <linux/types.h>

struct mm_struct;
struct mmix_rstack_domain;

int mmix_rstack_mm_init(struct mm_struct *mm);
void mmix_rstack_mm_destroy(struct mm_struct *mm);
int mmix_rstack_domain_create(struct mm_struct *mm, u64 parent, u64 *id,
			      unsigned long *base);
int mmix_rstack_domain_release(struct mm_struct *mm, u64 id);
/* Begin/end bracket faultable materialization; no mmap lock is held on success. */
struct mmix_rstack_domain *mmix_rstack_domain_begin(struct mm_struct *mm, u64 id,
						    unsigned long start, size_t len);
void mmix_rstack_domain_end(struct mm_struct *mm, struct mmix_rstack_domain *domain);
int mmix_rstack_dup_mmap(struct mm_struct *oldmm, struct mm_struct *mm);

#ifdef CONFIG_MMIX_BOOT_TEST
void mmix_rstack_domain_fail_after(int step);
#endif

#endif
