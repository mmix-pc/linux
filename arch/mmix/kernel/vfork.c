// SPDX-License-Identifier: GPL-2.0-only
#include <linux/completion.h>
#include <linux/mm.h>
#include <linux/refcount.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <asm/rstack.h>
#include "signal.h"
#include "user_entry.h"
#include "vfork.h"

#define VFORK_MAX_PREFIXES 33

struct mmix_vfork_prefix {
	u64 chain;
	unsigned long base;
	size_t length;
	void *data;
};

struct mmix_vfork {
	/* Each reference is backed by a task still holding this mm. */
	refcount_t refs;
	atomic_t users;
	struct completion quiet;
	struct mm_struct *mm;
	struct mmix_rstack_owner *owner;
	struct mmix_vfork *ancestor;
	unsigned int count;
	struct mmix_vfork_prefix prefix[VFORK_MAX_PREFIXES];
};

#if defined(CONFIG_MMIX_BOOT_TEST) || defined(CONFIG_MMIX_USER_TEST)
static atomic_long_t live_sessions = ATOMIC_LONG_INIT(0);

long mmix_vfork_live(void)
{
	return atomic_long_read(&live_sessions);
}
#endif

static void account_session(int delta)
{
#if defined(CONFIG_MMIX_BOOT_TEST) || defined(CONFIG_MMIX_USER_TEST)
	atomic_long_add(delta, &live_sessions);
#endif
}

#ifdef CONFIG_MMIX_BOOT_TEST
static atomic_t allocation_failure = ATOMIC_INIT(-1);
static atomic_t restore_failure = ATOMIC_INIT(0);

void mmix_vfork_fail_restore(void)
{
	atomic_set(&restore_failure, 1);
}

void mmix_vfork_fail_after(int step)
{
	atomic_set(&allocation_failure, step);
}

static bool vfork_fail(void)
{
	return atomic_read(&allocation_failure) >= 0 &&
		atomic_dec_return(&allocation_failure) < 0;
}
#else
static bool vfork_fail(void)
{
	return false;
}
#endif

static void vfork_put(struct mmix_vfork *session)
{
	unsigned int i;

	if (!refcount_dec_and_test(&session->refs))
		return;
	for (i = 0; i < session->count; i++)
		kvfree(session->prefix[i].data);
	mmix_rstack_owner_free(session->owner);
	mmix_rstack_session_put(session->mm);
	account_session(-1);
	kfree(session);
}

/* A dead intermediate child must not expose a still-running grandchild. */
static void vfork_leave(struct mmix_vfork *session)
{
	struct mmix_vfork *ancestor;

	while (atomic_dec_and_test(&session->users)) {
		ancestor = session->ancestor;
		session->ancestor = NULL;
		complete_all(&session->quiet);
		if (!ancestor)
			break;
		/* The extra reference belongs to the descendant's live-user edge. */
		vfork_put(session);
		session = ancestor;
	}
	vfork_put(session);
}

void mmix_vfork_detach(struct task_struct *task, struct mm_struct *mm)
{
	struct mmix_vfork *session = task->thread.vfork_child;

	if (!session || session->mm != mm)
		return;
	task->thread.vfork_child = NULL;
	vfork_leave(session);
}

int mmix_vfork_attach(struct task_struct *task)
{
	struct mmix_vfork *session = current->thread.vfork_prepare;

	if (!session || session->mm != task->mm || task->thread.vfork_child)
		return -EINVAL;
	refcount_inc(&session->refs);
	atomic_inc(&session->users);
	task->thread.vfork_child = session;
	return 0;
}

static int capture_prefix(void *data, u64 chain, u64 parent,
			  const struct mmix_user_state *state)
{
	struct mmix_vfork *session = data;
	struct mmix_vfork_prefix *prefix;
	struct mmix_rstack_domain *domain;
	unsigned long base;
	int error;

	if (session->count == VFORK_MAX_PREFIXES || mmix_user_rstack_validate(state))
		return -EINVAL;
	error = mmix_rstack_prefix(session->mm, chain, parent, state->regs.r_o, &base);
	if (error || state->pending.start < base)
		return -EINVAL;
	prefix = &session->prefix[session->count++];
	prefix->chain = chain;
	prefix->base = base;
	prefix->length = state->regs.r_o - base;
	if (!prefix->length)
		return 0;
	if (vfork_fail())
		return -ENOMEM;
	prefix->data = kvmalloc(prefix->length, GFP_KERNEL_ACCOUNT);
	if (!prefix->data)
		return -ENOMEM;
	domain = mmix_rstack_domain_begin(session->mm, chain, base, prefix->length);
	if (IS_ERR(domain))
		return PTR_ERR(domain);
	/* Pending words are logical state, including values not yet in memory. */
	error = vfork_fail() || copy_from_user(prefix->data, (void __user *)base,
					       state->pending.start - base) ? -EFAULT : 0;
	if (!error)
		memcpy(prefix->data + state->pending.start - base, state->pending.data,
		       state->pending.count * sizeof(unsigned long));
	mmix_rstack_domain_end(session->mm, domain);
	return error;
}

static int restore_prefixes(struct mmix_vfork *session)
{
	struct mmix_rstack_domain *domain;
	unsigned int i;
	int error;

	for (i = 0; i < session->count; i++) {
		struct mmix_vfork_prefix *prefix = &session->prefix[i];

#ifdef CONFIG_MMIX_BOOT_TEST
		if (atomic_xchg(&restore_failure, 0))
			return -EFAULT;
#endif
		if (!prefix->length)
			continue;
		domain = mmix_rstack_domain_begin(session->mm, prefix->chain,
						  prefix->base, prefix->length);
		if (IS_ERR(domain))
			return PTR_ERR(domain);
		error = copy_to_user((void __user *)prefix->base, prefix->data,
				     prefix->length) ? -EFAULT : 0;
		mmix_rstack_domain_end(session->mm, domain);
		if (error)
			return error;
	}
	return 0;
}

long mmix_vfork_clone(struct kernel_clone_args *args)
{
	struct mmix_vfork *session, *ancestor = current->thread.vfork_child;
	struct mm_struct *mm = current->mm;
	long result;
	int error;

	if (!current->thread.user_state || !current->thread.rstack_owner ||
	    current->thread.vfork_prepare)
		return -EINVAL;
	error = mmix_rstack_session_get(mm);
	if (error)
		return error;
	session = vfork_fail() ? NULL : kzalloc(sizeof(*session), GFP_KERNEL_ACCOUNT);
	if (!session) {
		mmix_rstack_session_put(mm);
		return -ENOMEM;
	}
	account_session(1);
	refcount_set(&session->refs, 1);
	atomic_set(&session->users, 0);
	init_completion(&session->quiet);
	session->mm = mm;
	session->owner = mmix_rstack_owner_alloc(mm, current->thread.rstack_chain);
	if (IS_ERR(session->owner)) {
		result = PTR_ERR(session->owner);
		session->owner = NULL;
		goto out;
	}
	result = mmix_signal_walk(capture_prefix, session);
	if (result)
		goto out;
	if (ancestor) {
		refcount_inc(&ancestor->refs);
		atomic_inc(&ancestor->users);
		session->ancestor = ancestor;
	}
	/* A preparation user covers failure before copy_thread attaches a child. */
	refcount_inc(&session->refs);
	atomic_inc(&session->users);
	current->thread.vfork_prepare = session;
	result = kernel_clone(args);
	current->thread.vfork_prepare = NULL;
	vfork_leave(session);
	if (result > 0 && !fatal_signal_pending(current)) {
		/* Generic completion covers the direct child, not orphaned descendants. */
		error = wait_for_completion_killable(&session->quiet);
		if (!error && !fatal_signal_pending(current) && restore_prefixes(session))
			/* Leave through user entry cleanup, without publishing a result. */
			current->thread.user_entry->fatal_signal = SIGSEGV;
	}
out:
	vfork_put(session);
	return result;
}
