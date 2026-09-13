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

struct mmix_rstack {
	struct work_struct work;
	struct vm_struct *area;
	struct page *pages[MMIX_RSTACK_SIZE / PAGE_SIZE];
};

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
	seed->globals[254 - MMIX_BOOT_RG] = sp + THREAD_SIZE;
	seed->ga = (unsigned long)MMIX_BOOT_RG << 56;
	task->thread.save = (unsigned long)&seed->ga;
	task->thread.function = (unsigned long)function;
	task->thread.argument = (unsigned long)argument;
	task->thread.started = 0;
	return 0;
}

int copy_thread(struct task_struct *task, const struct kernel_clone_args *args)
{
	struct mmix_rstack *stack;
	unsigned long base;
	unsigned int i;
	int err = -ENOMEM;

	/* Userspace fork/exec context is outside the kernel-thread profile. */
	if (!args->fn)
		return -EOPNOTSUPP;
	stack = kzalloc_obj(*stack);
	if (!stack)
		return -ENOMEM;
	INIT_WORK(&stack->work, release_rstack_work);
	for (i = 0; i < ARRAY_SIZE(stack->pages); i++) {
		stack->pages[i] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!stack->pages[i])
			goto fail;
	}
	/* Leave the first page absent; get_vm_area adds the upper guard. */
	stack->area = get_vm_area(MMIX_RSTACK_SIZE + PAGE_SIZE, VM_SPARSE);
	if (!stack->area)
		goto fail;
	base = (unsigned long)stack->area->addr + PAGE_SIZE;
	err = vm_area_map_pages(stack->area, base, base + MMIX_RSTACK_SIZE, stack->pages);
	if (err)
		goto fail;
	task->thread.rstack_base = base;
	task->thread.rstack_limit = base + MMIX_RSTACK_SIZE;
	err = mmix_prepare_thread(task, args->fn, args->fn_arg);
	if (err)
		goto fail;
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

	schedule_tail(prev);
	local_irq_enable();
	do_exit(function(argument));
}

void arch_cpu_idle(void)
{
	/* SYNC 4 is a power-saving hint, not an unconditional wait instruction. */
	local_irq_enable();
	asm volatile("SYNC 4" ::: "memory");
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
