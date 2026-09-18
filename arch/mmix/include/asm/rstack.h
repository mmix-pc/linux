/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_RSTACK_H
#define _ASM_MMIX_RSTACK_H

#include <linux/types.h>

struct mm_struct;
struct mmix_rstack_domain;
struct mmix_rstack_owner;
struct task_struct;

/* Private continuation claims; the caller keeps mm alive until detach. */
struct mmix_rstack_owner *mmix_rstack_owner_alloc(struct mm_struct *mm, u64 chain);
int mmix_rstack_owner_free(struct mmix_rstack_owner *owner);
void mmix_rstack_detach(struct task_struct *task, struct mm_struct *mm);

int mmix_rstack_session_get(struct mm_struct *mm);
void mmix_rstack_session_put(struct mm_struct *mm);
int mmix_rstack_prefix(struct mm_struct *mm, u64 id, u64 parent,
		       unsigned long top, unsigned long *base);
void mmix_vfork_detach(struct task_struct *task, struct mm_struct *mm);

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

#ifdef CONFIG_MMIX_USER_TEST
long mmix_rstack_domains_live(void);
#endif

#ifdef CONFIG_MMIX_BOOT_TEST
void mmix_rstack_domain_fail_after(int step);
void mmix_rstack_domain_fail_release(void);
#endif

#endif
