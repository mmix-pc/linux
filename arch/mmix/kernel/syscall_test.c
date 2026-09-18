// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/file.h>
#include <linux/fdtable.h>
#include <linux/kthread.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <asm/rstack.h>
#include <linux/shmem_fs.h>
#include <linux/syscalls.h>
#include <linux/time.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/syscall.h>
#include "user_entry.h"
#include "syscall_test.h"

#define CODE (4UL << 20)
#define STACK (6UL << 20)
#define DATA (8UL << 20)
#define MAPPING (10UL << 20)

struct syscall_test {
	struct kunit *test;
	struct mm_struct *mm;
	struct mmix_user_rstack_state *stack;
	struct user_regs_struct before;
	bool deep, sync, inject_pending;
	unsigned int fault;
	unsigned long base;
	u64 chain;
	unsigned int stops;
	long result;
};

static int observe_syscall(struct mmix_user_entry *entry, void *data)
{
	struct syscall_test *t = data;
	struct mmix_user_state *state = mmix_user_rstack_state(entry->stack);
	struct user_regs_struct *regs = &state->regs;
	unsigned long nr = t->before.regs[237];
	unsigned int i;
	int error;

	if (entry->event != MMIX_USER_SYSCALL)
		return mmix_user_fault(entry, NULL);
	if (t->deep) {
		if (entry->regs.syscall_nr == 0xffff) {
			t->result = regs->regs[231];
			return 1;
		}
		if (t->sync) {
			KUNIT_EXPECT_EQ(t->test, entry->regs.syscall_nr, __NR_mmix_rstack_sync);
			if (!t->stops)
				KUNIT_EXPECT_GT(t->test, state->pending.count, 0UL);
		}
		t->stops++;
		return mmix_user_syscall(entry, NULL);
	}
	if (!t->stops++) {
		KUNIT_EXPECT_EQ(t->test, (unsigned long)entry->regs.syscall_nr, nr);
		KUNIT_EXPECT_EQ(t->test, entry->regs.pc, CODE + 4);
		KUNIT_EXPECT_EQ(t->test, syscall_get_nr(current, &entry->regs),
				nr < __NR_syscalls ? (int)nr : -1);
		for (i = 0; i < 6; i++)
			KUNIT_EXPECT_EQ(t->test, entry->regs.syscall_args[i],
					t->before.regs[231 + i]);
		if (t->inject_pending) {
			state->pending.start = regs->r_o - 16;
			state->pending.count = 2;
			state->pending.data[0] = 0x12345678;
			state->pending.data[1] = 0xabcdef;
		}
		error = mmix_user_syscall(entry, NULL);
		if (t->fault == 4)
			KUNIT_EXPECT_TRUE(t->test, entry->rstack_query_pending);
		if (t->fault == 1 || t->fault == 2 || t->fault == 4) {
			mmix_user_rstack_write_limit(t->fault == 2 ? 8 : 0);
		} else if (t->fault == 3) {
			local_irq_enable();
			KUNIT_EXPECT_EQ(t->test,
					mmix_rstack_domain_release(current->mm, t->chain), 0);
			local_irq_disable();
		}
		if (t->fault == 5)
			state->pending.count++;
		if (nr == __NR_mmix_rstack_sync)
			KUNIT_EXPECT_TRUE(t->test, entry->rstack_sync_pending);
		KUNIT_EXPECT_EQ(t->test, error, 0);
		/* Service results must not destroy the original restart metadata. */
		KUNIT_EXPECT_EQ(t->test, (unsigned long)entry->regs.syscall_nr, nr);
		KUNIT_EXPECT_EQ(t->test, entry->regs.r_ww, CODE + 4);
		for (i = 0; i < 6; i++)
			KUNIT_EXPECT_EQ(t->test, entry->regs.syscall_args[i],
					t->before.regs[231 + i]);
		return error;
	}
	KUNIT_EXPECT_EQ(t->test, regs->pc, CODE + 8);
	for (i = 0; i < 256; i++) {
		if (i == 231 || i == 237 || i == 255 || (i >= 64 && i < 230))
			continue;
		KUNIT_EXPECT_EQ(t->test, regs->regs[i], t->before.regs[i]);
	}
	KUNIT_EXPECT_EQ(t->test, regs->r_g, t->before.r_g);
	KUNIT_EXPECT_EQ(t->test, regs->r_l, t->before.r_l);
	KUNIT_EXPECT_EQ(t->test, regs->r_o, t->before.r_o);
	KUNIT_EXPECT_EQ(t->test, regs->r_a, t->before.r_a);
	KUNIT_EXPECT_EQ(t->test, regs->r_b, t->before.r_b);
	KUNIT_EXPECT_EQ(t->test, regs->r_d, t->before.r_d);
	KUNIT_EXPECT_EQ(t->test, regs->r_e, t->before.r_e);
	KUNIT_EXPECT_EQ(t->test, regs->r_h, t->before.r_h);
	KUNIT_EXPECT_EQ(t->test, regs->r_j, t->before.r_j);
	KUNIT_EXPECT_EQ(t->test, regs->r_m, t->before.r_m);
	KUNIT_EXPECT_EQ(t->test, regs->r_p, t->before.r_p);
	KUNIT_EXPECT_EQ(t->test, regs->r_r, t->before.r_r);
	KUNIT_EXPECT_EQ(t->test, regs->r_w, t->before.r_w);
	KUNIT_EXPECT_EQ(t->test, regs->r_x, t->before.r_x);
	KUNIT_EXPECT_EQ(t->test, regs->r_y, t->before.r_y);
	KUNIT_EXPECT_EQ(t->test, regs->r_z, t->before.r_z);

	t->result = regs->regs[231];
	return 1;
}

static const struct mmix_user_entry_ops test_ops = {
	.event = observe_syscall,
	.write = mmix_user_write,
};

static long call_user(struct syscall_test *t, unsigned long nr, unsigned long a0, unsigned long a1,
		      unsigned long a2, unsigned long a3, unsigned long a4, unsigned long a5)
{
	struct mmix_user_state *state = mmix_user_rstack_state(t->stack);
	struct user_regs_struct *regs = &state->regs;
	unsigned long args[] = { a0, a1, a2, a3, a4, a5 };
	unsigned int i;

	memset(state, 0, sizeof(*state));
	regs->pc = CODE;
	regs->r_g = 230;
	regs->r_l = 64;
	regs->r_o = t->base + (t->inject_pending ? 16 : 0);
	state->pending.start = regs->r_o;
	for (i = 0; i < 256; i++)
		if (i < 64 || i >= 230)
			regs->regs[i] = 0x1234567800000000UL + i * 0x101;
	regs->regs[254] = MMIX_TASK_SIZE;
	regs->r_a = 0x30000;
	regs->r_b = 0x112;
	regs->r_d = 0x123;
	regs->r_e = 0x234;
	regs->r_h = 0x345;
	regs->r_j = 0x456;
	regs->r_m = 0x567;
	regs->r_p = 0x678;
	regs->r_r = 0x789;
	regs->r_w = 0xabc;
	regs->r_x = 0xbcd;
	regs->r_y = 0xcde;
	regs->r_z = 0xdef;
	memcpy(regs->regs + 231, args, sizeof(args));
	regs->regs[237] = nr;
	t->before = *regs;
	t->stops = 0;
	t->result = -EIO;
	KUNIT_EXPECT_EQ(t->test, mmix_user_enter(t->stack, &test_ops, t), 0);
	KUNIT_EXPECT_EQ(t->test, t->stops, 2U);
	return t->result;
}

static int syscall_test_init(struct kunit *test)
{
	struct syscall_test *t = kunit_kzalloc(test, sizeof(*t), GFP_KERNEL);

	if (!t)
		return -ENOMEM;
	test->priv = t;
	t->test = test;
	t->base = STACK;
	t->stack = mmix_user_rstack_alloc();
	if (!t->stack)
		return -ENOMEM;
	t->mm = mm_alloc();
	if (!t->mm) {
		mmix_user_rstack_free(&t->stack);
		return -ENOMEM;
	}
	kthread_use_mm(t->mm);
	if (vm_mmap(NULL, CODE, PAGE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0) != CODE ||
	    vm_mmap(NULL, STACK, PAGE_SIZE, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0) != STACK ||
	    vm_mmap(NULL, DATA, PAGE_SIZE, PROT_READ | PROT_WRITE,
		    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0) != DATA ||
	    copy_to_user((void __user *)CODE, mmix_syscall_test_start,
			 mmix_syscall_test_end - mmix_syscall_test_start)) {
		kthread_unuse_mm(t->mm);
		mmput(t->mm);
		mmix_user_rstack_free(&t->stack);
		return -ENOMEM;
	}
	flush_icache_range(CODE, CODE + PAGE_SIZE);
	return 0;
}

static void syscall_test_exit(struct kunit *test)
{
	struct syscall_test *t = test->priv;

	kthread_unuse_mm(t->mm);
	mmput(t->mm);
	mmix_user_rstack_free(&t->stack);
}

static void numbers_and_errors(struct kunit *test)
{
	struct syscall_test *t = test->priv;
	unsigned long invalid[] = { __NR_syscalls,
				    __NR_syscalls + 1,
				    1UL << 32 | __NR_getpid,
				    1UL << 63 | __NR_getpid,
				    ~0UL,
				    __NR_socket,
				    __NR_clone3 };
	struct pt_regs *regs = kunit_kzalloc(test, sizeof(*regs), GFP_KERNEL);
	long results[] = { -1, -4095, -4096, 0, 4096 };
	unsigned int i;

	KUNIT_ASSERT_NOT_NULL(test, regs);
	for (i = 0; i < ARRAY_SIZE(invalid); i++)
		KUNIT_EXPECT_EQ(test, call_user(t, invalid[i], 0, 0, 0, 0, 0, 0), -ENOSYS);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_clone, 0, 0, 0, 0, 0, 0), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_clone, SIGCHLD | CLONE_VM, 0, 0, 0, 0, 0),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test,
			call_user(t, __NR_clone, SIGCHLD | CLONE_VM | CLONE_VFORK,
				  0, 0, 0, 0, 0), -EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_clone, SIGCHLD, DATA, 0, 0, 0, 0),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_clone, (1UL << 32) | SIGCHLD, 0, 0, 0, 0, 0),
			-EOPNOTSUPP);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_execve, 0, 0, 0, 0, 0, 0), -EFAULT);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_execveat, 0, 0, 0, 0, 0, 0), -EFAULT);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_getpid, 0, 0, 0, 0, 0, 0),
			(long)task_tgid_vnr(current));
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_close, ~0UL, 0, 0, 0, 0, 0), -EBADF);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_clock_gettime, CLOCK_MONOTONIC, 0, 0, 0, 0, 0),
			-EFAULT);
	KUNIT_EXPECT_EQ(test,
			call_user(t, __NR_clock_gettime, CLOCK_MONOTONIC, 1UL << 63, 0, 0, 0, 0),
			-EFAULT);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_clock_gettime, CLOCK_MONOTONIC, DATA, 0, 0, 0, 0),
			0L);
	/* Boundary classification is a helper property, not a fabricated service. */
	for (i = 0; i < ARRAY_SIZE(results); i++) {
		syscall_set_return_value(current, regs, 0, results[i]);
		KUNIT_EXPECT_EQ(test, syscall_get_return_value(current, regs), results[i]);
		KUNIT_EXPECT_EQ(test, syscall_get_error(current, regs), i < 2 ? results[i] : 0L);
	}
	kunit_info(test, "MMIX_SYSCALL numbers=ok errno=ok buffers=ok\n");
}

static void six_arguments(struct kunit *test)
{
	struct syscall_test *t = test->priv;
	struct file *file;
	unsigned long value = 0x9876543210abcdefUL;
	int fd;

	file = shmem_file_setup("mmix-syscall", 2 * PAGE_SIZE, EMPTY_VMA_FLAGS);
	KUNIT_ASSERT_FALSE(test, IS_ERR(file));
	fd = get_unused_fd_flags(0);
	if (fd < 0) {
		fput(file);
		KUNIT_FAIL(test, "allocate file descriptor");
		return;
	}
	/* Populate the second page: fd and byte offset must both reach mmap. */
	KUNIT_EXPECT_EQ(test,
			vm_mmap(file, DATA + 2 * PAGE_SIZE, 2 * PAGE_SIZE, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_FIXED, 0),
			DATA + 2 * PAGE_SIZE);
	KUNIT_EXPECT_EQ(test, put_user(value, (unsigned long __user *)(DATA + 3 * PAGE_SIZE)), 0);
	fd_install(fd, file);
	KUNIT_EXPECT_EQ(test,
			call_user(t, __NR_mmap, MAPPING, PAGE_SIZE, PROT_READ,
				  MAP_PRIVATE | MAP_FIXED, fd, PAGE_SIZE),
			(long)MAPPING);
	value = 0;
	KUNIT_EXPECT_EQ(test, get_user(value, (unsigned long __user *)MAPPING), 0);
	KUNIT_EXPECT_EQ(test, value, 0x9876543210abcdefUL);
	KUNIT_EXPECT_EQ(test, put_user(1UL, (unsigned long __user *)MAPPING), -EFAULT);
	KUNIT_EXPECT_EQ(test, get_user(value, (unsigned long __user *)(MAPPING + PAGE_SIZE)),
			-EFAULT);
	KUNIT_EXPECT_EQ(test,
			call_user(t, __NR_mmap, MAPPING, PAGE_SIZE, PROT_READ,
				  MAP_PRIVATE | MAP_FIXED, fd, 4096),
			-EINVAL);
	KUNIT_EXPECT_EQ(test,
			call_user(t, __NR_mmap, MAPPING, PAGE_SIZE, PROT_READ,
				  MAP_PRIVATE | MAP_FIXED, ~0UL, PAGE_SIZE),
			-EBADF);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_munmap, MAPPING, PAGE_SIZE, 0, 0, 0, 0), 0L);
	KUNIT_EXPECT_EQ(test, close_fd(fd), 0);
	kunit_info(test, "MMIX_SYSCALL mmap=six offset=8192 protection=ok\n");
}

static void sleeping_calls(struct kunit *test)
{
	struct syscall_test *t = test->priv;
	struct __kernel_timespec ts = { .tv_nsec = 20000000 };
	unsigned long switches = current->nvcsw;
	unsigned int i;

	KUNIT_ASSERT_EQ(test, copy_to_user((void __user *)DATA, &ts, sizeof(ts)), 0UL);
	for (i = 0; i < 20; i++) {
		KUNIT_EXPECT_EQ(test, call_user(t, __NR_nanosleep, DATA, 0, 0, 0, 0, 0), 0L);
		KUNIT_EXPECT_EQ(test, call_user(t, __NR_getpid, 0, 0, 0, 0, 0, 0),
				(long)task_tgid_vnr(current));
	}
	KUNIT_EXPECT_GE(test, current->nvcsw - switches, 20UL);
	kunit_info(test, "MMIX_SYSCALL sleep=20 preserved=ok metadata=ok\n");
}

static void deep_sleeping_calls(struct kunit *test)
{
	struct syscall_test *t = test->priv;
	struct mmix_user_state *state = mmix_user_rstack_state(t->stack);
	struct __kernel_timespec ts = { .tv_nsec = 20000000 };
	unsigned long switches = current->nvcsw;

	KUNIT_ASSERT_EQ(test, copy_to_user((void __user *)DATA, &ts, sizeof(ts)), 0UL);
	state->regs.r_g = 230;
	state->regs.r_o = STACK;
	state->pending.start = STACK;
	state->regs.regs[254] = MMIX_TASK_SIZE;
	state->regs.pc = CODE + mmix_syscall_test_deep - mmix_syscall_test_start;
	KUNIT_ASSERT_EQ(test,
			vm_mmap(NULL, STACK, 8 * PAGE_SIZE, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0),
			STACK);
	t->deep = true;
	KUNIT_EXPECT_EQ(test, mmix_user_enter(t->stack, &test_ops, t), 0);
	KUNIT_EXPECT_EQ(test, t->stops, 20U);
	KUNIT_EXPECT_EQ(test, t->result, 0x1234L);
	KUNIT_EXPECT_GE(test, current->nvcsw - switches, 20UL);
	kunit_info(test, "MMIX_SYSCALL deep=64 sleep=20 unwind=ok\n");
}

/* KUnit suite cleanup runs in a different task: borrow mm within each case. */
static void numbers_and_errors_case(struct kunit *test)
{
	KUNIT_ASSERT_EQ(test, syscall_test_init(test), 0);
	numbers_and_errors(test);
	syscall_test_exit(test);
}

static void six_arguments_case(struct kunit *test)
{
	KUNIT_ASSERT_EQ(test, syscall_test_init(test), 0);
	six_arguments(test);
	syscall_test_exit(test);
}

static void sleeping_calls_case(struct kunit *test)
{
	KUNIT_ASSERT_EQ(test, syscall_test_init(test), 0);
	sleeping_calls(test);
	syscall_test_exit(test);
}

static void deep_sleeping_calls_case(struct kunit *test)
{
	KUNIT_ASSERT_EQ(test, syscall_test_init(test), 0);
	deep_sleeping_calls(test);
	syscall_test_exit(test);
}

static int sync_fault_child(void *data)
{
	struct syscall_test *t = data;

	current->thread.rstack_chain = t->chain;
	call_user(t, t->fault == 4 ? __NR_mmix_rstack_query : __NR_mmix_rstack_sync,
		  DATA, 2, 3, 4, 5, 6);
	return 99;
}

static void rstack_services_case(struct kunit *test)
{
	struct syscall_test *t;
	struct mmix_rstack_query query;
	struct mmix_user_state *state;
	sigset_t old_mask = current->blocked, mask;
	u64 old_chain = current->thread.rstack_chain;
	__sighandler_t handler = current->sighand->action[SIGCHLD - 1].sa.sa_handler;
	struct kernel_clone_args args = { .fn = sync_fault_child, .exit_signal = SIGCHLD };
	unsigned int i;
	int error, status;
	pid_t child;

	KUNIT_ASSERT_EQ(test, syscall_test_init(test), 0);
	t = test->priv;
	t->mm->mmap_base = TASK_UNMAPPED_BASE;
	error = mmix_rstack_domain_create(t->mm, 0, &t->chain, &t->base);
	KUNIT_EXPECT_EQ(test, error, 0);
	if (error)
		goto out;
	current->thread.rstack_chain = t->chain;
	sigemptyset(&mask);
	sigaddset(&mask, SIGUSR1);
	sigaddset(&mask, SIGKILL);
	sigaddset(&mask, SIGSTOP);
	set_current_blocked(&mask);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_mmix_rstack_sync, ~0UL, 2, 3, 4, 5, 6), 0L);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_mmix_rstack_query, DATA, 0, 0, 0, 0, 0), 0L);
	KUNIT_EXPECT_EQ(test, copy_from_user(&query, (void __user *)DATA, sizeof(query)), 0UL);
	KUNIT_EXPECT_EQ(test, query.chain_id, t->chain);
	KUNIT_EXPECT_EQ(test, query.sigmask, (u64)sigmask(SIGUSR1));
	sigaddset(&mask, SIGUSR2);
	set_current_blocked(&mask);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_mmix_rstack_query, DATA, 0, 0, 0, 0, 0), 0L);
	KUNIT_EXPECT_EQ(test, copy_from_user(&query, (void __user *)DATA, sizeof(query)), 0UL);
	KUNIT_EXPECT_EQ(test, query.chain_id, t->chain);
	KUNIT_EXPECT_EQ(test, query.sigmask, (u64)(sigmask(SIGUSR1) | sigmask(SIGUSR2)));
	sigdelset(&mask, SIGUSR2);
	set_current_blocked(&mask);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_mmix_rstack_query, DATA + 1, 0, 0, 0, 0, 0),
			-EINVAL);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_mmix_rstack_query, 0, 0, 0, 0, 0, 0), -EFAULT);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_mmix_rstack_query, MMIX_TASK_SIZE - 8,
					0, 0, 0, 0, 0), -EFAULT);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_mmix_rstack_query, DATA + PAGE_SIZE - 8,
					0, 0, 0, 0, 0), -EFAULT);
	KUNIT_EXPECT_EQ(test, sys_mprotect(DATA, PAGE_SIZE, PROT_READ), 0);
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_mmix_rstack_query, DATA, 0, 0, 0, 0, 0), -EFAULT);
	KUNIT_EXPECT_EQ(test, sys_mprotect(DATA, PAGE_SIZE, PROT_READ | PROT_WRITE), 0);
	t->inject_pending = true;
	KUNIT_EXPECT_EQ(test, call_user(t, __NR_mmix_rstack_query, t->base, 0, 0, 0, 0, 0), 0L);
	KUNIT_EXPECT_EQ(test, copy_from_user(&query, (void __user *)t->base, sizeof(query)), 0UL);
	KUNIT_EXPECT_EQ(test, query.chain_id, t->chain);
	KUNIT_EXPECT_EQ(test, query.sigmask, (u64)sigmask(SIGUSR1));
	args.fn_arg = t;
	kernel_sigaction(SIGCHLD, SIG_DFL);
	for (i = 1; i <= 5; i++) {
		t->fault = i;
		child = kernel_clone(&args);
		KUNIT_EXPECT_GT(test, child, 0);
		if (child > 0) {
			status = 0;
			KUNIT_EXPECT_EQ(test, kernel_wait(child, &status), child);
			KUNIT_EXPECT_EQ(test, status, SIGSEGV);
			KUNIT_EXPECT_EQ(test, t->stops, 1U);
		}
		mmix_user_rstack_write_limit(-1);
	}
	kernel_sigaction(SIGCHLD, handler);
	t->fault = 0;
	t->inject_pending = false;
	t->deep = true;
	t->sync = true;
	t->stops = 0;
	state = mmix_user_rstack_state(t->stack);
	memset(state, 0, sizeof(*state));
	state->regs.r_g = 230;
	state->regs.r_o = t->base;
	state->pending.start = t->base;
	state->regs.regs[254] = MMIX_TASK_SIZE;
	state->regs.pc = CODE + mmix_syscall_test_sync - mmix_syscall_test_start;
	KUNIT_EXPECT_EQ(test, mmix_user_enter(t->stack, &test_ops, t), 0);
	KUNIT_EXPECT_EQ(test, t->stops, 20U);
	KUNIT_EXPECT_EQ(test, t->result, 0x1234L);
	kunit_info(test, "MMIX_RSTACK sync=20 query=ok alias=ok fatal=5 preserved=ok\n");
out:
	set_current_blocked(&old_mask);
	current->thread.rstack_chain = old_chain;
	syscall_test_exit(test);
}

static struct kunit_case syscall_cases[] = { KUNIT_CASE(numbers_and_errors_case),
					     KUNIT_CASE(six_arguments_case),
					     KUNIT_CASE(sleeping_calls_case),
					     KUNIT_CASE(deep_sleeping_calls_case),
					     KUNIT_CASE(rstack_services_case),
					     {} };
static struct kunit_suite syscall_suite = {
	.name = "mmix_syscall",
	.test_cases = syscall_cases,
};

kunit_test_suite(syscall_suite);
