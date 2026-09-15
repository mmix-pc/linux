// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/delay.h>
#include <linux/fdtable.h>
#include <linux/file.h>
#include <linux/kthread.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/shmem_fs.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/unistd.h>
#include "lifecycle_test.h"
#include "process.h"
#include "user_entry.h"

#define CODE (4UL << 20)
#define DATA (6UL << 20)
#define STACK (8UL << 20)
#define FILE_MAP (10UL << 20)

struct lifecycle_test {
	unsigned int clones, waits, rejected;
	int fd;
};

static int lifecycle_event(struct mmix_user_entry *entry, void *data)
{
	struct lifecycle_test *t = data;
	unsigned long nr = entry->regs.syscall_nr;
	int error;

	if (entry->event == MMIX_USER_SYSCALL && nr == __NR_clone) {
		if (t->clones < 3)
			mmix_user_rstack_fail_after(t->clones);
		else if (t->clones == 3)
			mmix_rstack_fail_after(0);
		t->clones++;
	}
	error = mmix_user_syscall(entry, data);
	if (entry->event == MMIX_USER_SYSCALL && nr == __NR_clone) {
		if (t->clones <= 4 && (long)entry->regs.regs[231] == -ENOMEM)
			t->rejected++;
		mmix_user_rstack_fail_after(-1);
		mmix_rstack_fail_after(-1);
	}
	if (entry->event == MMIX_USER_SYSCALL && nr == __NR_wait4 &&
	    (long)entry->regs.regs[231] > 0)
		t->waits++;
	return error;
}

static const struct mmix_user_entry_ops lifecycle_ops = {
	.event = lifecycle_event,
	.write = mmix_user_write,
};

static int lifecycle_parent(void *data)
{
	struct lifecycle_test *t = data;
	struct mmix_user_state *state;

	current->thread.user_state = mmix_user_rstack_alloc();
	if (!current->thread.user_state)
		return 1;
	state = mmix_user_rstack_state(current->thread.user_state);
	state->regs.pc = CODE;
	state->regs.r_g = 230;
	state->regs.r_o = STACK;
	state->pending.start = STACK;
	state->regs.regs[254] = STACK + (64UL << 10) - 16;
	state->regs.regs[242] = t->fd;
	return mmix_user_enter(current->thread.user_state, &lifecycle_ops, t) ?
		       1 :
		       0;
}

static void lifecycle_cycles(struct kunit *test)
{
	__sighandler_t handler =
		current->sighand->action[SIGCHLD - 1].sa.sa_handler;
	struct kernel_clone_args args = { .fn = lifecycle_parent,
					  .exit_signal = SIGCHLD };
	struct lifecycle_test t = {};
	struct mm_struct *mm;
	struct file *file;
	unsigned long words[] = { DATA + 65, 0 };
	unsigned long delay[] = { 0, 10000000 };
	long baseline = mmix_user_rstack_live();
	unsigned long kernel_live =
		mmix_rstack_allocated() - mmix_rstack_released();
	size_t length = mmix_lifecycle_image_end - mmix_lifecycle_image;
	unsigned int i;
	int status = 0;
	pid_t pid;

	mm = mm_alloc();
	KUNIT_ASSERT_NOT_NULL(test, mm);
	kthread_use_mm(mm);
	kernel_sigaction(SIGCHLD, SIG_DFL);
	KUNIT_EXPECT_EQ(test,
			vm_mmap(NULL, CODE, PAGE_SIZE,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0),
			CODE);
	KUNIT_EXPECT_EQ(test,
			vm_mmap(NULL, DATA, PAGE_SIZE, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0),
			DATA);
	KUNIT_EXPECT_EQ(test,
			vm_mmap(NULL, STACK, 64UL << 10, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0),
			STACK);
	KUNIT_EXPECT_EQ(test,
			copy_to_user((void __user *)CODE, mmix_lifecycle_start,
				     mmix_lifecycle_end - mmix_lifecycle_start),
			0UL);
	KUNIT_EXPECT_EQ(test,
			copy_to_user((void __user *)(DATA + 32), words,
				     sizeof(words)),
			0UL);
	KUNIT_EXPECT_EQ(test,
			copy_to_user((void __user *)(DATA + 64), "\0child", 7),
			0UL);
	KUNIT_EXPECT_EQ(test,
			copy_to_user((void __user *)(DATA + 80), delay,
				     sizeof(delay)),
			0UL);
	flush_icache_range(CODE, CODE + PAGE_SIZE);
	file = shmem_file_setup("mmix-child", length, EMPTY_VMA_FLAGS);
	if (IS_ERR(file)) {
		KUNIT_FAIL(test, "create child executable");
		goto out;
	}
	t.fd = get_unused_fd_flags(0);
	if (t.fd < 0) {
		fput(file);
		KUNIT_FAIL(test, "allocate executable fd");
		goto out;
	}
	KUNIT_EXPECT_EQ(test,
			vm_mmap(file, FILE_MAP, PAGE_ALIGN(length),
				PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED,
				0),
			FILE_MAP);
	KUNIT_EXPECT_EQ(test,
			copy_to_user((void __user *)FILE_MAP,
				     mmix_lifecycle_image, length),
			0UL);
	KUNIT_EXPECT_EQ(test, vm_munmap(FILE_MAP, PAGE_ALIGN(length)), 0);
	fd_install(t.fd, file);
	args.fn_arg = &t;
	pid = kernel_clone(&args);
	KUNIT_EXPECT_GT(test, pid, 0);
	if (pid > 0)
		KUNIT_EXPECT_EQ(test, kernel_wait(pid, &status), pid);
	KUNIT_EXPECT_EQ(test, status, 0);
	KUNIT_EXPECT_EQ(test, t.clones, 104U);
	KUNIT_EXPECT_EQ(test, t.waits, 100U);
	KUNIT_EXPECT_EQ(test, t.rejected, 4U);
	KUNIT_EXPECT_EQ(test, close_fd(t.fd), 0);
	for (i = 0;
	     i < 100 &&
	     (mmix_user_rstack_live() != baseline ||
	      mmix_rstack_allocated() - mmix_rstack_released() != kernel_live);
	     i++)
		usleep_range(10000, 11000);
	KUNIT_EXPECT_EQ(test, mmix_user_rstack_live(), baseline);
	KUNIT_EXPECT_EQ(test, mmix_rstack_allocated() - mmix_rstack_released(),
			kernel_live);
	kunit_info(test, "MMIX_LIFECYCLE cycles=%u failed=%u status=%d\n",
		   t.waits, t.rejected, status);
out:
	kernel_sigaction(SIGCHLD, handler);
	kthread_unuse_mm(mm);
	mmput(mm);
}

static struct kunit_case lifecycle_cases[] = { KUNIT_CASE(lifecycle_cycles),
					       {} };
static struct kunit_suite lifecycle_suite = {
	.name = "mmix_lifecycle",
	.test_cases = lifecycle_cases,
};

kunit_test_suite(lifecycle_suite);
