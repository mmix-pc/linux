/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_KERNEL_USER_ENTRY_H
#define _MMIX_KERNEL_USER_ENTRY_H

#include <uapi/asm/rstack.h>
#include "entry.h"
#include "user_rstack.h"

struct mmix_user_entry;

enum mmix_step_state {
	MMIX_STEP_DISARMED,
	MMIX_STEP_IN_FLIGHT,
	MMIX_STEP_REQUESTED,
	MMIX_STEP_REPLAY,
	MMIX_STEP_SYSCALL,
};

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
	unsigned long step_state;
	struct task_struct *step_tracer;
	struct task_struct *step_notification_owner;
	bool step_completed;
	int step_detach_signal;
	long error;
	int fatal_signal;
	bool exit_requested, exit_group;
	bool rstack_sync_pending, rstack_query_pending;
	bool syscall_result;
	bool syscall_entry, syscall_exit_pending, ptrace_redirect;
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

int mmix_step_fetch(struct mmix_user_entry *entry, u32 *insn);
int mmix_step_resume(struct mmix_user_entry *entry, u32 insn);
void mmix_step_complete(struct mmix_user_entry *entry);
void mmix_step_cancel(struct mmix_user_entry *entry);
void mmix_ptrace_publish(struct mmix_user_entry *entry);
int mmix_user_enter(struct mmix_user_rstack_state *stack, const struct mmix_user_entry_ops *ops,
		    void *data);
int mmix_user_syscall(struct mmix_user_entry *entry, void *data);
int mmix_user_fault(struct mmix_user_entry *entry, void *data);
int mmix_user_write(void *data, unsigned long address, const void *source, size_t size);
void mmix_user_run(struct mmix_user_entry *entry);
int mmix_user_dispatch(struct mmix_user_entry *entry);
void mmix_user_capture_entry(void);
void mmix_user_invalid_entry(void);
#ifdef CONFIG_MMIX_USER_TEST
void mmix_user_entry_fail_after(int steps);
#endif

#endif
