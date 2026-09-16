/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_KERNEL_USER_ENTRY_H
#define _MMIX_KERNEL_USER_ENTRY_H

#include <uapi/asm/rstack.h>
#include "entry.h"
#include "user_rstack.h"

struct mmix_user_entry;

enum mmix_user_event {
	MMIX_USER_SYSCALL,
	MMIX_USER_FAULT,
};

/* Callbacks use the owning task's kernel stacks; event runs with IRQs disabled. */
struct mmix_user_entry_ops {
	int (*event)(struct mmix_user_entry *entry, void *data);
	int (*write)(void *data, unsigned long address, const void *source, size_t size);
};

struct mmix_user_entry {
	unsigned long kernel_save;
	unsigned long kernel_c;
	unsigned long shadow_base, shadow_end, shadow_physical;
	unsigned long user_top;
	long error;
	int fatal_signal;
	bool exit_requested, exit_group;
	bool rstack_sync_pending, rstack_query_pending;
	struct mmix_rstack_query rstack_query;
	void __user *rstack_query_output;
	int exit_code;
	struct mmix_user_capture capture;
	struct pt_regs regs;
	struct mmix_user_rstack_state *stack;
	const struct mmix_user_entry_ops *ops;
	void *data;
	unsigned long interrupts;
	enum mmix_user_event event;
};

/* Resident UP selectors; a user-controlled register never selects this storage. */
extern struct mmix_user_entry *mmix_active_user;
extern unsigned long mmix_user_shadow_active;
int mmix_user_enter(struct mmix_user_rstack_state *stack, const struct mmix_user_entry_ops *ops,
		    void *data);
int mmix_user_syscall(struct mmix_user_entry *entry, void *data);
int mmix_user_fault(struct mmix_user_entry *entry, void *data);
int mmix_user_write(void *data, unsigned long address, const void *source, size_t size);
void mmix_user_run(struct mmix_user_entry *entry);
int mmix_user_dispatch(struct mmix_user_entry *entry);
void mmix_user_capture_entry(void);
void mmix_user_invalid_entry(void);

#endif
