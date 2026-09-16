// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/sizes.h>
#include <linux/sched/mm.h>
#include <linux/sched/signal.h>
#include <linux/sched/task.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/rstack.h>
#include <asm/unistd.h>
#include <asm/tlbflush.h>
#include "user_entry.h"
#include "signal.h"
#include "signal_test.h"

#define CODE (4UL << 20)
#define DATA (6UL << 20)
#define STACK_SIZE SZ_1M

struct signal_test {
	unsigned long mode;
	unsigned int kills, returns, injections, reads;
	unsigned long denied;
	u64 root;
	unsigned int escaped;
	unsigned int steps, restored;
	struct kunit *test;
	struct mmix_user_state expected;
	struct pt_regs expected_regs;
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

static int signal_event(struct mmix_user_entry *entry, void *data)
{
	struct signal_test *t = data;
	unsigned long mode = t->mode == 74		    ? 22 :
			     t->mode >= 75		    ? 0 :
			     t->mode >= 28 && t->mode <= 30 ? t->mode - 6 :
							      t->mode;
	unsigned long nr = entry->regs.syscall_nr;

	int error;

	if (mode >= 32 && entry->event == MMIX_USER_SYSCALL && nr == 0xffff) {
		t->steps++;
		if (t->steps == mode - 31) {
			t->expected = *mmix_user_rstack_state(entry->stack);
			t->expected_regs = entry->regs;
			local_irq_enable();
			send_sig(SIGUSR1, current, 0);
			local_irq_disable();
			t->injections++;
		}
		return 0;
	}
	if (entry->event == MMIX_USER_SYSCALL) {
		if (nr == __NR_read && (mode == 25 || mode == 26)) {
			t->reads++;
			if (t->reads == 1) {
				local_irq_enable();
				send_sig(SIGUSR1, current, 0);
				local_irq_disable();
			}
		}
		if (nr == __NR_exit && mode == 70 &&
		    current->thread.rstack_chain != t->root) {
			t->escaped++;
			pr_info("MMIX_SIGNAL overflow=handled chain=%llu root=%llu\n",
				current->thread.rstack_chain, t->root);
		}
		if (nr == __NR_kill)
			t->kills++;
		if (nr == __NR_rt_sigreturn)
			t->returns++;
	}
	if (mode == 12 && entry->event == MMIX_USER_SYSCALL &&
	    nr == __NR_mmix_rstack_sync) {
		local_irq_enable();
		send_sig(SIGUSR1, current, 0);
		local_irq_disable();
	}
	if ((mode == 22 || mode == 31) && t->denied &&
	    entry->event == MMIX_USER_FAULT &&
	    entry->regs.pc == CODE + mmix_signal_pop - mmix_signal_test_start) {
		backing_read(t->denied, true);
		error = 0;
	} else {
		error = mmix_user_syscall(entry, NULL);
	}
	if ((mode == 22 || mode == 31) && !t->denied &&
	    entry->event == MMIX_USER_SYSCALL && nr == __NR_mmix_rstack_sync) {
		t->denied = entry->capture.r_o - (mode == 31 ? 16 : 8);
		backing_read(t->denied, false);
	}
	if (!error && ((mode >= 22 && mode <= 24) || mode == 31) &&
	    entry->event == MMIX_USER_FAULT && !t->injections) {
		const char *target = (mode == 22 || mode == 31) ?
					     mmix_signal_pop :
				     mode == 23 ? mmix_signal_growth :
						  mmix_signal_push;

		if (entry->regs.pc == CODE + target - mmix_signal_test_start) {
			local_irq_enable();
			send_sig(SIGUSR1, current, 0);
			local_irq_disable();
			t->injections++;
			t->expected = *mmix_user_rstack_state(entry->stack);
			t->expected_regs = entry->regs;
			pr_info("MMIX_SIGNAL partial=%lu L=%lu O=%lx S=%lx XX=%lx YY=%lx ZZ=%lx\n",
				mode, entry->capture.r_l, entry->capture.r_o,
				entry->capture.r_s, entry->regs.r_xx,
				entry->regs.r_yy, entry->regs.r_zz);
		}
	}
	if (t->injections && !t->restored && nr == __NR_rt_sigreturn &&
	    !error && entry->regs.r_ww == t->expected_regs.r_ww) {
		KUNIT_EXPECT_EQ(t->test,
				memcmp(&t->expected,
				       mmix_user_rstack_state(entry->stack),
				       sizeof(t->expected)),
				0);
		KUNIT_EXPECT_EQ(t->test, entry->regs.r_ww,
				t->expected_regs.r_ww);
		KUNIT_EXPECT_EQ(t->test, entry->regs.r_xx,
				t->expected_regs.r_xx);
		KUNIT_EXPECT_EQ(t->test, entry->regs.r_yy,
				t->expected_regs.r_yy);
		KUNIT_EXPECT_EQ(t->test, entry->regs.r_zz,
				t->expected_regs.r_zz);
		t->restored++;
	}
	if ((mode == 11 || mode == 72 || mode == 73) &&
	    entry->event == MMIX_USER_SYSCALL &&
	    entry->regs.syscall_nr == __NR_kill)
		mmix_rstack_domain_fail_after(mode == 11 ? 0 : mode - 71);
	return error;
}

static const struct mmix_user_entry_ops signal_ops = {
	.event = signal_event,
	.write = mmix_user_write,
};

static int signal_child(void *data)
{
	struct mmix_user_state *state;
	unsigned long base, values[31];
	unsigned int i;
	int error;

	current->thread.user_state = mmix_user_rstack_alloc();
	if (!current->thread.user_state ||
	    mmix_rstack_domain_create(current->mm, 0,
				      &current->thread.rstack_chain, &base))
		return 99;
	for (i = 0; i < ARRAY_SIZE(values); i++)
		values[i] = 1000 + i;
	if (copy_to_user((void __user *)(DATA + 4096), values, sizeof(values)))
		return 99;
	state = mmix_user_rstack_state(current->thread.user_state);
	state->regs.pc = CODE;
	state->regs.r_g = 230;
	state->regs.r_o = base;
	state->pending.start = base;
	state->regs.regs[254] = DATA + STACK_SIZE / 2;
	state->regs.regs[243] = ((struct signal_test *)data)->mode;
	state->regs.regs[242] =
		state->regs.regs[243] == 74 ? 22 :
		state->regs.regs[243] >= 75 ? 0 :
		state->regs.regs[243] >= 28 && state->regs.regs[243] <= 30 ?
					      state->regs.regs[243] - 6 :
					      state->regs.regs[243];
	((struct signal_test *)data)->root = current->thread.rstack_chain;
	error = mmix_user_enter(current->thread.user_state, &signal_ops, data);
	return error ? 99 : 98;
}

struct signal_expectation {
	int status;
	unsigned int kills, returns, injections;
};

static const struct signal_expectation expectations[] = {
	[0] = { 0, 1, 1, 0 },
	[1] = { 0, 1, 1, 0 },
	[2] = { 0, 3, 3, 0 },
	[3] = { 0, 1, 1, 0 },
	[4] = { 0, 3, 3, 0 },
	[5] = { 0, 1, 1, 0 },
	[6] = { SIGSEGV, 1, 1, 0 },
	[7] = { SIGSEGV, 1, 1, 0 },
	[8] = { SIGSEGV, 1, 1, 0 },
	[9] = { 0, 1, 1, 0 },
	[10] = { SIGSEGV, 33, 0, 0 },
	[11] = { SIGSEGV, 1, 0, 0 },
	[12] = { 0, 0, 1, 0 },
	[13] = { SIGSEGV, 1, 1, 0 },
	[14] = { SIGSEGV, 1, 1, 0 },
	[15] = { SIGSEGV, 1, 1, 0 },
	[16] = { SIGSEGV, 1, 1, 0 },
	[17] = { SIGSEGV, 1, 1, 0 },
	[18] = { SIGSEGV, 1, 1, 0 },
	[19] = { SIGSEGV, 1, 1, 0 },
	[20] = { SIGSEGV, 1, 1, 0 },
	[21] = { SIGSEGV, 1, 1, 0 },
	[22] = { 0, 0, 1, 1 },
	[23] = { 0, 0, 1, 1 },
	[24] = { 0, 0, 1, 1 },
	[25] = { 0, 0, 1, 0 },
	[26] = { 0, 0, 1, 0 },
	[27] = { SIGSEGV, 0, 1, 0 },
	[28] = { SIGSEGV, 0, 1, 1 },
	[29] = { SIGSEGV, 0, 1, 1 },
	[30] = { SIGSEGV, 0, 1, 1 },
	[31] = { 0, 1, 2, 1 },
	[32 ... 68] = { 0, 0, 1, 1 },
	[69] = { SIGSEGV, 1, 0, 0 },
	[70] = { 0, 0, 0, 0 },
	[71] = { SIGSEGV, 4, 0, 0 },
	[72] = { SIGSEGV, 1, 0, 0 },
	[73] = { SIGSEGV, 1, 0, 0 },
	[74] = { 0, 0, 1, 1 },
	[75] = { 0, 1, 1, 0 },
	[76] = { 0, 1, 1, 0 },
	[77] = { 0, 1, 1, 0 },
};

static void delivery_return_case(struct kunit *test)
{
	struct kernel_clone_args args = { .fn = signal_child,
					  .exit_signal = SIGCHLD };
	__sighandler_t old =
		current->sighand->action[SIGCHLD - 1].sa.sa_handler;
	struct mm_struct *mm = mm_alloc();
	struct signal_test *t = kunit_kzalloc(test, sizeof(*t), GFP_KERNEL);
	int status;
	size_t length = mmix_signal_test_end - mmix_signal_test_start;
	unsigned long mode;
	long baseline = mmix_signal_live();
	unsigned int retry;
	pid_t pid;

	KUNIT_ASSERT_NOT_NULL(test, mm);
	KUNIT_ASSERT_NOT_NULL(test, t);
	mm->mmap_base = TASK_UNMAPPED_BASE;
	kthread_use_mm(mm);
	kernel_sigaction(SIGCHLD, SIG_DFL);
	KUNIT_EXPECT_EQ(test,
			vm_mmap(NULL, CODE, PAGE_SIZE,
				PROT_READ | PROT_WRITE | PROT_EXEC,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0),
			CODE);
	KUNIT_EXPECT_EQ(test,
			vm_mmap(NULL, DATA, STACK_SIZE, PROT_READ | PROT_WRITE,
				MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, 0),
			DATA);
	KUNIT_EXPECT_LE(test, length, PAGE_SIZE);
	KUNIT_EXPECT_EQ(test,
			copy_to_user((void __user *)CODE, mmix_signal_test_start, length), 0UL);
	flush_icache_range(CODE, CODE + PAGE_SIZE);
	for (mode = 0; mode < ARRAY_SIZE(expectations); mode++) {
		const struct signal_expectation *expected = &expectations[mode];

		status = 0;
		memset(t, 0, sizeof(*t));
		t->test = test;
		t->mode = mode;
		args.fn_arg = t;
		pid = kernel_clone(&args);
		KUNIT_EXPECT_GT(test, pid, 0);
		if (pid > 0)
			KUNIT_EXPECT_EQ(test, kernel_wait(pid, &status), pid);
		mmix_rstack_domain_fail_after(-1);
		for (retry = 0; retry < 100 && mmix_signal_live() != baseline;
		     retry++)
			usleep_range(10000, 11000);
		KUNIT_EXPECT_EQ(test, mmix_signal_live(), baseline);
		KUNIT_EXPECT_EQ_MSG(test, status, expected->status, "signal mode %lu", mode);
		KUNIT_EXPECT_EQ(test, t->kills, expected->kills);
		KUNIT_EXPECT_EQ(test, t->returns, expected->returns);
		KUNIT_EXPECT_EQ(test, t->injections, expected->injections);
		if (mode == 25 || mode == 26)
			KUNIT_EXPECT_EQ(test, t->reads, mode == 26 ? 2U : 1U);
		if (mode >= 32 && mode <= 68) {
			KUNIT_EXPECT_EQ(test, t->steps, 37U);
			KUNIT_EXPECT_EQ(test, t->restored, 1U);
		}
		if ((mode >= 22 && mode <= 24) || mode == 31 || mode == 74)
			KUNIT_EXPECT_EQ(test, t->restored, 1U);
		KUNIT_EXPECT_EQ(test, t->escaped, mode == 70 ? 1U : 0U);
		kunit_info(test, "MMIX_SIGNAL mode=%lu status=%d kills=%u returns=%u\n",
			   mode, status, t->kills, t->returns);
	}
	kernel_sigaction(SIGCHLD, old);
	kthread_unuse_mm(mm);
	mmput(mm);
}

static struct kunit_case signal_cases[] = {
	KUNIT_CASE_SLOW(delivery_return_case),
	{}
};

static struct kunit_suite signal_suite = {
	.name = "mmix_signal",
	.test_cases = signal_cases,
};

kunit_test_suite(signal_suite);
