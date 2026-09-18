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

struct shared_domains {
	u64 chain;
	unsigned long base;
};

static int shared_owner_exit(void *data)
{
	struct shared_domains *shared = data;
	u64 id;
	unsigned long base;

	current->thread.rstack_chain = shared->chain;
	current->thread.rstack_owner = mmix_rstack_owner_alloc(current->mm, shared->chain);
	if (IS_ERR(current->thread.rstack_owner)) {
		current->thread.rstack_owner = NULL;
		return 1;
	}
	if (mmix_rstack_domain_create(current->mm, shared->chain, &id, &base))
		return 2;
	if (put_user(0xfeedUL, (unsigned long __user *)shared->base))
		return 3;
	/* A nonzero status also verifies kernel_wait wrote the output. */
	return 37;
}

static void shared_owners(struct kunit *test)
{
	struct mm_struct *mm = mm_alloc();
	struct mmix_rstack_owner *parent, *child, *grandchild, *failed;
	struct mm_struct *foreign;
	struct shared_domains shared;
	struct kernel_clone_args args = {
		.flags = CLONE_VM,
		.fn = shared_owner_exit,
		.fn_arg = &shared,
		.exit_signal = SIGCHLD,
	};
	__sighandler_t handler = current->sighand->action[SIGCHLD - 1].sa.sa_handler;
	u64 root, signal, nested, old_chain = current->thread.rstack_chain;
	unsigned long base, address, value;
	unsigned int i, cycle;
	int error, maps, status;
	pid_t pid;

	KUNIT_ASSERT_NOT_NULL(test, mm);
	KUNIT_ASSERT_NULL(test, current->thread.rstack_owner);
	mm->mmap_base = TASK_UNMAPPED_BASE;
	kthread_use_mm(mm);
	maps = mm->map_count;
	for (cycle = 0; cycle < 100; cycle++) {
		parent = NULL;
		child = NULL;
		grandchild = NULL;
		error = mmix_rstack_domain_create(mm, 0, &root, &base);
		KUNIT_EXPECT_EQ(test, error, 0);
		if (error)
			break;
		parent = mmix_rstack_owner_alloc(mm, root);
		if (IS_ERR(parent)) {
			KUNIT_FAIL(test, "parent claim allocation");
			parent = NULL;
			break;
		}
		current->thread.rstack_owner = parent;
		current->thread.rstack_chain = root;
		KUNIT_EXPECT_EQ(test, put_user(0x1234UL, (unsigned long __user *)base), 0);
		error = mmix_rstack_domain_create(mm, root, &signal, &address);
		KUNIT_EXPECT_EQ(test, error, 0);
		if (error)
			goto cleanup;
		/* Allocation and each ancestor acquisition fail without partial claims. */
		for (i = 0; i < 3; i++) {
			mmix_rstack_domain_fail_after(i);
			failed = mmix_rstack_owner_alloc(mm, signal);
			mmix_rstack_domain_fail_after(-1);
			KUNIT_EXPECT_TRUE(test, IS_ERR(failed));
			if (IS_ERR(failed))
				KUNIT_EXPECT_EQ(test, PTR_ERR(failed), -ENOMEM);
			else
				mmix_rstack_owner_free(failed);
		}
		child = mmix_rstack_owner_alloc(mm, signal);
		if (IS_ERR(child)) {
			child = NULL;
			KUNIT_FAIL(test, "child claim allocation");
			goto cleanup;
		}
		/* A normal/ancestor return drops only this task's inherited claim. */
		KUNIT_EXPECT_EQ(test, mmix_rstack_domain_release(mm, signal), 0);
		KUNIT_EXPECT_EQ(test, mmix_rstack_domain_release(mm, signal), -EPERM);
		KUNIT_EXPECT_EQ(test, sys_mprotect(base, PAGE_SIZE, PROT_NONE), -EPERM);
		current->thread.rstack_owner = child;
		current->thread.rstack_chain = signal;
		error = mmix_rstack_domain_create(mm, signal, &nested, &address);
		KUNIT_EXPECT_EQ(test, error, 0);
		if (error)
			goto cleanup;
		grandchild = mmix_rstack_owner_alloc(mm, nested);
		if (IS_ERR(grandchild)) {
			grandchild = NULL;
			KUNIT_FAIL(test, "descendant claim allocation");
			goto cleanup;
		}
		/* Parent and intermediate child disappear before their descendant. */
		KUNIT_EXPECT_EQ(test, mmix_rstack_owner_free(parent), 0);
		parent = NULL;
		current->thread.rstack_owner = grandchild;
		current->thread.rstack_chain = nested;
		KUNIT_EXPECT_EQ(test, mmix_rstack_owner_free(child), 0);
		child = NULL;
		KUNIT_EXPECT_EQ(test, get_user(value, (unsigned long __user *)base), 0);
		KUNIT_EXPECT_EQ(test, value, 0x1234UL);
		KUNIT_EXPECT_EQ(test, vm_munmap(base, PAGE_SIZE), -EPERM);
cleanup:
		current->thread.rstack_owner = NULL;
		KUNIT_EXPECT_EQ(test, mmix_rstack_owner_free(grandchild), 0);
		KUNIT_EXPECT_EQ(test, mmix_rstack_owner_free(child), 0);
		KUNIT_EXPECT_EQ(test, mmix_rstack_owner_free(parent), 0);
		KUNIT_EXPECT_EQ(test, mm->map_count, maps);
	}
	KUNIT_EXPECT_EQ(test, cycle, 100U);
	/* Failed-fork rollback must never charge munmap to the caller's mm. */
	foreign = mm_alloc();
	KUNIT_EXPECT_NOT_NULL(test, foreign);
	if (foreign) {
		foreign->mmap_base = TASK_UNMAPPED_BASE;
		kthread_unuse_mm(mm);
		kthread_use_mm(foreign);
		error = mmix_rstack_domain_create(foreign, 0, &root, &base);
		KUNIT_EXPECT_EQ(test, error, 0);
		parent = error ? ERR_PTR(error) : mmix_rstack_owner_alloc(foreign, root);
		kthread_unuse_mm(foreign);
		kthread_use_mm(mm);
		if (!IS_ERR(parent)) {
			KUNIT_EXPECT_EQ(test, mmix_rstack_owner_free(parent), 0);
			KUNIT_EXPECT_EQ(test, foreign->map_count, 3);
		} else {
			KUNIT_FAIL(test, "foreign-mm ownership allocation");
		}
		KUNIT_EXPECT_EQ(test, mm->map_count, maps);
		mmput(foreign);
	}
	/* Failed retirement retains the protected reservation until collection. */
	error = mmix_rstack_domain_create(mm, 0, &root, &base);
	KUNIT_EXPECT_EQ(test, error, 0);
	if (!error) {
		parent = mmix_rstack_owner_alloc(mm, root);
		if (!IS_ERR(parent)) {
			mmix_rstack_domain_fail_release();
			KUNIT_EXPECT_EQ(test, mmix_rstack_owner_free(parent), -ENOMEM);
			KUNIT_EXPECT_EQ(test, vm_munmap(base, PAGE_SIZE), -EPERM);
			error = mmix_rstack_domain_create(mm, 0, &signal, &address);
			KUNIT_EXPECT_EQ(test, error, 0);
			if (!error) {
				parent = mmix_rstack_owner_alloc(mm, signal);
				if (!IS_ERR(parent))
					KUNIT_EXPECT_EQ(test, mmix_rstack_owner_free(parent), 0);
				else
					KUNIT_FAIL(test, "retry claim allocation");
			}
		} else {
			KUNIT_FAIL(test, "retirement claim allocation");
		}
	}
	KUNIT_EXPECT_EQ(test, mm->map_count, maps);
	/* Exercise the real old-mm exit hook while the parent's mm remains live. */
	error = mmix_rstack_domain_create(mm, 0, &root, &base);
	KUNIT_EXPECT_EQ(test, error, 0);
	if (!error) {
		parent = mmix_rstack_owner_alloc(mm, root);
		if (!IS_ERR(parent)) {
			current->thread.rstack_owner = parent;
			current->thread.rstack_chain = root;
			shared.chain = root;
			shared.base = base;
			kernel_sigaction(SIGCHLD, SIG_DFL);
			for (i = 0; i < 2; i++) {
				args.flags = i ? 0 : CLONE_VM;
				error = put_user(0x1234UL, (unsigned long __user *)base);
				KUNIT_EXPECT_EQ(test, error, 0);
				pid = kernel_clone(&args);
				KUNIT_EXPECT_GT(test, pid, 0);
				if (pid > 0) {
					status = -1;
					KUNIT_EXPECT_EQ(test, kernel_wait(pid, &status), pid);
					KUNIT_EXPECT_EQ(test, status, 37);
					error = get_user(value, (unsigned long __user *)base);
					KUNIT_EXPECT_EQ(test, error, 0);
					KUNIT_EXPECT_EQ(test, value, i ? 0x1234UL : 0xfeedUL);
				}
			}
			kernel_sigaction(SIGCHLD, handler);
			KUNIT_EXPECT_EQ(test, mm->map_count, maps + 3);
			mmix_rstack_detach(current, mm);
			mmix_rstack_detach(current, mm);
		} else {
			KUNIT_FAIL(test, "exit-hook parent allocation");
		}
	}
	KUNIT_EXPECT_EQ(test, mm->map_count, maps);
	current->thread.rstack_chain = old_chain;
	kthread_unuse_mm(mm);
	mmput(mm);
	kunit_info(test, "MMIX_OWNER claims=ok rollback=ok retirement=ok exit=ok cycles=100\n");
}

static struct kunit_case rstack_domain_cases[] = {
	KUNIT_CASE(owned_domains),
	KUNIT_CASE(shared_owners),
	{}
};

static struct kunit_suite rstack_domain_suite = {
	.name = "mmix_rstack_domain",
	.test_cases = rstack_domain_cases,
};

kunit_test_suite(rstack_domain_suite);
