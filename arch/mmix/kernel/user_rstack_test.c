// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/mm.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include "user_rstack.h"
#include "user_rstack_test.h"

static void release_test_stack(void *pointer)
{
	struct mmix_user_rstack_state *stack = pointer;

	mmix_user_rstack_free(&stack);
}

static int discard_write(void *arg, unsigned long address, const void *data, size_t size)
{
	unsigned long *count = arg;

	*count += size / sizeof(unsigned long);
	return 0;
}

static int failed_write(void *arg, unsigned long address, const void *data, size_t size)
{
	memcpy(arg, data, 2 * sizeof(unsigned long));
	return -EFAULT;
}

static void user_rstack_hardware(struct kunit *test)
{
	struct mmix_user_rstack_state *stack = mmix_user_rstack_alloc();
	struct mmix_test_save *f = kunit_kzalloc(test, sizeof(*f), GFP_KERNEL);
	struct mmix_user_capture c;
	struct mmix_user_state *s;
	unsigned long flags, base, size, top, count = 0;
	unsigned long result, i;
	void *shadow;

	KUNIT_ASSERT_NOT_NULL(test, stack);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, release_test_stack, stack), 0);
	KUNIT_ASSERT_NOT_NULL(test, f);
	local_irq_save(flags);
	result = mmix_test_user_save(f);
	local_irq_restore(flags);
	KUNIT_EXPECT_EQ(test, result, 0UL);
	KUNIT_ASSERT_EQ(test, f->r_l, 30UL);
	KUNIT_ASSERT_EQ(test, f->r_g, 230UL);
	/* 64 recursive frames of 32 octas exceed the largest supported ring. */
	KUNIT_EXPECT_GT(test, f->r_o - f->base, 8192UL);
	c.r_g = f->r_g;
	c.r_l = f->r_l;
	c.r_s = 0x600008;
	c.r_o = c.r_s + f->r_o - f->r_s;
	c.start = c.r_s;
	KUNIT_ASSERT_EQ(test, mmix_user_rstack_window(&c, &base, &size), 0);
	shadow = mmix_user_rstack_shadow(stack);
	memcpy(shadow + c.r_s - base, f->data, f->top + 8 - f->r_s);
	KUNIT_ASSERT_EQ(test, mmix_user_rstack_capture(stack, &c, 0x400000), 0);
	s = mmix_user_rstack_state(stack);
	KUNIT_EXPECT_EQ(test, s->regs.regs[0], 0UL);
	KUNIT_EXPECT_EQ(test, s->regs.regs[29], 0x55UL);
	KUNIT_EXPECT_EQ(test, s->regs.regs[230], 0x2345ULL);
	KUNIT_EXPECT_EQ(test, s->regs.r_a, 0x30000ULL);
	KUNIT_EXPECT_EQ(test, s->regs.r_h, 0x6789ULL);
	KUNIT_EXPECT_EQ(test, s->regs.r_m, 0x1234ULL);
	KUNIT_EXPECT_EQ(test, s->regs.r_r, 0x45ULL);
	KUNIT_EXPECT_EQ(test, s->regs.r_p, 0x5aULL);
	KUNIT_EXPECT_EQ(test, s->regs.r_d, 0x77ULL);
	KUNIT_EXPECT_EQ(test,
			memcmp(s->pending.data, f->data, s->pending.count * sizeof(unsigned long)),
			0);
	/* The owned capture must also survive suspension of its kernel caller. */
	schedule_timeout_uninterruptible(1);
	/* Restore admission checks user SP separately from captured raw values. */
	s->regs.regs[254] = MMIX_TASK_SIZE;
	KUNIT_ASSERT_EQ(test, mmix_user_rstack_restore(stack, discard_write, &count, &base, &top),
			0);
	KUNIT_EXPECT_EQ(test, count, s->pending.count);
	local_irq_save(flags);
	result = mmix_test_user_restore((unsigned long)shadow + top - base);
	local_irq_restore(flags);
	KUNIT_EXPECT_EQ(test, result, 0UL);
	/* An unavailable user address is never dereferenced during conversion. */
	for (i = 0; i < s->pending.count; i++)
		KUNIT_EXPECT_EQ(test, s->pending.data[i], f->data[i]);
	kunit_info(test, "MMIX_RSTACK hardware depth=%lu pending=%llu restored=%lu\n",
		   (f->r_o - f->base) / 8, s->pending.count, result);
}

static void user_rstack_validation(struct kunit *test)
{
	struct mmix_user_rstack_state *stack = mmix_user_rstack_alloc();
	struct mmix_user_state *s;
	struct mmix_user_capture c = { 230, 0, 0x602000, 0x602000, 0x600000 };
	unsigned long base = 1, top = 2, size, partial[2] = {};
	unsigned long *shadow, *rc;
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, stack);
	KUNIT_ASSERT_EQ(test, kunit_add_action_or_reset(test, release_test_stack, stack), 0);
	s = mmix_user_rstack_state(stack);
	shadow = mmix_user_rstack_shadow(stack);
	rc = mmix_user_rstack_continuation(stack);
	KUNIT_ASSERT_EQ(test, mmix_user_rstack_window(&c, &base, &size), 0);
	for (i = 0; i < 1024; i++)
		rc[i] = i ^ 0xabcdefUL;
	shadow[(c.r_o - base) / 8 + 39] = 230UL << 56;
	KUNIT_ASSERT_EQ(test, mmix_user_rstack_capture(stack, &c, 0x400000), 0);
	KUNIT_EXPECT_EQ(test, s->pending.count, 1024ULL);
	for (i = 0; i < 1024; i++)
		KUNIT_EXPECT_EQ(test, s->pending.data[i], (unsigned long long)(i ^ 0xabcdefUL));
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_validate(s), 0);
	base = 1;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_restore(stack, failed_write, partial, &base, &top),
			-EFAULT);
	KUNIT_EXPECT_EQ(test, base, 1UL);
	KUNIT_EXPECT_EQ(test, top, 2UL);
	KUNIT_EXPECT_EQ(test, partial[0], 0xabcdefUL);
	KUNIT_EXPECT_EQ(test, partial[1], 0xabcdeeUL);
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_restore(stack, NULL, NULL, &base, &top), -EINVAL);
	/* Invalid capture must not partially publish state. */
	c.r_g = 229;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_capture(stack, &c, 0x400000), -EINVAL);
	KUNIT_EXPECT_EQ(test, s->pending.count, 1024ULL);
	c.r_g = 230;
	c.r_l = 231;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_window(&c, &base, &size), -EINVAL);
	c.r_l = 0;
	c.start = 0x5ffff8;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_window(&c, &base, &size), -EINVAL);
	c.start = MMIX_TASK_SIZE - 8;
	c.r_s = c.start;
	c.r_o = c.start;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_window(&c, &base, &size), -EINVAL);
	s->regs.r_g = 229;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_validate(s), -EINVAL);
	s->regs.r_g = 230;
	s->regs.regs[1] = 1;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_validate(s), -EINVAL);
	s->regs.regs[1] = 0;
	s->pending.count = 1025;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_validate(s), -EINVAL);
	s->pending.count = 1024;
	s->regs.r_a = 1UL << 18;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_validate(s), -EINVAL);
	s->regs.r_a = 0;
	s->regs.pc = PAGE_OFFSET;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_validate(s), -EINVAL);
	s->regs.pc = 0x400002;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_validate(s), -EINVAL);
	s->regs.pc = 0x400000;
	s->regs.regs[254] = PAGE_OFFSET;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_validate(s), -EINVAL);
	s->regs.regs[254] = 7;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_validate(s), -EINVAL);
	s->regs.regs[254] = 0;
	s->pending.start = s->regs.r_o;
	s->pending.count = 0;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_validate(s), -EINVAL);
	memset(&s->pending.data, 0, sizeof(s->pending.data));
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_validate(s), 0);
	/* Largest unaligned window uses three pages; the fourth is never touched. */
	c = (struct mmix_user_capture){ 230, 230, 0x603ff8, 0x601ff8, 0x601ff8 };
	KUNIT_ASSERT_EQ(test, mmix_user_rstack_window(&c, &base, &size), 0);
	KUNIT_EXPECT_EQ(test, size, 3UL * PAGE_SIZE);
	memset(shadow, 0, 4 * PAGE_SIZE);
	shadow[(c.r_o - base) / 8 + c.r_l] = c.r_l;
	shadow[(c.r_o - base) / 8 + c.r_l + 39] = 230UL << 56;
	shadow[3 * PAGE_SIZE / 8] = 0x12345678;
	KUNIT_ASSERT_EQ(test, mmix_user_rstack_capture(stack, &c, 0x400000), 0);
	KUNIT_EXPECT_EQ(test, s->pending.count, 1024ULL);
	KUNIT_EXPECT_EQ(test, s->regs.r_l, 230ULL);
	KUNIT_EXPECT_EQ(test, shadow[3 * PAGE_SIZE / 8], 0x12345678UL);
	shadow[(c.r_o - base) / 8 + c.r_l] = 229;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_capture(stack, &c, 0x400000), -EINVAL);
	KUNIT_EXPECT_EQ(test, s->regs.r_l, 230ULL);
	shadow[(c.r_o - base) / 8 + c.r_l] = 230;
	shadow[(c.r_o - base) / 8 + c.r_l + 39] |= 1UL << 20;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_capture(stack, &c, 0x400000), -EINVAL);
	c.r_s++;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_window(&c, &base, &size), -EINVAL);
	c.r_s = PAGE_OFFSET;
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_window(&c, &base, &size), -EINVAL);
	/* Merge a page-tail continuation prefix with still-resident older words. */
	c = (struct mmix_user_capture){ 230, 0, 0x602048, 0x602000, 0x601ff0 };
	KUNIT_ASSERT_EQ(test, mmix_user_rstack_window(&c, &base, &size), 0);
	memset(shadow, 0, 3 * PAGE_SIZE);
	rc[1022] = 0x1122;
	rc[1023] = 0x3344;
	for (i = 0; i < 9; i++)
		shadow[(c.r_s - base) / 8 + i] = 0x5500 + i;
	shadow[(c.r_o - base) / 8 + 39] = 230UL << 56;
	KUNIT_ASSERT_EQ(test, mmix_user_rstack_capture(stack, &c, 0x400000), 0);
	KUNIT_EXPECT_EQ(test, s->pending.count, 11ULL);
	KUNIT_EXPECT_EQ(test, s->pending.data[0], 0x1122ULL);
	KUNIT_EXPECT_EQ(test, s->pending.data[1], 0x3344ULL);
	for (i = 0; i < 9; i++)
		KUNIT_EXPECT_EQ(test, s->pending.data[i + 2], (unsigned long long)(0x5500 + i));
}

static int snapshot_worker(void *unused)
{
	return 0;
}

static void user_rstack_ownership(struct kunit *test)
{
	struct mmix_user_rstack_state *stack, *child;
	struct mmix_user_state *s;
	long before = mmix_user_rstack_live();
	unsigned int round, step;
	struct task_struct *task;

	for (round = 0; round < 16; round++) {
		for (step = 0; step < 3; step++) {
			mmix_user_rstack_fail_after(step);
			stack = mmix_user_rstack_alloc();
			KUNIT_EXPECT_PTR_EQ(test, stack, NULL);
			mmix_user_rstack_free(&stack);
			KUNIT_EXPECT_EQ(test, mmix_user_rstack_live(), before);
		}
		mmix_user_rstack_fail_after(-1);
		stack = mmix_user_rstack_alloc();
		KUNIT_ASSERT_NOT_NULL(test, stack);
		s = mmix_user_rstack_state(stack);
		s->regs.r_g = 230;
		s->regs.r_o = 0x600000;
		s->pending.start = s->regs.r_o;
		s->regs.regs[230] = 0x2345;
		for (step = 0; step < 3; step++) {
			mmix_user_rstack_fail_after(step);
			child = mmix_user_rstack_dup(stack);
			KUNIT_EXPECT_PTR_EQ(test, child, NULL);
			mmix_user_rstack_free(&child);
			KUNIT_EXPECT_EQ(test, mmix_user_rstack_live(), before + 1);
			KUNIT_EXPECT_EQ(test, s->regs.regs[230], 0x2345ULL);
		}
		mmix_user_rstack_fail_after(-1);
		child = mmix_user_rstack_dup(stack);
		if (!child) {
			mmix_user_rstack_free(&stack);
			KUNIT_FAIL(test, "private snapshot duplication");
			return;
		}
		KUNIT_EXPECT_EQ(test, memcmp(mmix_user_rstack_state(child), s, sizeof(*s)), 0);
		s->regs.regs[230] = 0;
		KUNIT_EXPECT_EQ(test, mmix_user_rstack_state(child)->regs.regs[230], 0x2345ULL);
		mmix_user_rstack_free(&stack);
		mmix_user_rstack_free(&stack);
		mmix_user_rstack_free(&child);
		KUNIT_EXPECT_EQ(test, mmix_user_rstack_live(), before);
	}
	/* The final task reference, not kthread_stop alone, owns snapshot release. */
	task = kthread_create(snapshot_worker, NULL, "mmix-snapshot");
	KUNIT_ASSERT_FALSE(test, IS_ERR(task));
	get_task_struct(task);
	task->thread.user_state = mmix_user_rstack_alloc();
	KUNIT_EXPECT_NOT_NULL(test, task->thread.user_state);
	kthread_stop(task);
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_live(), before + 1);
	put_task_struct(task);
	for (step = 0; step < 100 && mmix_user_rstack_live() != before; step++)
		usleep_range(10000, 11000);
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_live(), before);
	KUNIT_ASSERT_PTR_EQ(test, current->thread.user_state, NULL);
	current->thread.user_state = mmix_user_rstack_alloc();
	KUNIT_EXPECT_NOT_NULL(test, current->thread.user_state);
	flush_thread();
	KUNIT_EXPECT_PTR_EQ(test, current->thread.user_state, NULL);
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_live(), before);
}

static struct kunit_case user_rstack_cases[] = { KUNIT_CASE(user_rstack_hardware),
						 KUNIT_CASE(user_rstack_validation),
						 KUNIT_CASE(user_rstack_ownership),
						 {} };

static struct kunit_suite user_rstack_suite = {
	.name = "mmix_user_rstack",
	.test_cases = user_rstack_cases,
};

kunit_test_suite(user_rstack_suite);
