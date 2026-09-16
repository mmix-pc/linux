/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_USER_RSTACK_H
#define _MMIX_USER_RSTACK_H

#include <linux/types.h>
#include <asm/page.h>
#include <uapi/asm/ptrace.h>

#define MMIX_USER_SAVE_WORDS 40
#define MMIX_USER_SHADOW_PAGES 3

/* Trusted entry metadata, not a user-supplied SAVE pointer or public ABI. */
struct mmix_user_capture {
	unsigned long r_g, r_l, r_o, r_s;
	unsigned long start; /* First pending word, including a diverted prefix. */
};

struct mmix_user_state {
	struct user_regs_struct regs;
	struct mmix_user_rstack pending;
};

struct mmix_user_rstack_state;
struct task_struct;

struct mmix_user_rstack_state *mmix_user_rstack_alloc(void);
void mmix_user_rstack_free(struct mmix_user_rstack_state **owner);
struct mmix_user_rstack_state *mmix_user_rstack_dup(const struct mmix_user_rstack_state *source);
/* Accessed only while the owner is quiescent on a trusted kernel stack. */
struct mmix_user_state *mmix_user_rstack_state(struct mmix_user_rstack_state *stack);
void *mmix_user_rstack_shadow(struct mmix_user_rstack_state *stack);
void *mmix_user_rstack_continuation(struct mmix_user_rstack_state *stack);
int mmix_user_rstack_window(const struct mmix_user_capture *capture, unsigned long *base,
			    unsigned long *size);
int mmix_user_rstack_capture(struct mmix_user_rstack_state *stack,
			     const struct mmix_user_capture *capture, unsigned long pc);
int mmix_user_rstack_validate(const struct mmix_user_state *state);
int mmix_user_rstack_materialize(struct mmix_user_rstack_state *stack,
				 int (*write)(void *, unsigned long, const void *, size_t),
				 void *arg);
int mmix_user_rstack_prepare(struct mmix_user_rstack_state *stack,
			     unsigned long *base, unsigned long *top);
int mmix_user_rstack_restore(struct mmix_user_rstack_state *stack,
			     int (*write)(void *arg, unsigned long address, const void *data,
					  size_t size),
			     void *arg, unsigned long *base, unsigned long *top);
#ifdef CONFIG_MMIX_BOOT_TEST
void mmix_user_rstack_fail_after(int step);
void mmix_user_rstack_write_limit(long bytes);
#endif
#if defined(CONFIG_MMIX_BOOT_TEST) || defined(CONFIG_MMIX_USER_TEST)
long mmix_user_rstack_live(void);
#endif
#endif
