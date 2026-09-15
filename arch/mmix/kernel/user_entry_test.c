// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/kthread.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include "user_entry.h"
#include "user_entry_test.h"

#define CODE (4UL << 20)
#define STACK (6UL << 20)
#define STACK_SIZE (8 * PAGE_SIZE)

enum user_test_mode {
	TEST_ROUNDTRIP,
	TEST_DEEP,
	TEST_KERNEL_ADDRESS,
	TEST_SPILL_FAULT,
	TEST_SAVE_FAULT,
	TEST_USER_TP,
	TEST_PRIVILEGED,
	TEST_UNSAVE_FAULT,
	TEST_DENIED_RETURN,
};

struct user_test {
	struct kunit *test;
	struct mm_struct *mm;
	pmd_t *pmd;
	pgtable_t pte;
	struct page *code, *stack;
	struct mmix_user_rstack_state *state;
	unsigned int stops;
	enum user_test_mode mode;
	unsigned long writes, interrupts;
	unsigned int faults;
	struct completion ready;
	bool running, preempted;
};

static int write_backing(void *data, unsigned long address, const void *source, size_t size)
{
	struct user_test *t = data;
	pte_t *pte = page_address(t->pte);
	unsigned long page;

	if (address < STACK || address > STACK + STACK_SIZE || size > STACK + STACK_SIZE - address)
		return -EFAULT;
	for (page = address & PAGE_MASK; page < address + size; page += PAGE_SIZE)
		if (!pte_write(pte[page >> PAGE_SHIFT]))
			return -EFAULT;
	memcpy(page_address(t->stack) + address - STACK, source, size);
	t->writes += size;
	return 0;
}

static int user_stop(struct mmix_user_entry *entry, void *data)
{
	struct user_test *t = data;
	struct mmix_user_state *s = mmix_user_rstack_state(entry->stack);

	t->stops++;
	t->interrupts = entry->interrupts;
	if (t->mode == TEST_DENIED_RETURN) {
		pte_t *pte = page_address(t->pte);

		KUNIT_EXPECT_GT(t->test, s->pending.count, 0UL);
		set_pte(&pte[STACK >> PAGE_SHIFT], pfn_pte(page_to_pfn(t->stack), PAGE_KERNEL_RO));
		flush_tlb_all();
		return 0;
	}
	if (t->mode == TEST_UNSAVE_FAULT && t->stops <= 2) {
		pte_t *pte = page_address(t->pte);

		if (t->stops == 1) {
			KUNIT_EXPECT_EQ(t->test, entry->event, MMIX_USER_SYSCALL);
			set_pte(&pte[STACK >> PAGE_SHIFT],
				pfn_pte(page_to_pfn(t->stack),
					__pgprot(pgprot_val(PAGE_KERNEL) & ~_PAGE_READ)));
		} else {
			KUNIT_EXPECT_EQ(t->test, entry->event, MMIX_USER_FAULT);
			KUNIT_EXPECT_EQ(t->test, entry->regs.r_q & (0xffUL << 32), 1UL << 39);
			t->faults++;
			set_pte(&pte[STACK >> PAGE_SHIFT],
				pfn_pte(page_to_pfn(t->stack), PAGE_KERNEL));
		}
		flush_tlb_all();
		return 0;
	}
	if ((t->mode == TEST_SPILL_FAULT || t->mode == TEST_SAVE_FAULT) &&
	    entry->event == MMIX_USER_FAULT) {
		pte_t *pte = page_address(t->pte);

		t->faults++;
		KUNIT_EXPECT_EQ(t->test, entry->regs.r_q & (0xffUL << 32), 1UL << 38);
		KUNIT_EXPECT_EQ(t->test, entry->capture.r_s, STACK + PAGE_SIZE);
		KUNIT_EXPECT_PTR_EQ(t->test,
				    memchr_inv(page_address(t->stack) + PAGE_SIZE, 0, PAGE_SIZE),
				    NULL);
		/* Only the test's explicit fault policy repairs user backing permission. */
		set_pte(&pte[(STACK >> PAGE_SHIFT) + 1],
			pfn_pte(page_to_pfn(t->stack) + 1, PAGE_KERNEL));
		flush_tlb_all();
		return 0;
	}
	if (t->mode == TEST_KERNEL_ADDRESS) {
		KUNIT_EXPECT_EQ(t->test, entry->regs.r_yy, 0x2000000000000000UL);
		KUNIT_EXPECT_EQ(t->test, entry->regs.r_xx >> 32, 0x03000000UL);
		return 1;
	}
	if (t->mode == TEST_USER_TP) {
		KUNIT_EXPECT_EQ(t->test, s->regs.regs[230], 1UL << 63);
		return 1;
	}
	if (t->mode == TEST_PRIVILEGED) {
		KUNIT_EXPECT_EQ(t->test, entry->event, MMIX_USER_FAULT);
		KUNIT_EXPECT_NE(t->test, entry->regs.r_q & (1UL << 35), 0UL);
		return 1;
	}
	KUNIT_EXPECT_EQ(t->test, entry->event, MMIX_USER_SYSCALL);
	KUNIT_EXPECT_EQ(t->test, entry->regs.r_xx & 0xffffffffUL, 0x00010000UL);
	KUNIT_EXPECT_EQ(t->test, s->regs.regs[230], 0x2345UL);
	KUNIT_EXPECT_EQ(t->test, s->regs.regs[231], 0x1234UL);
	KUNIT_EXPECT_EQ(t->test, s->regs.r_a, 0x30000UL);
	KUNIT_EXPECT_EQ(t->test, s->regs.r_d, 0x77UL);
	KUNIT_EXPECT_EQ(t->test, s->regs.r_h, 0x6789UL);
	KUNIT_EXPECT_EQ(t->test, s->regs.r_m, 0x1234UL);
	KUNIT_EXPECT_EQ(t->test, s->regs.r_p, 0x5aUL);
	KUNIT_EXPECT_EQ(t->test, s->regs.r_r, 0x45UL);
	if (t->mode == TEST_DEEP || t->mode == TEST_SPILL_FAULT) {
		KUNIT_EXPECT_EQ(t->test, s->regs.r_o, STACK);
		KUNIT_EXPECT_GT(t->test, t->interrupts, 0UL);
		return 1;
	}
	if (t->mode == TEST_SAVE_FAULT || t->mode == TEST_UNSAVE_FAULT) {
		KUNIT_EXPECT_EQ(t->test, s->regs.regs[0], 0x4567UL);
		KUNIT_EXPECT_EQ(t->test, s->regs.regs[63], 0x6789UL);
		return 1;
	}
	KUNIT_EXPECT_EQ(t->test, s->regs.regs[29], 0x7654UL);
	if (t->stops == 1) {
		s->regs.regs[232] = 0xabcd;
		return 0;
	}
	KUNIT_EXPECT_EQ(t->test, s->regs.regs[233], 0xabcdUL);
	return 1;
}

static const struct mmix_user_entry_ops ops = {
	.event = user_stop,
	.write = write_backing,
};

static void release_test(struct user_test *t)
{
	mmix_user_rstack_free(&t->state);
	if (t->mm)
		pgd_clear(t->mm->pgd);
	if (t->code)
		__free_page(t->code);
	if (t->stack)
		__free_pages(t->stack, 3);
	if (t->pte)
		pte_free(t->mm, t->pte);
	if (t->pmd)
		pmd_free(t->mm, t->pmd);
	if (t->mm)
		mmput(t->mm);
}

static bool prepare_test(struct user_test *t)
{
	pte_t *pte;
	unsigned int i;

	t->mm = mm_alloc();
	if (!t->mm)
		return false;
	t->pmd = pmd_alloc_one(t->mm, CODE);
	t->pte = pte_alloc_one(t->mm);
	t->code = alloc_page(GFP_KERNEL | __GFP_ZERO);
	t->stack = alloc_pages(GFP_KERNEL | __GFP_ZERO, 3);
	t->state = mmix_user_rstack_alloc();
	if (!t->pmd || !t->pte || !t->code || !t->stack || !t->state)
		return false;
	pud_populate(t->mm, (pud_t *)t->mm->pgd, t->pmd);
	pmd_populate(t->mm, t->pmd, t->pte);
	pte = page_address(t->pte);
	set_pte(&pte[CODE >> PAGE_SHIFT],
		pfn_pte(page_to_pfn(t->code), __pgprot(pgprot_val(PAGE_KERNEL_RO) | _PAGE_EXEC)));
	for (i = 0; i < 8; i++)
		set_pte(&pte[(STACK >> PAGE_SHIFT) + i],
			pfn_pte(page_to_pfn(t->stack) + i, PAGE_KERNEL));
	memcpy(page_address(t->code), mmix_user_test_start,
	       mmix_user_test_end - mmix_user_test_start);
	flush_tlb_all();
	return true;
}

static int run_user(struct user_test *t, const unsigned char *code)
{
	struct mmix_user_state *s = mmix_user_rstack_state(t->state);
	int result;

	memset(s, 0, sizeof(*s));
	memset(page_address(t->stack), 0, STACK_SIZE);
	s->regs.pc = CODE + code - mmix_user_test_start;
	s->regs.r_o = STACK + (t->mode == TEST_SAVE_FAULT ? PAGE_SIZE : 0);
	s->regs.r_g = 230;
	s->regs.r_a = 0x30000;
	s->regs.r_d = 0x77;
	s->regs.r_h = 0x6789;
	s->regs.r_m = 0x1234;
	s->regs.r_p = 0x5a;
	s->regs.r_r = 0x45;
	s->regs.regs[230] = 0x2345;
	s->regs.regs[254] = MMIX_TASK_SIZE;
	s->pending.start = s->regs.r_o;
	t->stops = 0;
	t->interrupts = 0;
	t->faults = 0;
	kthread_use_mm(t->mm);
	WRITE_ONCE(t->running, true);
	result = mmix_user_enter(t->state, &ops, t);
	WRITE_ONCE(t->running, false);
	kthread_unuse_mm(t->mm);
	return result;
}

static int preempt_user(void *data)
{
	struct user_test *t = data;

	complete(&t->ready);
	while (!kthread_should_stop()) {
		unsigned long pc, spin = CODE + mmix_user_test_spin - mmix_user_test_start;

		usleep_range(10000, 11000);
		pc = READ_ONCE(mmix_user_rstack_state(t->state)->regs.pc);
		/* Require a captured PC inside the user loop, not merely API activity. */
		if (READ_ONCE(t->running) && (pc == spin || pc == spin + 4))
			WRITE_ONCE(t->preempted, true);
	}
	return 0;
}

static void user_entry_roundtrips(struct kunit *test)
{
	struct user_test t = { .test = test };
	struct task_struct *worker = NULL;
	pte_t *pte;

	if (!prepare_test(&t)) {
		KUNIT_FAIL(test, "user fixture allocation");
		goto out;
	}
	KUNIT_EXPECT_EQ(test, run_user(&t, mmix_user_test_start), 0);
	KUNIT_EXPECT_EQ(test, t.stops, 2U);
	t.mode = TEST_DEEP;
	init_completion(&t.ready);
	worker = kthread_create(preempt_user, &t, "mmix-user-preempt");
	if (IS_ERR(worker)) {
		worker = NULL;
		KUNIT_FAIL(test, "preemption worker allocation");
		goto out;
	}
	get_task_struct(worker);
	sched_set_fifo(worker);
	wake_up_process(worker);
	if (!wait_for_completion_timeout(&t.ready, HZ)) {
		KUNIT_FAIL(test, "preemption worker start");
		goto out;
	}
	KUNIT_EXPECT_EQ(test, run_user(&t, mmix_user_test_deep), 0);
	KUNIT_EXPECT_EQ(test, t.stops, 1U);
	KUNIT_EXPECT_GT(test, t.writes, 0UL);
	KUNIT_EXPECT_TRUE(test, READ_ONCE(t.preempted));
	kthread_stop(worker);
	put_task_struct(worker);
	worker = NULL;
	kunit_info(test, "MMIX_USER irqs=%lu pending=%lu preempted=%u\n", t.interrupts, t.writes,
		   t.preempted);
	t.mode = TEST_KERNEL_ADDRESS;
	KUNIT_EXPECT_EQ(test, run_user(&t, mmix_user_test_kernel), 0);
	KUNIT_EXPECT_EQ(test, t.stops, 1U);
	pte = page_address(t.pte);
	t.mode = TEST_SPILL_FAULT;
	set_pte(&pte[(STACK >> PAGE_SHIFT) + 1], pfn_pte(page_to_pfn(t.stack) + 1, PAGE_KERNEL_RO));
	KUNIT_EXPECT_EQ(test, run_user(&t, mmix_user_test_deep), 0);
	KUNIT_EXPECT_EQ(test, t.faults, 1U);
	KUNIT_EXPECT_EQ(test, t.stops, 2U);
	t.mode = TEST_SAVE_FAULT;
	set_pte(&pte[(STACK >> PAGE_SHIFT) + 1], pfn_pte(page_to_pfn(t.stack) + 1, PAGE_KERNEL_RO));
	KUNIT_EXPECT_EQ(test, run_user(&t, mmix_user_test_save), 0);
	KUNIT_EXPECT_EQ(test, t.faults, 1U);
	KUNIT_EXPECT_EQ(test, t.stops, 2U);
	t.mode = TEST_UNSAVE_FAULT;
	KUNIT_EXPECT_EQ(test, run_user(&t, mmix_user_test_restore), 0);
	KUNIT_EXPECT_EQ(test, t.faults, 1U);
	KUNIT_EXPECT_EQ(test, t.stops, 3U);
	t.mode = TEST_DENIED_RETURN;
	KUNIT_EXPECT_EQ(test, run_user(&t, mmix_user_test_pending), -EFAULT);
	KUNIT_EXPECT_EQ(test, t.stops, 1U);
	KUNIT_EXPECT_GT(test, mmix_user_rstack_state(t.state)->pending.count, 0UL);
	set_pte(&pte[STACK >> PAGE_SHIFT], pfn_pte(page_to_pfn(t.stack), PAGE_KERNEL));
	t.mode = TEST_USER_TP;
	KUNIT_EXPECT_EQ(test, run_user(&t, mmix_user_test_tp), 0);
	KUNIT_EXPECT_EQ(test, t.stops, 1U);
	t.mode = TEST_ROUNDTRIP;
	KUNIT_EXPECT_EQ(test, run_user(&t, mmix_user_test_rg), -EINVAL);
	KUNIT_EXPECT_EQ(test, run_user(&t, mmix_user_test_pointer), -EINVAL);
	KUNIT_EXPECT_EQ(test, run_user(&t, mmix_user_test_unsave), -EINVAL);
	t.mode = TEST_PRIVILEGED;
	KUNIT_EXPECT_EQ(test, run_user(&t, mmix_user_test_privileged), 0);
	KUNIT_EXPECT_EQ(test, t.stops, 1U);
	t.mode = TEST_ROUNDTRIP;
	/* An invalid user context must leave the kernel able to admit a control. */
	KUNIT_EXPECT_EQ(test, run_user(&t, mmix_user_test_start), 0);
	KUNIT_EXPECT_EQ(test, t.stops, 2U);
	kunit_info(test, "MMIX_USER faults=3 denied_return=ok control=ok\n");
out:
	if (worker) {
		kthread_stop(worker);
		put_task_struct(worker);
	}
	release_test(&t);
}

static struct kunit_case user_entry_cases[] = { KUNIT_CASE(user_entry_roundtrips), {} };
static struct kunit_suite user_entry_suite = {
	.name = "mmix_user_entry",
	.test_cases = user_entry_cases,
};

kunit_test_suite(user_entry_suite);
