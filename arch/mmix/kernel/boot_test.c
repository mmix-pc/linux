// SPDX-License-Identifier: GPL-2.0-only
/* Executed by KUnit before init memory is freed or userspace is entered. */
#include <kunit/test.h>
#include <linux/completion.h>
#include <linux/crc32.h>
#include <linux/delay.h>
#include <linux/hrtimer.h>
#include <linux/kthread.h>
#include <linux/libfdt.h>
#include <linux/mm.h>
#include <linux/mmzone.h>
#include <linux/moduleparam.h>
#include <linux/sched/rt.h>
#include <linux/vmalloc.h>
#include <asm/boot.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include <asm/uaccess.h>
#include "boot_test.h"
#include "entry.h"
#include "process.h"
#include "time.h"

static_assert(offsetof(struct mmix_boot_register_state, progress) == MMIX_BOOT_TEST_PROGRESS);
static_assert(offsetof(struct mmix_boot_register_state, stop) == MMIX_BOOT_TEST_STOP);
static_assert(offsetof(struct mmix_boot_register_state, error) == MMIX_BOOT_TEST_ERROR);
static struct mmix_boot_info saved_boot;
static u32 text_crc, fdt_crc, bootstrap_crc;

static char *fault;
module_param(fault, charp, 0400);

static void __ref boot_inputs(struct kunit *test)
{
	const struct mmix_boot_info *boot = mmix_get_boot_info();
	const void *fdt = __va(boot->fdt);
	unsigned long free = global_zone_page_state(NR_FREE_PAGES) << PAGE_SHIFT;
	int node;

	KUNIT_ASSERT_GE(test, free, 8UL << 20);
	KUNIT_ASSERT_EQ(test, fdt_check_header(fdt), 0);
	for (node = fdt_next_node(fdt, -1, NULL); node >= 0;
	     node = fdt_next_node(fdt, node, NULL))
		KUNIT_EXPECT_PTR_EQ(test, strstr(fdt_get_name(fdt, node, NULL),
						 "framebuffer"), NULL);
	KUNIT_EXPECT_GE(test, fdt_node_offset_by_compatible(fdt, -1, "qemu,mmix-intc"), 0);
	KUNIT_EXPECT_GE(test, fdt_node_offset_by_compatible(fdt, -1, "qemu,mmix-timer"), 0);
	saved_boot = *boot;
	text_crc = crc32_le(0, _stext, _etext - _stext);
	fdt_crc = crc32_le(0, fdt, boot->fdt_size);
	bootstrap_crc = crc32_le(0, __boot_start, __boot_end - __boot_start);
	kunit_info(test, "MMIX_CHECK boot ram=%lu free=%lu reserved=%lu image=%lx..%lx fdt=%lx+%lu loader_stack=%lx+%lu\n",
		   boot->ram_size, free, boot->ram_size - (totalram_pages() << PAGE_SHIFT),
		   __pa_symbol(_stext), __pa_symbol(_end), boot->fdt, boot->fdt_size,
		   boot->stack_base, boot->stack_size);
	KUNIT_EXPECT_EQ(test, boot->cpu, 0UL);
	KUNIT_EXPECT_EQ(test, boot->ram_size, 256UL << 20);
	KUNIT_EXPECT_GT(test, boot->fdt_size, 0UL);
	KUNIT_EXPECT_EQ(test, boot->uart, (unsigned long)MMIX_BOOT_UART);
	KUNIT_EXPECT_TRUE(test, IS_ENABLED(CONFIG_VMAP_STACK));
	KUNIT_EXPECT_TRUE(test, IS_ENABLED(CONFIG_PREEMPTION));
	KUNIT_EXPECT_TRUE(test, irqs_disabled() == false);
}

static bool overlaps(unsigned long a, unsigned long size, unsigned long b,
		     unsigned long length)
{
	return a < b + length && b < a + size;
}

static void __ref memory(struct kunit *test)
{
	const struct mmix_boot_info *boot = mmix_get_boot_info();
	struct page *pages[32];
	unsigned long first = 0, min_pa = ULONG_MAX, max_pa = 0;
	bool reused = false;
	unsigned int round, i, j, allocated, checked = 0;

	for (round = 0; round < 64; round++) {
		allocated = 0;
		for (i = 0; i < ARRAY_SIZE(pages); i++) {
			unsigned long pa;

			pages[i] = alloc_page(GFP_KERNEL);
			if (!pages[i])
				break;
			allocated++;
			pa = page_to_phys(pages[i]);
			min_pa = min(min_pa, pa);
			max_pa = max(max_pa, pa);
			checked++;
			KUNIT_EXPECT_FALSE(test, PageReserved(pages[i]));
			for (j = 0; j < i; j++)
				KUNIT_EXPECT_NE(test, pa, page_to_phys(pages[j]));
			KUNIT_EXPECT_FALSE(test, overlaps(pa, PAGE_SIZE,
							  __pa_symbol(__boot_start),
							  __boot_end - __boot_start));
			if (!round && !i)
				first = pa;
			else if (round && pa == first)
				reused = true;
			KUNIT_EXPECT_FALSE(test, overlaps(pa, PAGE_SIZE,
							  __pa_symbol(_stext),
							  _end - _stext));
			KUNIT_EXPECT_FALSE(test,
					   overlaps(pa, PAGE_SIZE, boot->fdt,
						    boot->fdt_size));
			KUNIT_EXPECT_FALSE(test, overlaps(pa, PAGE_SIZE,
							  boot->stack_base,
							  boot->stack_size));
			memset(page_address(pages[i]), round + 1, PAGE_SIZE);
			KUNIT_EXPECT_PTR_EQ(test,
					    memchr_inv(page_address(pages[i]),
						       round + 1, PAGE_SIZE),
					    NULL);
		}
		for (i = 0; i < allocated; i++)
			__free_page(pages[i]);
		KUNIT_ASSERT_EQ(test, allocated,
				(unsigned int)ARRAY_SIZE(pages));
	}
	KUNIT_EXPECT_TRUE(test, reused);
	KUNIT_EXPECT_EQ(test, crc32_le(0, _stext, _etext - _stext), text_crc);
	KUNIT_EXPECT_EQ(test, crc32_le(0, __va(boot->fdt), boot->fdt_size), fdt_crc);
	KUNIT_EXPECT_EQ(test, crc32_le(0, __boot_start,
				       __boot_end - __boot_start), bootstrap_crc);
	KUNIT_EXPECT_EQ(test, memcmp(boot, &saved_boot, sizeof(saved_boot)), 0);
	kunit_info(test, "MMIX_CHECK memory pages=%u range=%lx..%lx reused=%u text_crc=%x fdt_crc=%x bootstrap_crc=%x\n",
		   checked, min_pa, max_pa + PAGE_SIZE, reused, text_crc, fdt_crc, bootstrap_crc);
}

static pte_t *mapping_pte(void *address)
{
	unsigned long va = (unsigned long)address;
	pgd_t *pgd = pgd_offset_k(va);
	p4d_t *p4d = p4d_offset(pgd, va);
	pud_t *pud = pud_offset(p4d, va);
	pmd_t *pmd = pmd_offset(pud, va);

	return pte_offset_kernel(pmd, va);
}

static void mappings(struct kunit *test)
{
	struct page *a = alloc_page(GFP_KERNEL);
	struct page *b = alloc_page(GFP_KERNEL);
	unsigned long *va;
	pte_t *pte;

	if (!a || !b) {
		if (a)
			__free_page(a);
		if (b)
			__free_page(b);
		KUNIT_FAIL(test, "mapping page allocation");
		return;
	}
	*(unsigned long *)page_address(a) = 0x1234;
	*(unsigned long *)page_address(b) = 0x5678;
	va = vmap(&a, 1, VM_MAP, PAGE_KERNEL);
	if (!va) {
		KUNIT_FAIL(test, "vmap allocation");
		goto free;
	}
	KUNIT_EXPECT_EQ(test, READ_ONCE(*va), 0x1234UL);
	pte = mapping_pte(va);
	set_pte(pte, pfn_pte(page_to_pfn(b), PAGE_KERNEL));
	flush_tlb_kernel_range((unsigned long)va,
			       (unsigned long)va + PAGE_SIZE);
	KUNIT_EXPECT_EQ(test, READ_ONCE(*va), 0x5678UL);
	KUNIT_EXPECT_PTR_EQ(test, vmalloc_to_page(va), b);
	vunmap(va);
	vm_unmap_aliases();
	{
		struct vm_struct *area;
		unsigned long start = (unsigned long)va;

		/* Reserve the released range; vmap does not promise immediate reuse. */
		area = __get_vm_area_caller(PAGE_SIZE, VM_SPARSE, start,
					    start + 2 * PAGE_SIZE,
					    __builtin_return_address(0));
		if (area) {
			struct mmix_boot_register_state state = {};
			struct mmix_fault_sample before, after;
			int ret;

			mmix_boot_fault_sample(&before);
			KUNIT_EXPECT_EQ(test, mmix_boot_register_test(&state, va), 1UL);
			mmix_boot_fault_sample(&after);
			KUNIT_EXPECT_EQ(test, state.error, 0UL);
			KUNIT_EXPECT_EQ(test, after.count, before.count + 1);
			KUNIT_EXPECT_EQ(test, after.address, start);
			KUNIT_EXPECT_EQ(test, after.pc, (unsigned long)mmix_boot_test_store);
			KUNIT_EXPECT_EQ(test, after.cause >> 32, 0x03000000UL);
			kunit_info(test, "MMIX_CHECK mappings va=%lx a=%llx b=%llx fault_pc=%lx cause=%lx\n",
				   start, page_to_phys(a), page_to_phys(b), after.pc, after.cause);
			ret = vm_area_map_pages(area, start, start + PAGE_SIZE, &a);

			KUNIT_EXPECT_EQ(test, ret, 0);
			if (!ret)
				KUNIT_EXPECT_EQ(test, READ_ONCE(*va), 0x1234UL);
			free_vm_area(area);
		} else {
			KUNIT_FAIL(test, "mapping reuse");
		}
	}
free:
	__free_page(a);
	__free_page(b);
}

static void exceptions(struct kunit *test)
{
	unsigned long *va = vmalloc(PAGE_SIZE);
	unsigned long value = 0x5678, left;
	struct mmix_boot_register_state state = {};
	struct mmix_fault_sample before, after;
	pte_t *pte;

	KUNIT_ASSERT_NOT_NULL(test, va);
	if (fault && !strcmp(fault, "fatal"))
		WRITE_ONCE(*(unsigned long *)((char *)va + PAGE_SIZE), 1);
	*va = 0x1234;
	pte = mapping_pte(va);
	set_pte(pte, pte_modify(*pte, PAGE_KERNEL_RO));
	flush_tlb_kernel_range((unsigned long)va,
			       (unsigned long)va + PAGE_SIZE);
	mmix_boot_fault_sample(&before);
	KUNIT_EXPECT_EQ(test, mmix_boot_register_test(&state, va), 1UL);
	mmix_boot_fault_sample(&after);
	KUNIT_EXPECT_EQ(test, state.error, 0UL);
	KUNIT_EXPECT_EQ(test, after.count, before.count + 1);
	KUNIT_EXPECT_EQ(test, after.address, (unsigned long)va);
	KUNIT_EXPECT_EQ(test, after.pc, (unsigned long)mmix_boot_test_store);
	KUNIT_EXPECT_NE(test, after.cause & MMIX_KERNEL_FAULT_MASK, 0UL);
	kunit_info(test, "MMIX_CHECK exceptions va=%lx pc=%lx cause=%lx patterns=%lu\n",
		   after.address, after.pc, after.cause, state.error);
	/* Raw access deliberately bypasses the user-address envelope in this test. */
	left = raw_copy_to_user((void __user *)va, &value, sizeof(value));
	KUNIT_EXPECT_EQ(test, left, sizeof(value));
	KUNIT_EXPECT_EQ(test, READ_ONCE(*va), 0x1234UL);
	set_pte(pte, pte_modify(*pte, PAGE_KERNEL));
	flush_tlb_kernel_range((unsigned long)va,
			       (unsigned long)va + PAGE_SIZE);
	vfree(va);
}

static void irq_mask(struct kunit *test)
{
	unsigned long flags, nested;
	bool disabled, still_disabled, enabled;
	struct mmix_timer_mask_result result = {};

	local_irq_save(flags);
	disabled = irqs_disabled();
	local_irq_save(nested);
	local_irq_restore(nested);
	still_disabled = irqs_disabled();
	local_irq_restore(flags);
	enabled = !irqs_disabled();
	KUNIT_EXPECT_TRUE(test, disabled && still_disabled && enabled);
	KUNIT_EXPECT_EQ(test, mmix_timer_test_mask(&result), 0);
	KUNIT_EXPECT_EQ(test, result.pending, 1UL);
	KUNIT_EXPECT_EQ(test, result.masked_irqs, 0UL);
	KUNIT_EXPECT_GE(test, result.delivered_irqs, 1UL);
	KUNIT_EXPECT_LE(test, result.delivered_irqs, 100UL);
	KUNIT_EXPECT_EQ(test, result.claims, result.completions);
	KUNIT_EXPECT_GE(test, result.claims, 1UL);
	kunit_info(test, "MMIX_CHECK irq pending=%lu masked=%lu delivered=%lu claims=%lu completions=%lu\n",
		   result.pending, result.masked_irqs, result.delivered_irqs,
		   result.claims, result.completions);
}

struct switch_test {
	struct completion turn[2], done[2];
	atomic_t error;
	unsigned long iterations[2], depth[2];
};

struct switch_worker {
	struct switch_test *state;
	unsigned int id;
};

static noinline unsigned long deep_calls(unsigned long depth, unsigned long *extent)
{
	volatile unsigned long values[4] = { depth, ~depth, depth * 7,
					     depth + 3 };
	unsigned long value;

	if (depth) {
		value = deep_calls(depth - 1, extent);
	} else {
		unsigned long ro;

		asm volatile("GET %0,rO" : "=r"(ro));
		*extent = ro - current->thread.rstack_base;
		if (*extent <= 1024 * sizeof(unsigned long))
			return ULONG_MAX;
		schedule_timeout_uninterruptible(1);
		if (fault && !strcmp(fault, "context"))
			values[0] ^= 1;
		value = 0;
	}

	if (values[0] != depth || values[1] != ~depth ||
	    values[2] != depth * 7 || values[3] != depth + 3)
		return ULONG_MAX;
	return value + values[0];
}

static int switching(void *arg)
{
	struct switch_worker *worker = arg;
	struct switch_test *state = worker->state;
	unsigned int id = worker->id, i;

	if (deep_calls(80, &state->depth[id]) != 3240)
		atomic_set(&state->error, 1);
	for (i = 0; i < 1000; i++) {
		if (kthread_should_stop() ||
		    !wait_for_completion_timeout(&state->turn[id], 5 * HZ)) {
			atomic_set(&state->error, 1);
			break;
		}
		state->iterations[id]++;
		complete(&state->turn[!id]);
	}
	complete(&state->done[id]);
	return 0;
}

static int never_started(void *arg)
{
	return 0;
}

static void thread_failures(struct kunit *test)
{
	unsigned int step;

	/* Descriptor, eight pages, VA, eight mappings, and seed preparation. */
	for (step = 0; step < 19; step++) {
		unsigned long allocated = mmix_rstack_allocated();
		unsigned long released = mmix_rstack_released();
		struct task_struct *task;

		mmix_rstack_fail_after(step);
		task = kthread_create(never_started, NULL, "mmix-fail/%u",
				      step);
		mmix_rstack_fail_after(-1);
		if (!IS_ERR(task)) {
			kthread_stop(task);
			KUNIT_FAIL(test,
				   "injected acquisition failure was missed");
			return;
		}
		KUNIT_EXPECT_EQ(test, PTR_ERR(task), -ENOMEM);
		KUNIT_EXPECT_EQ(test, mmix_rstack_allocated() - allocated,
				step ? 1UL : 0UL);
		KUNIT_EXPECT_GE(test, mmix_rstack_released() - released,
				step ? 1UL : 0UL);
	}
	{
		pid_t occupied = 1;
		struct kernel_clone_args args = {
			.flags = CLONE_VM | CLONE_UNTRACED,
			.exit_signal = SIGCHLD,
			.fn = never_started,
			.set_tid = &occupied,
			.set_tid_size = 1,
		};
		unsigned long allocated = mmix_rstack_allocated();
		unsigned long released = mmix_rstack_released();
		long pid = kernel_clone(&args);
		unsigned int i;

		KUNIT_EXPECT_EQ(test, pid, (long)-EEXIST);
		KUNIT_EXPECT_EQ(test, mmix_rstack_allocated() - allocated, 1UL);
		for (i = 0; i < 100 && mmix_rstack_released() == released; i++)
			usleep_range(10000, 11000);
		KUNIT_EXPECT_GT(test, mmix_rstack_released(), released);
	}
}

static void context_switch(struct kunit *test)
{
	struct switch_test state = {};
	struct switch_worker worker[2];
	struct task_struct *tasks[2];
	unsigned int i;
	unsigned long done[2], bases[2];

	thread_failures(test);
	atomic_set(&state.error, 0);
	for (i = 0; i < 2; i++) {
		init_completion(&state.turn[i]);
		init_completion(&state.done[i]);
		worker[i] = (struct switch_worker){ &state, i };
	}
	for (i = 0; i < 2; i++) {
		tasks[i] = kthread_create(switching, &worker[i],
					  "mmix-switch/%u", i);
		if (IS_ERR(tasks[i])) {
			if (i) {
				kthread_stop(tasks[0]);
				put_task_struct(tasks[0]);
			}
			KUNIT_FAIL(test, "kthread creation");
			return;
		}
		get_task_struct(tasks[i]);
	}
	for (i = 0; i < 2; i++) {
		bases[i] = tasks[i]->thread.rstack_base;
		wake_up_process(tasks[i]);
	}
	complete(&state.turn[0]);
	for (i = 0; i < 2; i++)
		done[i] = wait_for_completion_timeout(&state.done[i], 30 * HZ);
	for (i = 0; i < 2; i++) {
		complete_all(&state.turn[i]);
		kthread_stop(tasks[i]);
		KUNIT_EXPECT_NOT_NULL(test, vmalloc_to_page((void *)bases[i]));
		put_task_struct(tasks[i]);
	}
	for (i = 0; i < 100; i++) {
		if (!vmalloc_to_page((void *)bases[0]) &&
		    !vmalloc_to_page((void *)bases[1]))
			break;
		usleep_range(10000, 11000);
	}
	KUNIT_EXPECT_PTR_EQ(test, vmalloc_to_page((void *)bases[0]), NULL);
	KUNIT_EXPECT_PTR_EQ(test, vmalloc_to_page((void *)bases[1]), NULL);
	vm_unmap_aliases();
	KUNIT_EXPECT_NE(test, done[0], 0UL);
	KUNIT_EXPECT_NE(test, done[1], 0UL);
	KUNIT_EXPECT_EQ(test, atomic_read(&state.error), 0);
	KUNIT_EXPECT_EQ(test, state.iterations[0], 1000UL);
	KUNIT_EXPECT_EQ(test, state.iterations[1], 1000UL);
	kunit_info(test, "MMIX_CHECK context switches=%lu,%lu depth=%lu,%lu failures=19 cleanup=%u\n",
		   state.iterations[0], state.iterations[1], state.depth[0], state.depth[1], i);
}

static void __ref arm_timer_loss(void)
{
	/* Direct output remains observable after the scheduler loses its timer. */
	mmix_boot_puts("\nMMIX: timer loss armed\n");
	mmix_timer_drop_events(true);
}

static void __ref arm_source_loss(void)
{
	mmix_boot_puts("\nMMIX: clocksource loss armed\n");
	mmix_timer_freeze_source();
}

static void timer_wakeup(struct kunit *test)
{
	unsigned long before = mmix_timer_irq_count();
	unsigned int i;
	u64 min_ns = U64_MAX, max_ns = 0;

	KUNIT_EXPECT_EQ(test, mmix_timer_test_reprogram(), 0);
	if (fault && !strcmp(fault, "timer"))
		arm_timer_loss();
	if (fault && !strcmp(fault, "source"))
		arm_source_loss();
	for (i = 0; i < 20; i++) {
		ktime_t start = ktime_get();

		set_current_state(TASK_UNINTERRUPTIBLE);
		schedule_hrtimeout_range_clock(&(ktime_t){ 10000000 }, 0,
					       HRTIMER_MODE_REL,
					       CLOCK_MONOTONIC);
		min_ns = min_t(u64, min_ns, ktime_to_ns(ktime_sub(ktime_get(), start)));
		max_ns = max_t(u64, max_ns, ktime_to_ns(ktime_sub(ktime_get(), start)));
		KUNIT_EXPECT_GE(test, ktime_ms_delta(ktime_get(), start),
				(s64)10);
	}
	msleep(1100);
	KUNIT_EXPECT_GE(test, mmix_timer_irq_count() - before, 100UL);
	kunit_info(test, "MMIX_CHECK timer irqs=%lu wakeups=%u min_ns=%llu max_ns=%llu\n",
		   mmix_timer_irq_count() - before, i, min_ns, max_ns);
}

static int busy_worker(void *arg)
{
	mmix_boot_register_test(arg, NULL);
	return 0;
}

static void preemption(struct kunit *test)
{
	struct mmix_boot_register_state state = {};
	unsigned long previous;
	struct task_struct *task =
		kthread_create(busy_worker, &state, "mmix-busy");
	unsigned int i;

	KUNIT_ASSERT_FALSE(test, IS_ERR(task));
	get_task_struct(task);
	sched_set_fifo_low(task);
	sched_set_fifo(current);
	wake_up_process(task);
	for (i = 0; i < 20; i++) {
		previous = READ_ONCE(state.progress);
		usleep_range(10000, 11000);
		KUNIT_EXPECT_GT(test, READ_ONCE(state.progress), previous);
	}
	WRITE_ONCE(state.stop, 1);
	kthread_stop(task);
	put_task_struct(task);
	sched_set_normal(current, 0);
	KUNIT_EXPECT_EQ(test, state.error, 0UL);
	kunit_info(test, "MMIX_CHECK preemption wakeups=%u progress=%lu patterns=%lu\n",
		   i, state.progress, state.error);
}

static struct kunit_case mmix_boot_cases[] = { KUNIT_CASE(boot_inputs),
					       KUNIT_CASE(memory),
					       KUNIT_CASE(mappings),
					       KUNIT_CASE(exceptions),
					       KUNIT_CASE(irq_mask),
					       KUNIT_CASE(context_switch),
					       KUNIT_CASE(timer_wakeup),
					       KUNIT_CASE(preemption),
					       {} };

static struct kunit_suite mmix_boot_suite = {
	.name = "mmix_boot",
	.test_cases = mmix_boot_cases,
};

kunit_test_suite(mmix_boot_suite);
