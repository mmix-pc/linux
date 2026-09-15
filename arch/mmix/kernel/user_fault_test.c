// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/kthread.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/task.h>
#include <linux/sched/signal.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include "user_entry.h"
#include "user_entry_test.h"

#define DATA (8UL << 20)
#define CODE (4UL << 20)
#define STACK (6UL << 20)

static unsigned long map_region(unsigned long address, unsigned long size, unsigned long prot)
{
	return vm_mmap(NULL, address, size, prot, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0);
}

static struct mm_struct *use_test_mm(void)
{
	struct mm_struct *mm = mm_alloc();

	if (mm)
		kthread_use_mm(mm);
	return mm;
}

static void drop_test_mm(struct mm_struct *mm)
{
	kthread_unuse_mm(mm);
	mmput(mm);
}

static void user_access(struct kunit *test)
{
	struct mm_struct *mm = use_test_mm();
	unsigned char source[32], result[32];
	unsigned long value, left;
	unsigned long edge = DATA + PAGE_SIZE - 16;
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, mm);
	if (map_region(DATA, PAGE_SIZE, PROT_READ | PROT_WRITE) != DATA) {
		KUNIT_FAIL(test, "map demand page");
		goto out;
	}
	memset(source, 0x5a, sizeof(source));
	memset(result, 0xff, sizeof(result));
	KUNIT_EXPECT_EQ(test, copy_from_user(result, (void __user *)DATA, sizeof(result)), 0UL);
	KUNIT_EXPECT_PTR_EQ(test, memchr_inv(result, 0, sizeof(result)), NULL);
	KUNIT_EXPECT_EQ(test, put_user(0x123456789abcdef0UL, (unsigned long __user *)DATA), 0);
	KUNIT_EXPECT_EQ(test, get_user(value, (unsigned long __user *)DATA), 0);
	KUNIT_EXPECT_EQ(test, value, 0x123456789abcdef0UL);
	left = copy_to_user((void __user *)edge, source, sizeof(source));
	KUNIT_EXPECT_EQ(test, left, 16UL);
	memset(result, 0xff, sizeof(result));
	left = copy_from_user(result, (void __user *)edge, sizeof(result));
	KUNIT_EXPECT_EQ(test, left, 16UL);
	for (i = 0; i < 32; i++)
		KUNIT_EXPECT_EQ(test, result[i], (unsigned char)(i < 16 ? 0x5a : 0));
	value = ~0UL;
	KUNIT_EXPECT_EQ(test, get_user(value, (unsigned long __user *)(DATA + PAGE_SIZE - 4)),
			-EFAULT);
	KUNIT_EXPECT_EQ(test, value, 0UL);
	KUNIT_EXPECT_EQ(test, put_user(0xabcdefUL, (unsigned long __user *)(DATA + PAGE_SIZE - 4)),
			-EFAULT);
	KUNIT_EXPECT_EQ(test, clear_user((void __user *)edge, 32), 16UL);
	KUNIT_EXPECT_EQ(test, copy_from_user(result, (void __user *)edge, 16), 0UL);
	KUNIT_EXPECT_PTR_EQ(test, memchr_inv(result, 0, 16), NULL);
	KUNIT_EXPECT_EQ(test, map_region(DATA + 2 * PAGE_SIZE, PAGE_SIZE, PROT_READ),
			DATA + 2 * PAGE_SIZE);
	KUNIT_EXPECT_EQ(test, put_user(1UL, (unsigned long __user *)(DATA + 2 * PAGE_SIZE)),
			-EFAULT);
	KUNIT_EXPECT_EQ(test, get_user(value, (unsigned long __user *)0), -EFAULT);
	KUNIT_EXPECT_EQ(test, get_user(value, (unsigned long __user *)(1UL << 63)), -EFAULT);
	KUNIT_EXPECT_EQ(test, get_user(value, (unsigned long __user *)VMALLOC_START), -EFAULT);
	KUNIT_EXPECT_EQ(test, value, 0UL);
	KUNIT_EXPECT_EQ(test, copy_to_user((void __user *)(MMIX_TASK_SIZE - 8), source, 32), 32UL);
	KUNIT_EXPECT_EQ(test, clear_user((void __user *)(~0UL - 8), 32), 32UL);
	/* Fault-disabled demand access must fail instead of sleeping or mapping. */
	KUNIT_EXPECT_EQ(test, map_region(DATA + 4 * PAGE_SIZE, PAGE_SIZE, PROT_READ | PROT_WRITE),
			DATA + 4 * PAGE_SIZE);
	pagefault_disable();
	left = raw_copy_to_user((void __user *)(DATA + 4 * PAGE_SIZE), source, 1);
	pagefault_enable();
	KUNIT_EXPECT_EQ(test, left, 1UL);
	KUNIT_EXPECT_EQ(test, copy_to_user((void __user *)(DATA + 4 * PAGE_SIZE), source, 1), 0UL);
	kunit_info(test, "MMIX_FAULT demand=ok partial=16 scalar_zero=ok disabled=ok\n");
out:
	drop_test_mm(mm);
}

static int cow_child(void *unused)
{
	unsigned long value;

	if (get_user(value, (unsigned long __user *)DATA) || value != 0x1111)
		return 1;
	if (put_user(0x2222UL, (unsigned long __user *)DATA))
		return 2;
	if (get_user(value, (unsigned long __user *)DATA) || value != 0x2222)
		return 3;
	return 0;
}

static void private_write_fault(struct kunit *test)
{
	__sighandler_t handler = current->sighand->action[SIGCHLD - 1].sa.sa_handler;
	struct mm_struct *mm = use_test_mm();
	struct kernel_clone_args args = { .fn = cow_child, .exit_signal = SIGCHLD };
	unsigned long value;
	pid_t child;
	int status = 0;

	KUNIT_ASSERT_NOT_NULL(test, mm);
	kernel_sigaction(SIGCHLD, SIG_DFL);
	if (map_region(DATA, PAGE_SIZE, PROT_READ | PROT_WRITE) != DATA) {
		KUNIT_FAIL(test, "map private page");
		goto out;
	}
	KUNIT_EXPECT_EQ(test, put_user(0x1111UL, (unsigned long __user *)DATA), 0);
	child = kernel_clone(&args);
	KUNIT_EXPECT_GT(test, child, 0);
	if (child > 0) {
		KUNIT_EXPECT_EQ(test, kernel_wait(child, &status), child);
		KUNIT_EXPECT_EQ(test, status, 0);
	}
	KUNIT_EXPECT_EQ(test, get_user(value, (unsigned long __user *)DATA), 0);
	KUNIT_EXPECT_EQ(test, value, 0x1111UL);
	KUNIT_EXPECT_EQ(test, put_user(0x3333UL, (unsigned long __user *)DATA), 0);
	kunit_info(test, "MMIX_FAULT cow_child=2222 parent=1111\n");
out:
	kernel_sigaction(SIGCHLD, handler);
	drop_test_mm(mm);
}

static int user_event(struct mmix_user_entry *entry, void *data)
{
	unsigned int *faults = data;

	if (entry->event == MMIX_USER_FAULT) {
		(*faults)++;
		return mmix_user_fault(entry, NULL);
	}
	return mmix_user_rstack_state(entry->stack)->regs.regs[231] == 0x1234 ? 1 : -EINVAL;
}

static const struct mmix_user_entry_ops user_ops = {
	.event = user_event,
	.write = mmix_user_write,
};

static int run_fault_user(void *code)
{
	struct mmix_user_state *state;
	unsigned int faults = 0;
	int result;

	current->thread.user_state = mmix_user_rstack_alloc();
	if (!current->thread.user_state)
		return 1;
	state = mmix_user_rstack_state(current->thread.user_state);
	state->regs.r_g = 230;
	state->regs.r_o = STACK;
	state->pending.start = STACK;
	state->regs.regs[254] = MMIX_TASK_SIZE;
	state->regs.pc = CODE + (const unsigned char *)code - mmix_user_test_start;
	result = mmix_user_enter(current->thread.user_state, &user_ops, &faults);
	return result || ((code == mmix_user_test_deep) && !faults);
}

static void user_fault_containment(struct kunit *test)
{
	__sighandler_t handler = current->sighand->action[SIGCHLD - 1].sa.sa_handler;
	struct mm_struct *mm = use_test_mm();
	struct kernel_clone_args args = { .fn = run_fault_user, .exit_signal = SIGCHLD };
	const unsigned char *programs[] = { mmix_user_test_kernel,     mmix_user_test_direct,
					    mmix_user_test_privileged, mmix_user_test_deep,
					    mmix_user_test_deep,       mmix_user_test_deep,
					    mmix_user_test_deep,       mmix_user_test_deep };
	int expected[] = { SIGSEGV, SIGSEGV, SIGILL, 0, 0, SIGSEGV, SIGSEGV, SIGSEGV };
	unsigned long value;
	unsigned int i;
	int status;
	pid_t child;

	KUNIT_ASSERT_NOT_NULL(test, mm);
	kernel_sigaction(SIGCHLD, SIG_DFL);
	if (map_region(CODE, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC) != CODE ||
	    map_region(STACK, 8 * PAGE_SIZE, PROT_READ | PROT_WRITE) != STACK) {
		KUNIT_FAIL(test, "map user program and register stack");
		goto out;
	}
	KUNIT_EXPECT_EQ(test,
			copy_to_user((void __user *)CODE, mmix_user_test_start,
				     mmix_user_test_end - mmix_user_test_start),
			0UL);
	flush_icache_range(CODE, CODE + PAGE_SIZE);
	for (i = 0; i < ARRAY_SIZE(programs); i++) {
		if (i == 4) {
			/* Also fault a real implicit write on an existing read-only zero page. */
			KUNIT_EXPECT_EQ(test, get_user(value, (unsigned long __user *)STACK), 0);
			KUNIT_EXPECT_EQ(test, value, 0UL);
		}
		if (i == 5)
			KUNIT_EXPECT_EQ(test, vm_munmap(STACK + PAGE_SIZE, PAGE_SIZE), 0);
		if (i == 6) {
			KUNIT_EXPECT_EQ(test, map_region(CODE, PAGE_SIZE, PROT_READ | PROT_WRITE),
					CODE);
			KUNIT_EXPECT_EQ(test,
					copy_to_user((void __user *)CODE, mmix_user_test_start,
						     mmix_user_test_end - mmix_user_test_start),
					0UL);
		}
		if (i == 7)
			KUNIT_EXPECT_EQ(test, vm_munmap(CODE, PAGE_SIZE), 0);
		args.fn_arg = (void *)programs[i];
		child = kernel_clone(&args);
		KUNIT_EXPECT_GT(test, child, 0);
		if (child <= 0)
			continue;
		status = 0;
		KUNIT_EXPECT_EQ(test, kernel_wait(child, &status), child);
		KUNIT_EXPECT_EQ(test, status, expected[i]);
	}
	/* A fresh independent child remains usable after fatal user faults. */
	KUNIT_EXPECT_EQ(test, map_region(CODE, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC),
			CODE);
	KUNIT_EXPECT_EQ(test,
			copy_to_user((void __user *)CODE, mmix_user_test_start,
				     mmix_user_test_end - mmix_user_test_start),
			0UL);
	flush_icache_range(CODE, CODE + PAGE_SIZE);
	KUNIT_EXPECT_EQ(test, map_region(STACK + PAGE_SIZE, PAGE_SIZE, PROT_READ | PROT_WRITE),
			STACK + PAGE_SIZE);
	args.fn_arg = (void *)mmix_user_test_deep;
	child = kernel_clone(&args);
	KUNIT_EXPECT_GT(test, child, 0);
	if (child > 0) {
		status = 0;
		KUNIT_EXPECT_EQ(test, kernel_wait(child, &status), child);
		KUNIT_EXPECT_EQ(test, status, 0);
	}
	kunit_info(test, "MMIX_FAULT user_stack=ok fatal=contained control=ok\n");
out:
	kernel_sigaction(SIGCHLD, handler);
	drop_test_mm(mm);
}

static struct kunit_case user_fault_cases[] = { KUNIT_CASE(user_access),
						KUNIT_CASE(private_write_fault),
						KUNIT_CASE(user_fault_containment),
						{} };
static struct kunit_suite user_fault_suite = {
	.name = "mmix_user_fault",
	.test_cases = user_fault_cases,
};

kunit_test_suite(user_fault_suite);
