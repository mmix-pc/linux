// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/mman.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/sizes.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/rstack.h>
#include <asm/tlbflush.h>
#include <asm/unistd.h>
#include "signal.h"
#include "user_entry.h"
#include "rstack_jump_test.h"

#define CODE (4UL << 20)
#define DATA (6UL << 20)
#define ENV (DATA + 4096)
#define REQUEST (DATA + 256)
#define CASES 75

struct jump_test {
	struct mmix_rstack_owner *borrower;
	struct mm_struct *borrow_mm;
	unsigned long borrow_address;
	struct kunit *test;
	unsigned long mode, denied;
	u64 root, orphan, stale, outer;
	unsigned int jumps, landed, returns, injected, steps, reads;
	int rejected;
	struct mmix_user_state leave;
	sigset_t mask;
	unsigned long alt_sp, alt_size, alt_flags;
	long activations;
};

/* Deliberately deny one backing read to expose a real partial POP. */
static void backing_read(unsigned long address, bool allow)
{
	struct mm_struct *mm = current->mm;
	pgd_t *pgd;
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	/* The leaf page-table lock protects the injected permission change. */
	spinlock_t *lock;

	local_irq_enable();
	mmap_read_lock(mm);
	pgd = pgd_offset(mm, address);
	p4d = p4d_offset(pgd, address);
	pud = pud_offset(p4d, address);
	pmd = pmd_offset(pud, address);
	pte = pte_offset_map_lock(mm, pmd, address, &lock);
	if (allow)
		set_pte(pte, __pte(pte_val(*pte) | _PAGE_READ));
	else
		set_pte(pte, __pte(pte_val(*pte) & ~_PAGE_READ));
	pte_unmap_unlock(pte, lock);
	mmap_read_unlock(mm);
	flush_tlb_all();
	local_irq_disable();
}

static int invalid_request(struct jump_test *t, struct mmix_user_entry *entry)
{
	struct mmix_rstack_jump request;
	int expected = -EINVAL;

	if (copy_from_user(&request, (void __user *)REQUEST, sizeof(request)))
		return -EFAULT;
	switch (t->mode) {
	case 44:
		request.flags = 2;
		break;
	case 45:
		request.sigmask = 1;
		break;
	case 46:
		request.result = 0;
		break;
	case 47:
		request.result = 0x80000000UL;
		break;
	case 48:
		entry->regs.syscall_args[0]++;
		break;
	case 49:
		entry->regs.syscall_args[0] = PAGE_SIZE;
		expected = -EFAULT;
		break;
	case 50:
		request.landing++;
		break;
	case 51:
		request.landing = TASK_SIZE;
		expected = -EFAULT;
		break;
	case 52:
		request.environment++;
		break;
	case 53:
		request.environment = TASK_SIZE;
		expected = -EFAULT;
		break;
	case 54:
		request.chain_id = 0;
		break;
	case 55:
		request.chain_id = ~0ULL;
		expected = -ESRCH;
		break;
	case 56:
		request.chain_id = t->orphan;
		expected = -ESRCH;
		break;
	case 57:
		request.chain_id = t->stale;
		expected = -ESRCH;
		break;
	case 58:
		entry->regs.syscall_args[0] = TASK_SIZE - 8;
		expected = -EFAULT;
		break;
	default:
		return 0;
	}
	if (copy_to_user((void __user *)REQUEST, &request, sizeof(request)))
		return -EFAULT;
	return expected;
}

static int jump_event(struct mmix_user_entry *entry, void *data)
{
	struct jump_test *t = data;
	struct kunit *test = t->test;
	unsigned long nr = entry->regs.syscall_nr, mode = t->mode;
	struct mmix_user_state *state = mmix_user_rstack_state(entry->stack);
	u64 before = current->thread.rstack_chain;
	int expected = 0, error;

	if (entry->event == MMIX_USER_SYSCALL && nr == 0xffff) {
		t->steps++;
		if (((mode >= 10 && mode <= 40) ||
		     (mode >= 68 && mode <= 73)) &&
		    t->steps == (mode >= 68 ? mode - 36 : mode - 9)) {
			local_irq_enable();
			send_sig(SIGUSR1, current, 0);
			local_irq_disable();
			t->injected++;
		}
		return 0;
	}
	if (entry->event == MMIX_USER_SYSCALL && nr == 0xfffe) {
		unsigned long fp, sp;

		t->landed++;
		local_irq_enable();
		KUNIT_EXPECT_EQ(test, get_user(fp, (unsigned long __user *)ENV), 0);
		KUNIT_EXPECT_EQ(test, get_user(sp, (unsigned long __user *)(ENV + 16)), 0);
		local_irq_disable();
		KUNIT_EXPECT_EQ(test, state->regs.regs[253], (u64)fp);
		KUNIT_EXPECT_EQ(test, state->regs.regs[254], (u64)sp);
		KUNIT_EXPECT_EQ(test, state->regs.regs[235], t->leave.regs.regs[235]);
		KUNIT_EXPECT_EQ(test, state->regs.regs[236], t->leave.regs.regs[236]);
		KUNIT_EXPECT_EQ(test, current->thread.rstack_chain,
				mode == 4 || mode == 8 || mode == 63 ?
					t->outer :
					t->root);
		KUNIT_EXPECT_EQ(test, state->regs.regs[230], 0x1234ULL);
		KUNIT_EXPECT_EQ(test, state->regs.r_d, t->leave.regs.r_d);
		KUNIT_EXPECT_EQ(test, state->regs.r_e, t->leave.regs.r_e);
		KUNIT_EXPECT_EQ(test, state->regs.r_h, t->leave.regs.r_h);
		if (mode == 5)
			KUNIT_EXPECT_EQ(test, current->blocked.sig[0],
					sigmask(SIGUSR2));
		if (mode == 6)
			KUNIT_EXPECT_TRUE(test, sigismember(&current->blocked,
							    SIGUSR1));
		if (mode == 7) {
			KUNIT_EXPECT_EQ(test, current->sas_ss_sp,
					DATA + 0xf0000);
			KUNIT_EXPECT_EQ(test, current->sas_ss_flags,
					SS_AUTODISARM);
		}
		if (mode == 8)
			KUNIT_EXPECT_EQ(test, current->sas_ss_sp,
					DATA + 0xe0000);
		return 0;
	}
	if (entry->event == MMIX_USER_SYSCALL && nr == __NR_rt_sigreturn) {
		t->returns++;
		t->stale = before;
	}
	if (entry->event == MMIX_USER_SYSCALL && nr == __NR_read &&
	    mode == 43) {
		t->reads++;
		local_irq_enable();
		send_sig(SIGUSR1, current, 0);
		local_irq_disable();
	}
	if (entry->event == MMIX_USER_SYSCALL && nr == __NR_mmix_rstack_query &&
	    before != t->root && !t->outer)
		t->outer = before;
	if (entry->event == MMIX_USER_SYSCALL && nr == __NR_mmix_rstack_jump) {
		t->jumps++;
		t->leave = *state;
		t->mask = current->blocked;
		t->alt_sp = current->sas_ss_sp;
		t->alt_size = current->sas_ss_size;
		t->alt_flags = current->sas_ss_flags;
		t->activations = mmix_signal_live();
		local_irq_enable();
		expected = invalid_request(t, entry);
		if (mode == 59)
			mmix_user_rstack_write_limit(0);
		if (mode == 62)
			mmix_user_rstack_fail_after(0);
		local_irq_disable();
	}
	if (mode == 2 && entry->event == MMIX_USER_SYSCALL &&
	    nr == __NR_mmix_rstack_jump && !t->borrower) {
		KUNIT_EXPECT_NE(test, current->thread.rstack_chain, t->root);
		local_irq_enable();
		t->borrower =
			mmix_rstack_owner_alloc(current->mm, current->thread.rstack_chain);
		KUNIT_EXPECT_FALSE(t->test, IS_ERR(t->borrower));
		if (IS_ERR(t->borrower)) {
			t->borrower = NULL;
		} else {
			t->borrow_mm = current->mm;
			mmget(t->borrow_mm);
			t->borrow_address = mmix_user_rstack_state(entry->stack)->regs.r_o;
		}
		local_irq_disable();
	}
	if (entry->event == MMIX_USER_FAULT && t->denied &&
	    entry->regs.pc == CODE + mmix_jump_pop - mmix_jump_test_start) {
		backing_read(t->denied, true);
		KUNIT_EXPECT_LT(test, entry->regs.r_xx, 1UL << 63);
		local_irq_enable();
		send_sig(SIGUSR1, current, 0);
		local_irq_disable();
		t->injected++;
		pr_info("MMIX_JUMP partial mode=%lu L=%lu O=%lx S=%lx\n", mode,
			entry->capture.r_l, entry->capture.r_o,
			entry->capture.r_s);
		return 0;
	}
	error = mmix_user_syscall(entry, NULL);
	if (!error && !t->injected && entry->event == MMIX_USER_FAULT &&
	    ((mode == 64 && entry->regs.pc == CODE + mmix_jump_growth -
						      mmix_jump_test_start) ||
	     (mode == 65 && entry->regs.pc == CODE + mmix_jump_push -
						      mmix_jump_test_start))) {
		local_irq_enable();
		send_sig(SIGUSR1, current, 0);
		local_irq_disable();
		t->injected++;
		pr_info("MMIX_JUMP abandon mode=%lu XX=%lx\n", mode,
			entry->regs.r_xx);
	}
	if (entry->event == MMIX_USER_SYSCALL && nr == __NR_mmix_rstack_sync &&
	    (mode == 41 || mode == 42 || mode == 67) && !t->denied) {
		t->denied = entry->capture.r_o - (mode == 67 ? 16 : 8);
		backing_read(t->denied, false);
	}
	if (entry->event == MMIX_USER_SYSCALL && nr == __NR_mmix_rstack_jump) {
		if (expected) {
			KUNIT_EXPECT_EQ(test, state->regs.regs[231],
					(u64)(long)expected);
			KUNIT_EXPECT_EQ(test, current->thread.rstack_chain,
					before);
			t->rejected++;
			KUNIT_EXPECT_EQ(test,
					memcmp(&current->blocked, &t->mask,
					       sizeof(t->mask)),
					0);
			KUNIT_EXPECT_EQ(test, current->sas_ss_sp, t->alt_sp);
			KUNIT_EXPECT_EQ(test, current->sas_ss_size,
					t->alt_size);
			KUNIT_EXPECT_EQ(test, current->sas_ss_flags,
					t->alt_flags);
			KUNIT_EXPECT_EQ(test, mmix_signal_live(),
					t->activations);
			t->leave.regs.regs[231] = state->regs.regs[231];
			KUNIT_EXPECT_EQ(test, memcmp(&t->leave, state, sizeof(*state)), 0);
		} else if (!error) {
			KUNIT_EXPECT_EQ(test, entry->regs.r_xx, 1UL << 63);
			KUNIT_EXPECT_EQ(test, entry->regs.r_yy, 0UL);
			KUNIT_EXPECT_EQ(test, entry->regs.r_zz, 0UL);
			KUNIT_EXPECT_EQ(test, entry->regs.syscall_nr, -1L);
			KUNIT_EXPECT_FALSE(test, entry->syscall_result);
			KUNIT_EXPECT_TRUE(test, current->restart_block.fn ==
							do_no_restart_syscall);
			KUNIT_EXPECT_EQ(test,
					memcmp(&state->regs.r_a,
					       &t->leave.regs.r_a,
					       4 * sizeof(u64)),
					0);
			KUNIT_EXPECT_EQ(test, state->regs.r_h,
					t->leave.regs.r_h);
			KUNIT_EXPECT_EQ(test,
					memcmp(&state->regs.r_m,
					       &t->leave.regs.r_m,
					       7 * sizeof(u64)),
					0);
			KUNIT_EXPECT_EQ(test, state->regs.regs[230],
					t->leave.regs.regs[230]);
			KUNIT_EXPECT_EQ(test,
					memcmp(state->regs.regs + 233,
					       t->leave.regs.regs + 233,
					       20 * sizeof(u64)),
					0);
			if (before != current->thread.rstack_chain) {
				local_irq_enable();
				KUNIT_EXPECT_EQ(test,
						mmix_rstack_domain_release(current->mm, before),
						t->borrower ? -EPERM : -ESRCH);
				local_irq_disable();
			}
			KUNIT_EXPECT_EQ(test, mmix_signal_live(),
					mode == 4 || mode == 8 || mode == 63 ? 1L : 0L);
			if (mode == 9 || (mode == 74 && t->jumps == 1)) {
				local_irq_enable();
				send_sig(mode == 9 ? SIGUSR2 : SIGUSR1, current,
					 0);
				local_irq_disable();
				t->injected++;
			}
		}
	}
	return error;
}

static const struct mmix_user_entry_ops jump_ops = {
	.event = jump_event,
	.write = mmix_user_write,
};

static int jump_child(void *data)
{
	struct jump_test *t = data;
	struct mmix_user_state *state;
	unsigned long base, orphan_base;
	int error;

	current->thread.user_state = mmix_user_rstack_alloc();
	if (!current->thread.user_state ||
	    mmix_rstack_domain_create(current->mm, 0,
				      &current->thread.rstack_chain, &base))
		return 99;
	current->thread.rstack_owner =
		mmix_rstack_owner_alloc(current->mm, current->thread.rstack_chain);
	if (IS_ERR(current->thread.rstack_owner)) {
		current->thread.rstack_owner = NULL;
		return 99;
	}
	t->root = current->thread.rstack_chain;
	if (t->mode == 56 &&
	    mmix_rstack_domain_create(current->mm, 0, &t->orphan, &orphan_base))
		return 99;
	state = mmix_user_rstack_state(current->thread.user_state);
	state->regs.pc = CODE;
	state->regs.r_g = 230;
	state->regs.r_o = base;
	state->pending.start = base;
	state->regs.regs[254] = DATA + SZ_512K;
	state->regs.regs[242] = t->mode;
	state->regs.regs[230] = 0x1234;
	state->regs.r_d = 9;
	state->regs.r_e = 10;
	state->regs.r_h = 11;
	state->regs.r_m = 0x12345000;
	state->regs.r_p = 0x456;
	state->regs.r_r = 0x789;
	state->regs.r_w = 0xabc;
	state->regs.r_x = 0xdef;
	state->regs.r_y = 0x123;
	state->regs.r_z = 0x234;
	state->regs.regs[235] = 0x5555;
	state->regs.regs[236] = 0x6666;
	error = mmix_user_enter(current->thread.user_state, &jump_ops, data);
	return error ? 99 : 98;
}

static void transfer_case(struct kunit *test)
{
	struct kernel_clone_args args = { .fn = jump_child,
					  .exit_signal = SIGCHLD };
	__sighandler_t old =
		current->sighand->action[SIGCHLD - 1].sa.sa_handler;
	struct mm_struct *mm = mm_alloc();
	struct jump_test *t = kunit_kzalloc(test, sizeof(*t), GFP_KERNEL);
	size_t length = mmix_jump_test_end - mmix_jump_test_start;
	unsigned long mode;
	unsigned int retry;
	long baseline = mmix_signal_live();
	int status;
	pid_t pid;

	KUNIT_ASSERT_NOT_NULL(test, mm);
	KUNIT_ASSERT_NOT_NULL(test, t);
	mm->mmap_base = TASK_UNMAPPED_BASE;
	kthread_use_mm(mm);
	kernel_sigaction(SIGCHLD, SIG_DFL);
	KUNIT_ASSERT_EQ(test,
			vm_mmap(NULL, CODE, PAGE_SIZE,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0),
			CODE);
	KUNIT_ASSERT_EQ(test,
			vm_mmap(NULL, DATA, SZ_1M, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0),
			DATA);
	KUNIT_ASSERT_LE(test, length, PAGE_SIZE);
	KUNIT_ASSERT_EQ(test,
			copy_to_user((void __user *)CODE, mmix_jump_test_start,
				     length),
			0UL);
	flush_icache_range(CODE, CODE + PAGE_SIZE);
	for (mode = 0; mode < CASES; mode++) {
		bool fatal = mode == 59 || mode == 60 || mode == 62;
		bool reject = mode >= 44 && mode <= 58;

		memset(t, 0, sizeof(*t));
		t->test = test;
		t->mode = mode;
		args.fn_arg = t;
		status = 0;
		pid = kernel_clone(&args);
		KUNIT_EXPECT_GT(test, pid, 0);
		if (pid > 0)
			KUNIT_EXPECT_EQ(test, kernel_wait(pid, &status), pid);
		if (mode == 2)
			KUNIT_EXPECT_NOT_NULL(test, t->borrower);
		if (t->borrower) {
			struct vm_area_struct *vma;
			bool owned;

			mmap_read_lock(t->borrow_mm);
			vma = find_vma(t->borrow_mm, t->borrow_address);
			owned = vma && vma->vm_start <= t->borrow_address &&
				vma_is_arch_owned(vma);
			KUNIT_EXPECT_TRUE(test, owned);
			mmap_read_unlock(t->borrow_mm);
			/* The held mm outlives its child; adopt it for munmap accounting. */
			kthread_unuse_mm(mm);
			kthread_use_mm(t->borrow_mm);
			KUNIT_EXPECT_EQ(test, mmix_rstack_owner_free(t->borrower), 0);
			mmap_read_lock(t->borrow_mm);
			vma = find_vma(t->borrow_mm, t->borrow_address);
			KUNIT_EXPECT_TRUE(test, !vma || vma->vm_start > t->borrow_address);
			mmap_read_unlock(t->borrow_mm);
			kthread_unuse_mm(t->borrow_mm);
			kthread_use_mm(mm);
			mmput(t->borrow_mm);
			kunit_info(test, "MMIX_OWNER jump=retained-and-released\n");
		}
		mmix_user_rstack_fail_after(-1);
		mmix_user_rstack_write_limit(-1);
		for (retry = 0; retry < 100 && mmix_signal_live() != baseline;
		     retry++)
			usleep_range(10000, 11000);
		KUNIT_EXPECT_EQ(test, mmix_signal_live(), baseline);
		KUNIT_EXPECT_EQ_MSG(test, status,
				    fatal      ? SIGSEGV :
				    mode == 66 ? 99 << 8 :
						 0,
				    "jump mode %lu", mode);
		KUNIT_EXPECT_EQ(test, t->rejected, reject ? 1 : 0);
		KUNIT_EXPECT_EQ(test, t->landed,
				fatal || reject || mode == 66 ? 0U : 1U);
		KUNIT_EXPECT_EQ(test, t->jumps,
				((mode >= 10 && mode <= 40) || mode >= 68) ? 2U : 1U);
		if ((mode >= 10 && mode <= 42) || mode == 9 || mode == 64 ||
		    mode == 65 || mode == 67 || mode >= 68)
			KUNIT_EXPECT_EQ(test, t->injected, 1U);
		KUNIT_EXPECT_EQ(test, t->returns,
				mode == 4 || mode == 8 || mode == 9 || mode == 55 ||
				mode == 57 || mode == 63 ? 1U : 0U);
		if (mode == 43)
			KUNIT_EXPECT_EQ(test, t->reads, 1U);
		kunit_info(test,
			   "MMIX_JUMP mode=%lu status=%d jumps=%u landed=%u rejected=%d\n",
			   mode, status, t->jumps, t->landed, t->rejected);
	}
	kernel_sigaction(SIGCHLD, old);
	kthread_unuse_mm(mm);
	mmput(mm);
}

static struct kunit_case jump_cases[] = { KUNIT_CASE_SLOW(transfer_case), {} };

static struct kunit_suite jump_suite = {
	.name = "mmix_rstack_jump",
	.test_cases = jump_cases,
};

kunit_test_suite(jump_suite);
