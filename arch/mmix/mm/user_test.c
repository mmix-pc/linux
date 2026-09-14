// SPDX-License-Identifier: GPL-2.0-only
/* Privileged checks of user page-table ownership; no user entry is claimed. */
#include <kunit/test.h>
#include <linux/mm.h>
#include <linux/sched/mm.h>
#include <linux/vmalloc.h>
#include <asm/mmu_context.h>
#include <asm/pgalloc.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>

#define TEST_ADDRESS (4UL << 20)

struct test_space {
	struct mm_struct *mm;
	pmd_t *pmd;
	pgtable_t pte;
	struct page *page;
};

static void release_space(struct test_space *space)
{
	if (space->mm) {
		pgd_clear(space->mm->pgd);
		flush_tlb_mm(space->mm);
	}
	if (space->page)
		__free_page(space->page);
	if (space->pte)
		pte_free(space->mm, space->pte);
	if (space->pmd)
		pmd_free(space->mm, space->pmd);
	if (space->mm)
		mmput(space->mm);
	memset(space, 0, sizeof(*space));
}

static bool prepare_space(struct test_space *space, int fail)
{
	if (fail == 0)
		return false;
	space->mm = mm_alloc();
	if (!space->mm || fail == 1)
		return false;
	space->pmd = pmd_alloc_one(space->mm, TEST_ADDRESS);
	if (!space->pmd || fail == 2)
		return false;
	space->pte = pte_alloc_one(space->mm);
	if (!space->pte || fail == 3)
		return false;
	space->page = alloc_page(GFP_KERNEL | __GFP_ZERO);
	return !!space->page;
}

static void user_roots(struct kunit *test)
{
	struct test_space spaces[2] = { };
	unsigned long observed[3], flags, kernel_value;
	struct mm_struct *previous = current->active_mm;
	unsigned long *shared = NULL;
	unsigned int i, round;

	for (i = 0; i < ARRAY_SIZE(spaces); i++) {
		pte_t *pte;

		if (!prepare_space(&spaces[i], -1)) {
			KUNIT_FAIL(test, "user page-table allocation");
			goto out;
		}
		KUNIT_EXPECT_PTR_EQ(test, memchr_inv(spaces[i].mm->pgd, 0, PAGE_SIZE), NULL);
		pud_populate(spaces[i].mm, (pud_t *)spaces[i].mm->pgd, spaces[i].pmd);
		pmd_populate(spaces[i].mm, spaces[i].pmd, spaces[i].pte);
		pte = page_address(spaces[i].pte);
		set_pte(&pte[TEST_ADDRESS >> PAGE_SHIFT],
			pfn_pte(page_to_pfn(spaces[i].page), PAGE_KERNEL));
		*(unsigned long *)page_address(spaces[i].page) = 0x12340000UL + i;
	}
	/* Created after both user roots: visibility cannot rely on copying PGDs. */
	shared = vmalloc(PAGE_SIZE);
	if (!shared) {
		KUNIT_FAIL(test, "shared kernel mapping allocation");
		goto out;
	}
	*shared = 0x76543210;
	for (round = 0; round < 32; round++) {
		local_irq_save(flags);
		switch_mm(previous, spaces[0].mm, current);
		observed[0] = READ_ONCE(*(unsigned long *)TEST_ADDRESS);
		switch_mm(spaces[0].mm, spaces[1].mm, current);
		observed[1] = READ_ONCE(*(unsigned long *)TEST_ADDRESS);
		kernel_value = READ_ONCE(*shared);
		switch_mm(spaces[1].mm, spaces[0].mm, current);
		observed[2] = READ_ONCE(*(unsigned long *)TEST_ADDRESS);
		switch_mm(spaces[0].mm, previous, current);
		local_irq_restore(flags);
		KUNIT_EXPECT_EQ(test, observed[0], 0x12340000UL);
		KUNIT_EXPECT_EQ(test, observed[1], 0x12340001UL);
		KUNIT_EXPECT_EQ(test, observed[2], observed[0]);
		KUNIT_EXPECT_EQ(test, kernel_value, 0x76543210UL);
	}
out:
	vfree(shared);
	for (i = 0; i < ARRAY_SIZE(spaces); i++)
		release_space(&spaces[i]);
}

static void user_allocation_unwind(struct kunit *test)
{
	struct test_space space = { };
	unsigned int step, round;

	for (round = 0; round < 32; round++) {
		for (step = 0; step < 4; step++) {
			KUNIT_EXPECT_FALSE(test, prepare_space(&space, step));
			release_space(&space);
			KUNIT_EXPECT_PTR_EQ(test, memchr_inv(&space, 0, sizeof(space)), NULL);
		}
	}
}

static void user_bounds_and_protection(struct kunit *test)
{
	pte_t pte = pfn_pte(123, PAGE_KERNEL);

	KUNIT_EXPECT_TRUE(test, access_ok((void __user *)(TASK_SIZE - PAGE_SIZE), PAGE_SIZE));
	KUNIT_EXPECT_FALSE(test, access_ok((void __user *)(TASK_SIZE - 1), 2));
	KUNIT_EXPECT_FALSE(test, access_ok((void __user *)VMALLOC_START, 1));
	KUNIT_EXPECT_FALSE(test, access_ok((void __user *)PAGE_OFFSET, 1));
	KUNIT_EXPECT_FALSE(test, access_ok((void __user *)16, ~0UL));
	pte = pte_wrprotect(pte);
	KUNIT_EXPECT_FALSE(test, pte_write(pte));
	KUNIT_EXPECT_EQ(test, pte_val(pte) & _PAGE_WRITE, 0UL);
	pte = pte_modify(pte, PAGE_NONE);
	KUNIT_EXPECT_TRUE(test, pte_present(pte));
	KUNIT_EXPECT_EQ(test, pte_val(pte) & 7, 0UL);
	KUNIT_EXPECT_EQ(test, pte_pfn(pte), 123UL);
	pte = pte_mkwrite_novma(pte);
	KUNIT_EXPECT_TRUE(test, pte_write(pte));
}

static struct kunit_case user_mm_cases[] = {
	KUNIT_CASE(user_roots),
	KUNIT_CASE(user_allocation_unwind),
	KUNIT_CASE(user_bounds_and_protection),
	{ }
};

static struct kunit_suite user_mm_suite = {
	.name = "mmix_user_mm",
	.test_cases = user_mm_cases,
};

kunit_test_suite(user_mm_suite);
