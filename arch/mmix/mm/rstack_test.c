// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/kthread.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/sizes.h>
#include <linux/syscalls.h>
#include <linux/uaccess.h>
#include <asm/rstack.h>

struct fork_domains {
	u64 root, orphan;
	unsigned long base, orphan_base;
};

static int check_child_domains(void *data)
{
	struct fork_domains *state = data;
	struct mmix_rstack_domain *domain;
	unsigned long value;

	current->thread.rstack_chain = state->root;
	domain = mmix_rstack_domain_begin(current->mm, state->root, state->base, sizeof(value));
	if (IS_ERR(domain))
		return 1;
	if (get_user(value, (unsigned long __user *)state->base) || value != 0x12345678 ||
	    put_user(0xabcdefUL, (unsigned long __user *)state->base)) {
		mmix_rstack_domain_end(current->mm, domain);
		return 2;
	}
	mmix_rstack_domain_end(current->mm, domain);
	if (sys_mprotect(state->base, PAGE_SIZE, PROT_NONE) != -EPERM)
		return 3;
	domain = mmix_rstack_domain_begin(current->mm, state->orphan, state->orphan_base, 8);
	if (!IS_ERR(domain)) {
		mmix_rstack_domain_end(current->mm, domain);
		return 4;
	}
	/* Unrelated inherited data remains mapped, without active-chain authority. */
	if (get_user(value, (unsigned long __user *)state->orphan_base) || value != 0x9876)
		return 5;
	if (vm_munmap(state->orphan_base - PAGE_SIZE, SZ_1M + 3 * PAGE_SIZE))
		return 6;
	return 0;
}

static void owned_domains(struct kunit *test)
{
	struct mm_struct *mm = mm_alloc();
	struct mmix_rstack_domain *domain;
	struct vm_area_struct *vma;
	unsigned long base, address, value = 0x12345678;
	u64 root, ids[32], extra, *roots;
	unsigned int i;
	int error, maps, status;
	pid_t child;
	struct fork_domains fork_state;
	struct kernel_clone_args args = {
		.fn = check_child_domains,
		.fn_arg = &fork_state,
		.exit_signal = SIGCHLD,
	};
	__sighandler_t handler = current->sighand->action[SIGCHLD - 1].sa.sa_handler;
	u64 old_chain = current->thread.rstack_chain;

	KUNIT_ASSERT_NOT_NULL(test, mm);
	roots = kunit_kcalloc(test, 256, sizeof(*roots), GFP_KERNEL);
	if (!roots) {
		mmput(mm);
		KUNIT_FAIL(test, "allocate identity array");
		return;
	}
	/* Match exec's initialized allocation layout in this synthetic mm. */
	mm->mmap_base = TASK_UNMAPPED_BASE;
	kthread_use_mm(mm);
	error = mmix_rstack_domain_create(mm, 0, &root, &base);
	KUNIT_EXPECT_EQ(test, error, 0);
	if (error)
		goto out;
	current->thread.rstack_chain = root;
	mmap_read_lock(mm);
	vma = find_vma(mm, base);
	KUNIT_EXPECT_TRUE(test, vma_is_arch_owned(vma));
	KUNIT_EXPECT_EQ(test, vma->vm_end - vma->vm_start, SZ_1M);
	KUNIT_EXPECT_EQ(test, vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC), VM_READ | VM_WRITE);
	vma = find_vma(mm, base - PAGE_SIZE);
	KUNIT_EXPECT_EQ(test, vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC), 0UL);
	vma = find_vma(mm, base + SZ_1M);
	KUNIT_EXPECT_EQ(test, vma->vm_end - vma->vm_start, 2 * PAGE_SIZE);
	KUNIT_EXPECT_EQ(test, vma->vm_flags & (VM_READ | VM_WRITE | VM_EXEC), 0UL);
	mmap_read_unlock(mm);

	domain = mmix_rstack_domain_begin(mm, root, base, sizeof(value));
	KUNIT_EXPECT_FALSE(test, IS_ERR(domain));
	if (!IS_ERR(domain)) {
		/* Demand allocation while the owner transaction holds no mmap lock. */
		KUNIT_EXPECT_EQ(test,
				copy_to_user((void __user *)base, &value, sizeof(value)), 0UL);
		mmix_rstack_domain_end(mm, domain);
	}
	KUNIT_EXPECT_EQ(test, vm_munmap(base - PAGE_SIZE, SZ_1M + 3 * PAGE_SIZE), -EPERM);
	KUNIT_EXPECT_EQ(test, vm_munmap(base + PAGE_SIZE, PAGE_SIZE), -EPERM);
	KUNIT_EXPECT_EQ(test, sys_mprotect(base, PAGE_SIZE, PROT_NONE), -EPERM);
	KUNIT_EXPECT_EQ(test, sys_mprotect(base - PAGE_SIZE, PAGE_SIZE, PROT_READ), -EPERM);
	KUNIT_EXPECT_EQ(test, sys_mremap(base, PAGE_SIZE, PAGE_SIZE, MREMAP_MAYMOVE, 0), -EPERM);
	KUNIT_EXPECT_EQ(test, sys_mseal(base, PAGE_SIZE, 0), -EPERM);
	KUNIT_EXPECT_EQ(test, do_madvise(mm, base, PAGE_SIZE, MADV_DONTNEED), -EPERM);
	KUNIT_EXPECT_EQ(test, do_madvise(mm, base, PAGE_SIZE, MADV_FREE), -EPERM);
	KUNIT_EXPECT_EQ(test, do_madvise(mm, base, PAGE_SIZE, MADV_DONTFORK), -EPERM);
	KUNIT_EXPECT_EQ(test, do_madvise(mm, base, PAGE_SIZE, MADV_WIPEONFORK), -EPERM);
	KUNIT_EXPECT_EQ(test, do_madvise(mm, base, PAGE_SIZE, MADV_GUARD_INSTALL), -EPERM);
	address = vm_mmap(NULL, base, PAGE_SIZE, PROT_READ | PROT_WRITE,
			  MAP_FIXED | MAP_PRIVATE | MAP_ANONYMOUS, 0);
	KUNIT_EXPECT_EQ(test, (long)address, -EPERM);
	KUNIT_EXPECT_EQ(test, get_user(address, (unsigned long __user *)base), 0);
	KUNIT_EXPECT_EQ(test, address, value);
	/* Prefaulting preserves contents and cannot turn a guard into backing. */
	KUNIT_EXPECT_EQ(test, do_madvise(mm, base, PAGE_SIZE, MADV_POPULATE_READ), 0);
	KUNIT_EXPECT_EQ(test, do_madvise(mm, base, PAGE_SIZE, MADV_POPULATE_WRITE), 0);
	KUNIT_EXPECT_EQ(test, do_madvise(mm, base - PAGE_SIZE, PAGE_SIZE, MADV_POPULATE_WRITE),
			-EINVAL);
	KUNIT_EXPECT_EQ(test, get_user(address, (unsigned long __user *)base), 0);
	KUNIT_EXPECT_EQ(test, address, value);
	address = vm_mmap(NULL, 0, PAGE_SIZE, PROT_READ | PROT_WRITE,
			  MAP_PRIVATE | MAP_ANONYMOUS, 0);
	KUNIT_EXPECT_FALSE(test, IS_ERR_VALUE(address));
	if (!IS_ERR_VALUE(address)) {
		KUNIT_EXPECT_EQ(test, put_user(value, (unsigned long __user *)address), 0);
		KUNIT_EXPECT_EQ(test, sys_mremap(address, PAGE_SIZE, PAGE_SIZE,
						 MREMAP_MAYMOVE | MREMAP_FIXED, base), -EPERM);
		KUNIT_EXPECT_EQ(test, get_user(value, (unsigned long __user *)address), 0);
		KUNIT_EXPECT_EQ(test, value, 0x12345678UL);
		KUNIT_EXPECT_EQ(test, vm_munmap(address, PAGE_SIZE), 0);
	}
	domain = mmix_rstack_domain_begin(mm, root, base - 8, 8);
	KUNIT_EXPECT_TRUE(test, IS_ERR(domain));
	if (!IS_ERR(domain))
		mmix_rstack_domain_end(mm, domain);
	domain = mmix_rstack_domain_begin(mm, root, base + SZ_1M, 8);
	KUNIT_EXPECT_TRUE(test, IS_ERR(domain));
	if (!IS_ERR(domain))
		mmix_rstack_domain_end(mm, domain);
	fork_state.root = root;
	fork_state.base = base;
	error = mmix_rstack_domain_create(mm, 0, &fork_state.orphan, &fork_state.orphan_base);
	KUNIT_EXPECT_EQ(test, error, 0);
	if (!error) {
		error = put_user(0x9876UL, (unsigned long __user *)fork_state.orphan_base);
		KUNIT_EXPECT_EQ(test, error, 0);
		current->thread.rstack_chain = root;
		kernel_sigaction(SIGCHLD, SIG_DFL);
		child = kernel_clone(&args);
		KUNIT_EXPECT_GT(test, child, 0);
		if (child > 0) {
			status = 0;
			KUNIT_EXPECT_EQ(test, kernel_wait(child, &status), child);
			KUNIT_EXPECT_EQ(test, status, 0);
		}
		kernel_sigaction(SIGCHLD, handler);
		current->thread.rstack_chain = root;
		KUNIT_EXPECT_EQ(test, get_user(address, (unsigned long __user *)base), 0);
		KUNIT_EXPECT_EQ(test, address, value);
		KUNIT_EXPECT_EQ(test, sys_mprotect(fork_state.orphan_base, PAGE_SIZE, PROT_NONE),
				-EPERM);
		KUNIT_EXPECT_EQ(test, mmix_rstack_domain_release(mm, fork_state.orphan), 0);
	}
	for (i = 0; i < ARRAY_SIZE(ids); i++) {
		error = mmix_rstack_domain_create(mm, i ? ids[i - 1] : root, &ids[i], &address);
		KUNIT_EXPECT_EQ(test, error, 0);
		if (error)
			break;
	}
	if (i == ARRAY_SIZE(ids)) {
		KUNIT_EXPECT_EQ(test, mmix_rstack_domain_create(mm, ids[i - 1], &extra, &address),
				-EOVERFLOW);
		KUNIT_EXPECT_EQ(test, mmix_rstack_domain_release(mm, root), -EBUSY);
	}
	while (i)
		KUNIT_EXPECT_EQ(test, mmix_rstack_domain_release(mm, ids[--i]), 0);
	KUNIT_EXPECT_EQ(test, mmix_rstack_domain_release(mm, root), 0);
	KUNIT_EXPECT_EQ(test, mmix_rstack_domain_release(mm, root), -ESRCH);
	/* Exact owner release permits later ordinary address reuse. */
	address = vm_mmap(NULL, base - PAGE_SIZE, SZ_1M + 3 * PAGE_SIZE, PROT_READ | PROT_WRITE,
			  MAP_FIXED_NOREPLACE | MAP_PRIVATE | MAP_ANONYMOUS, 0);
	KUNIT_EXPECT_EQ(test, address, base - PAGE_SIZE);
	KUNIT_EXPECT_EQ(test, vm_munmap(address, SZ_1M + 3 * PAGE_SIZE), 0);
	maps = mm->map_count;
	for (i = 0; i < 3; i++) {
		mmix_rstack_domain_fail_after(i);
		KUNIT_EXPECT_EQ(test, mmix_rstack_domain_create(mm, 0, &extra, &address), -ENOMEM);
		KUNIT_EXPECT_EQ(test, mm->map_count, maps);
		mmix_rstack_domain_fail_after(-1);
	}
	for (i = 0; i < 256; i++) {
		error = mmix_rstack_domain_create(mm, 0, &roots[i], &address);
		KUNIT_EXPECT_EQ(test, error, 0);
		if (error)
			break;
		KUNIT_EXPECT_GT(test, roots[i], root);
	}
	if (i == 256)
		KUNIT_EXPECT_EQ(test, mmix_rstack_domain_create(mm, 0, &extra, &address), -ENOMEM);
	while (i)
		KUNIT_EXPECT_EQ(test, mmix_rstack_domain_release(mm, roots[--i]), 0);
	KUNIT_EXPECT_EQ(test, mm->map_count, maps);
	kunit_info(test, "MMIX_DOMAIN protection=ok materialize=ok nesting=ok release=ok\n");
out:
	current->thread.rstack_chain = old_chain;
	kthread_unuse_mm(mm);
	mmput(mm);
}

static struct kunit_case rstack_domain_cases[] = {
	KUNIT_CASE(owned_domains),
	{}
};

static struct kunit_suite rstack_domain_suite = {
	.name = "mmix_rstack_domain",
	.test_cases = rstack_domain_cases,
};

kunit_test_suite(rstack_domain_suite);
