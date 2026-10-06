// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cpu.h>
#include <linux/init_task.h>
#include <linux/smp.h>
#include <asm/boot.h>
#include <asm/irqflags.h>
#include "cpu_local.h"
#include "smp_boot.h"

/* Bootstrap storage stays owned even if a CPU fails to acknowledge startup. */
static unsigned char secondary_stacks[MMIX_BOOT_CPU_SLOTS][MMIX_BOOT_STACK_SIZE]
	__aligned(PAGE_SIZE);
static unsigned long secondary_rstack[MMIX_BOOT_CPU_SLOTS]
	[MMIX_BOOT_RSTACK_SIZE / sizeof(unsigned long)] __aligned(PAGE_SIZE);

void __init mmix_smp_discover(void)
{
	unsigned int cpu;

	for (cpu = 0; cpu < mmix_get_boot_info()->nr_cpus; cpu++) {
		set_cpu_possible(cpu, true);
		set_cpu_present(cpu, true);
	}
}

void __noreturn mmix_secondary_park(void)
{
	struct mmix_secondary_boot *boot = &mmix_secondary_boot[mmix_cpu_id()];

	/* No current task, allocator, scheduler or IRQ service is admitted here. */
	asm volatile("GET %0,rO\n\tGET %1,rS\n\tOR %2,r254,0\n\t"
		     "GET %3,rT\n\tGET %4,rTT\n\tGET %5,rV\n\tGET %6,rC"
		     : "=r" (boot->r_o), "=r" (boot->r_s), "=r" (boot->sp),
		       "=r" (boot->r_t), "=r" (boot->r_tt), "=r" (boot->r_v),
		       "=r" (boot->r_c) : : "memory");
	/* Publish every observation before the boot CPU accepts this handshake. */
	smp_store_release(&boot->status, MMIX_SECONDARY_PARKED);
	/* Acquire the stop request before publishing the terminal acknowledgement. */
	while (!smp_load_acquire(&mmix_secondary_abort))
		cpu_relax();
	/* Only permanent bootstrap storage is accessed after this release. */
	smp_store_release(&boot->status, MMIX_SECONDARY_STOPPED);
	for (;;)
		cpu_relax();
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	const struct mmix_boot_info *info = mmix_get_boot_info();
	unsigned int cpu, attempt;

	for (cpu = 1; cpu < info->nr_cpus; cpu++) {
		struct mmix_secondary_boot *boot = &mmix_secondary_boot[cpu];
		struct mmix_cpu_state *state = &mmix_cpu_states[cpu];
		unsigned long *seed = secondary_rstack[cpu];
		unsigned int top = 1 + 256 - MMIX_BOOT_RG + 12;

		for (attempt = 0; attempt < 1000000; attempt++) {
			/* Acquire the raw handoff published by entry assembly's SYNC 3. */
			if (smp_load_acquire(&boot->status) != MMIX_SECONDARY_WAITING)
				break;
			cpu_relax();
		}
		if (READ_ONCE(mmix_secondary_abort) ||
		    READ_ONCE(boot->status) != MMIX_SECONDARY_ENTERED ||
		    boot->fdt != info->fdt || boot->loader_rs != info->loader_stack[cpu])
			goto fail;
		state->mmu.root = init_mm.pgd;
		state->mmu.ram_end = info->ram_size;
		boot->root = (unsigned long)init_mm.pgd;
		boot->ram_end = info->ram_size;
		boot->vector = (unsigned long)mmix_cpu_vectors + cpu * MMIX_CPU_VECTOR_SIZE;
		boot->r_c = __pa(state->continuation) | 6;
		seed[1 + 254 - MMIX_BOOT_RG] = (unsigned long)secondary_stacks[cpu] +
			MMIX_BOOT_STACK_SIZE;
		seed[top] = (unsigned long)MMIX_BOOT_RG << 56;
		boot->seed = (unsigned long)&seed[top];
		/* Publish stacks, roots and locator before the secondary's acquire. */
		smp_store_release(&boot->release, 1);
		for (attempt = 0; attempt < 1000000; attempt++) {
			/* Acquire the secondary's complete snapshot before validating it. */
			if (smp_load_acquire(&boot->status) != MMIX_SECONDARY_ENTERED)
				break;
			cpu_relax();
		}
		if (READ_ONCE(mmix_secondary_abort) ||
		    READ_ONCE(boot->status) != MMIX_SECONDARY_PARKED ||
		    boot->r_t != boot->vector || boot->r_v != MMIX_RV_SOFTWARE ||
		    boot->r_tt != (unsigned long)mmix_secondary_fault ||
		    boot->r_c != (__pa(state->continuation) | 6) ||
		    boot->r_o < (unsigned long)seed ||
		    boot->r_o >= (unsigned long)seed + MMIX_BOOT_RSTACK_SIZE ||
		    boot->r_s < (unsigned long)seed ||
		    boot->r_s >= (unsigned long)seed + MMIX_BOOT_RSTACK_SIZE ||
		    boot->sp < (unsigned long)secondary_stacks[cpu] ||
		    boot->sp > (unsigned long)secondary_stacks[cpu] + MMIX_BOOT_STACK_SIZE)
			goto fail;
		pr_info("MMIX: CPU %u prepared and parked (offline)\n", cpu);
	}
	return;
fail:
	mmix_smp_stop();
	panic("MMIX: secondary CPU %u startup failed", cpu);
}

int __cpu_up(unsigned int cpu, struct task_struct *idle)
{
	/* IRQ, timer and remote MMU services must precede ordinary CPU admission. */
	return -EOPNOTSUPP;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
	pr_info("MMIX: secondary scheduling disabled; %u CPU online\n", num_online_cpus());
}

void mmix_smp_stop(void)
{
	unsigned int cpu, attempt;

	/* Every barrier and parked CPU polls this permanent release record. */
	smp_store_release(&mmix_secondary_abort, 1);
	for (cpu = 1; cpu < MMIX_BOOT_CPU_SLOTS; cpu++) {
		if (!READ_ONCE(mmix_secondary_boot[cpu].claim))
			continue;
		for (attempt = 0; attempt < 1000000; attempt++) {
			/* Acquire the terminal acknowledgement before accepting the stop. */
			unsigned long status = smp_load_acquire(&mmix_secondary_boot[cpu].status);

			if (status == MMIX_SECONDARY_STOPPED || status == MMIX_SECONDARY_FAILED)
				break;
			cpu_relax();
		}
		if (attempt == 1000000)
			pr_emerg("MMIX: CPU %u stop acknowledgement missing\n", cpu);
	}
}

void smp_send_stop(void)
{
	mmix_smp_stop();
}

void arch_smp_send_reschedule(int cpu)
{
	/* No remote CPU can become online at this preparation boundary. */
	WARN_ON_ONCE(cpu_online(cpu) && cpu != smp_processor_id());
}

void arch_send_call_function_single_ipi(int cpu)
{
	WARN_ON_ONCE(cpu_online(cpu) && cpu != smp_processor_id());
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	WARN_ON_ONCE(cpumask_weight(cpu_online_mask) != 1);
}
