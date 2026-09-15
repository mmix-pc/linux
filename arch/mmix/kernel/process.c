// SPDX-License-Identifier: GPL-2.0-only
#include <linux/errno.h>
#include <linux/cpu.h>
#include <linux/workqueue.h>
#include <linux/mm.h>
#include <linux/reboot.h>
#include <linux/sched/task.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <asm/current.h>
#include <asm/irqflags.h>
#include "process.h"
#include "user_entry.h"

struct mmix_rstack {
	struct work_struct work;
	struct vm_struct *area;
	struct page *pages[MMIX_RSTACK_SIZE / PAGE_SIZE];
};

#if defined(CONFIG_MMIX_BOOT_TEST) || defined(CONFIG_MMIX_USER_TEST)
static atomic_long_t rstack_allocations = ATOMIC_LONG_INIT(0);
static atomic_long_t rstack_releases = ATOMIC_LONG_INIT(0);

unsigned long mmix_rstack_allocated(void)
{
	return atomic_long_read(&rstack_allocations);
}

unsigned long mmix_rstack_released(void)
{
	return atomic_long_read(&rstack_releases);
}
#endif

#ifdef CONFIG_MMIX_BOOT_TEST
static atomic_t rstack_fail_step = ATOMIC_INIT(-1);

void mmix_rstack_fail_after(int steps)
{
	atomic_set(&rstack_fail_step, steps);
}

static bool rstack_fail(void)
{
	return atomic_read(&rstack_fail_step) >= 0 &&
		atomic_dec_return(&rstack_fail_step) < 0;
}
#else
static inline bool rstack_fail(void)
{
	return false;
}
#endif

static void free_rstack(struct mmix_rstack *stack)
{
	unsigned int i;

	if (!stack)
		return;
	if (stack->area) {
		unsigned long base = (unsigned long)stack->area->addr + PAGE_SIZE;

		vm_area_unmap_pages(stack->area, base, base + MMIX_RSTACK_SIZE);
		free_vm_area(stack->area);
	}
	for (i = 0; i < ARRAY_SIZE(stack->pages); i++)
		if (stack->pages[i])
			__free_page(stack->pages[i]);
#if defined(CONFIG_MMIX_BOOT_TEST) || defined(CONFIG_MMIX_USER_TEST)
	atomic_long_inc(&rstack_releases);
#endif
	kfree(stack);
}

static void release_rstack_work(struct work_struct *work)
{
	free_rstack(container_of(work, struct mmix_rstack, work));
}

int arch_dup_task_struct(struct task_struct *dst, struct task_struct *src)
{
	*dst = *src;
	/* Allocation belongs to copy_thread, after generic duplication succeeds. */
	memset(&dst->thread, 0, sizeof(dst->thread));
	return 0;
}

void arch_release_task_struct(struct task_struct *task)
{
	/* Final task release, including failed forks; never the running task. */
	mmix_user_rstack_free(&task->thread.user_state);
	if (task->thread.rstack)
		schedule_work(&task->thread.rstack->work);
}

void release_thread(struct task_struct *task)
{
	/* The saved context remains owned until arch_release_task_struct. */
}

int mmix_prepare_thread(struct task_struct *task, int (*function)(void *), void *argument)
{
	unsigned long base = task->thread.rstack_base;
	unsigned long limit = task->thread.rstack_limit;
	unsigned long sp = (unsigned long)task->stack;
	struct mmix_switch_seed *seed = (void *)base;

	if (!function || (long)function >= 0 || ((unsigned long)function & 3) ||
	    sp < VMALLOC_START || sp > VMALLOC_END - THREAD_SIZE ||
	    (sp & (PAGE_SIZE - 1)) || base < VMALLOC_START ||
	    (base & (PAGE_SIZE - 1)) || limit <= base || limit > VMALLOC_END ||
	    limit - base != MMIX_RSTACK_SIZE ||
	    (sp < limit && sp + THREAD_SIZE > base))
		return -EINVAL;
	memset(seed, 0, sizeof(*seed));
	seed->globals[230 - MMIX_BOOT_RG] = (unsigned long)task;
	memset(task_pt_regs(task), 0, sizeof(struct pt_regs));
	task_pt_regs(task)->pc = (unsigned long)function;
	task_pt_regs(task)->syscall_nr = -1;
	seed->globals[254 - MMIX_BOOT_RG] = (unsigned long)task_pt_regs(task);
	seed->ga = (unsigned long)MMIX_BOOT_RG << 56;
	task->thread.save = (unsigned long)&seed->ga;
	task->thread.function = (unsigned long)function;
	task->thread.argument = (unsigned long)argument;
	task->thread.started = 0;
	return 0;
}

static int user_child(void *unused)
{
	int error = mmix_user_enter(current->thread.user_state, NULL, NULL);

	/* A native user task can only return here if initial admission failed. */
	do_group_exit(error ? SIGSEGV : 0);
}

int copy_thread(struct task_struct *task, const struct kernel_clone_args *args)
{
	struct mmix_rstack *stack;
	unsigned long base;
	unsigned int i;
	int err = -ENOMEM;

	/* Only independent-mm, original-stack native children are qualified. */
	if (!args->fn) {
		if (args->flags || args->stack || args->stack_size ||
		    args->exit_signal != SIGCHLD || !current->thread.user_state)
			return -EOPNOTSUPP;
	}
	if (rstack_fail())
		return -ENOMEM;
	stack = kzalloc_obj(*stack);
	if (!stack)
		return -ENOMEM;
#if defined(CONFIG_MMIX_BOOT_TEST) || defined(CONFIG_MMIX_USER_TEST)
	atomic_long_inc(&rstack_allocations);
#endif
	INIT_WORK(&stack->work, release_rstack_work);
	for (i = 0; i < ARRAY_SIZE(stack->pages); i++) {
		if (rstack_fail())
			goto fail;
		stack->pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!stack->pages[i])
			goto fail;
	}
	/* Leave the first page absent; get_vm_area adds the upper guard. */
	if (rstack_fail())
		goto fail;
	stack->area = get_vm_area(MMIX_RSTACK_SIZE + PAGE_SIZE, VM_SPARSE);
	if (!stack->area)
		goto fail;
	base = (unsigned long)stack->area->addr + PAGE_SIZE;
	for (i = 0; i < ARRAY_SIZE(stack->pages); i++) {
		unsigned long address = base + i * PAGE_SIZE;

		err = -ENOMEM;
		if (rstack_fail())
			goto fail;
		err = vm_area_map_pages(stack->area, address, address + PAGE_SIZE,
					&stack->pages[i]);
		if (err)
			goto fail;
	}
	err = -ENOMEM;
	if (rstack_fail())
		goto fail;
	task->thread.rstack_base = base;
	task->thread.rstack_limit = base + MMIX_RSTACK_SIZE;
	err = mmix_prepare_thread(task, args->fn ?: user_child, args->fn_arg);
	if (err)
		goto fail;
	if (!args->fn) {
		struct mmix_user_state *state;

		task->thread.user_state = mmix_user_rstack_dup(current->thread.user_state);
		if (!task->thread.user_state) {
			err = -ENOMEM;
			goto fail;
		}
		state = mmix_user_rstack_state(task->thread.user_state);
		state->regs.regs[231] = 0;
	}
	task->thread.rstack = stack;
	return 0;
fail:
	free_rstack(stack);
	memset(&task->thread, 0, sizeof(task->thread));
	return err;
}

void __noreturn mmix_ret_from_fork(struct task_struct *prev)
{
	int (*function)(void *) = (void *)current->thread.function;
	void *argument = (void *)current->thread.argument;
	int result;

	schedule_tail(prev);
	local_irq_enable();
	result = function(argument);
	if (!result && current->thread.exec_pending) {
		mmix_user_enter(current->thread.user_state, NULL, NULL);
		do_group_exit(SIGSEGV);
	}
	do_exit(result);
}

void arch_cpu_idle(void)
{
	/* FIXME: SYNC 4 can sleep after an IRQ was serviced; no atomic wake protocol yet. */
	local_irq_enable();
	cpu_relax();
	local_irq_disable();
}

void machine_halt(void)
{
	asm volatile("PUT rK,0" ::: "memory");
	for (;;)
		cpu_relax();
}

void machine_power_off(void)
{
	machine_halt();
}

void machine_restart(char *command)
{
	machine_halt();
}

void flush_thread(void)
{
	/* Crossing the exec commit point discards the previous user snapshot. */
	current->thread.exec_pending = false;
	current->thread.exec_committed = true;
	mmix_user_rstack_free(&current->thread.user_state);
	task_pt_regs(current)->syscall_nr = -1;
}
