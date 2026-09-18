// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/uaccess.h>
#include <asm/rstack.h>
#include "process.h"
#include "user_entry.h"
#include "vfork.h"

static int released_child(void *data)
{
	return 0;
}

static void checkpoint_rollback(struct kunit *test)
{
	struct mm_struct *mm = mm_alloc();
	struct mmix_user_rstack_state *stack = mmix_user_rstack_alloc();
	struct mmix_user_rstack_state *old_stack = current->thread.user_state;
	struct mmix_rstack_owner *old_owner = current->thread.rstack_owner;
	struct kernel_clone_args args = {
		.flags = CLONE_VM | CLONE_VFORK,
		.exit_signal = SIGCHLD,
	};
	struct mmix_user_state *state;
	struct mmix_user_entry *entry;
	struct mmix_user_entry *old_entry = current->thread.user_entry;
	__sighandler_t handler = current->sighand->action[SIGCHLD - 1].sa.sa_handler;
	pid_t child;
	int status = 0;
	u64 old_chain = current->thread.rstack_chain, root;
	unsigned long base, value;
	unsigned int i;
	int error;

	if (!mm || !stack) {
		if (mm)
			mmput(mm);
		mmix_user_rstack_free(&stack);
		KUNIT_FAIL(test, "fixture allocation");
		return;
	}
	mm->mmap_base = TASK_UNMAPPED_BASE;
	kthread_use_mm(mm);
	current->thread.rstack_owner = NULL;
	current->thread.user_state = stack;
	error = mmix_rstack_domain_create(mm, 0, &root, &base);
	KUNIT_EXPECT_EQ(test, error, 0);
	if (error)
		goto out;
	current->thread.rstack_chain = root;
	current->thread.rstack_owner = mmix_rstack_owner_alloc(mm, root);
	if (IS_ERR(current->thread.rstack_owner)) {
		current->thread.rstack_owner = NULL;
		KUNIT_FAIL(test, "fixture ownership");
		goto out;
	}
	state = mmix_user_rstack_state(stack);
	state->regs.r_g = 230;
	state->regs.r_o = base + 16;
	state->pending.start = base + 8;
	state->pending.count = 1;
	state->pending.data[0] = 0x7654;
	KUNIT_EXPECT_EQ(test, put_user(0x1234UL, (unsigned long __user *)base), 0);
	for (i = 0; i < 3; i++) {
		mmix_vfork_fail_after(i);
		KUNIT_EXPECT_EQ(test, mmix_vfork_clone(&args), i == 2 ? -EFAULT : -ENOMEM);
		mmix_vfork_fail_after(-1);
		KUNIT_EXPECT_EQ(test, mmix_vfork_live(), 0L);
		KUNIT_EXPECT_PTR_EQ(test, current->thread.vfork_prepare, NULL);
		KUNIT_EXPECT_EQ(test, get_user(value, (unsigned long __user *)base), 0);
		KUNIT_EXPECT_EQ(test, value, 0x1234UL);
	}
	/* Checkpoint and claims succeed, then copy_thread fails before publication. */
	mmix_rstack_fail_after(0);
	KUNIT_EXPECT_EQ(test, mmix_vfork_clone(&args), -ENOMEM);
	mmix_rstack_fail_after(-1);
	KUNIT_EXPECT_EQ(test, mmix_vfork_live(), 0L);
	/* Parent checkpoint claims and child claims each fail before publication. */
	for (i = 0; i < 4; i++) {
		mmix_rstack_domain_fail_after(i);
		KUNIT_EXPECT_EQ(test, mmix_vfork_clone(&args), -ENOMEM);
		mmix_rstack_domain_fail_after(-1);
		KUNIT_EXPECT_EQ(test, mmix_vfork_live(), 0L);
	}
	mmix_user_rstack_fail_after(0);
	KUNIT_EXPECT_EQ(test, mmix_vfork_clone(&args), -ENOMEM);
	mmix_user_rstack_fail_after(-1);
	KUNIT_EXPECT_EQ(test, mmix_vfork_live(), 0L);
	for (i = 0; i < 32; i++)
		KUNIT_EXPECT_EQ(test, mmix_rstack_session_get(mm), 0);
	KUNIT_EXPECT_EQ(test, mmix_vfork_clone(&args), -EAGAIN);
	for (i = 0; i < 32; i++)
		mmix_rstack_session_put(mm);
	/* A published child cannot be reported as an ordinary clone error. */
	entry = kunit_kzalloc(test, sizeof(*entry), GFP_KERNEL);
	if (!entry) {
		KUNIT_FAIL(test, "entry fixture allocation");
		goto out;
	}
	current->thread.user_entry = entry;
	args.fn = released_child;
	kernel_sigaction(SIGCHLD, SIG_DFL);
	mmix_vfork_fail_restore();
	child = mmix_vfork_clone(&args);
	KUNIT_EXPECT_GT(test, child, 0);
	if (child > 0) {
		KUNIT_EXPECT_EQ(test, kernel_wait(child, &status), child);
		KUNIT_EXPECT_EQ(test, status, 0);
	}
	kernel_sigaction(SIGCHLD, handler);
	KUNIT_EXPECT_EQ(test, entry->fatal_signal, SIGSEGV);
	KUNIT_EXPECT_EQ(test, mmix_vfork_live(), 0L);
	current->thread.user_entry = old_entry;
	state->pending.start = base - 8;
	KUNIT_EXPECT_EQ(test, mmix_vfork_clone(&args), -EINVAL);
	KUNIT_EXPECT_EQ(test, mmix_vfork_live(), 0L);
	pr_info("MMIX_VFORK rollback=10 limit=32 state=invalid restore=fatal\n");
out:
	mmix_rstack_detach(current, mm);
	current->thread.rstack_owner = old_owner;
	current->thread.rstack_chain = old_chain;
	current->thread.user_state = old_stack;
	kthread_unuse_mm(mm);
	mmput(mm);
	mmix_user_rstack_free(&stack);
}

static struct kunit_case vfork_cases[] = {
	KUNIT_CASE(checkpoint_rollback),
	{}
};

static struct kunit_suite vfork_suite = {
	.name = "mmix_vfork",
	.test_cases = vfork_cases,
};

kunit_test_suite(vfork_suite);
