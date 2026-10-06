/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_IRQFLAGS_H
#define _ASM_MMIX_IRQFLAGS_H

#include <linux/types.h>
#include <asm/cpu_local.h>

#define MMIX_IRQ_CONTROLLER	(1UL << 8)
#define MMIX_IRQ_IPI		(1UL << 9)
#ifdef CONFIG_SMP
#define MMIX_IRQ_EXTERNAL	(MMIX_IRQ_CONTROLLER | MMIX_IRQ_IPI)
#else
#define MMIX_IRQ_EXTERNAL	MMIX_IRQ_CONTROLLER
#endif
#define MMIX_KERNEL_FAULT_MASK	(0xe4UL << 32)

/* The directly mapped shadow belongs to the invoking CPU. */

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;

	asm volatile("GET %0,rK" : "=r" (flags) :: "memory");
	return flags;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	unsigned long *mask;

	/* Select the local shadow only after IRQs prevent migration. */
	asm volatile("PUT rK,0\n\t"
		     "GET %0,rT\n\tLDOU %0,%0,32\n\t"
		     "STOU %1,%0,0\n\tPUT rK,%1"
		     : "=&r" (mask) : "r" (flags) : "memory");
}

static inline void arch_local_irq_disable(void)
{
	arch_local_irq_restore(arch_local_save_flags() & ~MMIX_IRQ_EXTERNAL);
}

static inline void arch_local_irq_enable(void)
{
	arch_local_irq_restore(arch_local_save_flags() | MMIX_IRQ_EXTERNAL);
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();

	arch_local_irq_restore(flags & ~MMIX_IRQ_EXTERNAL);
	return flags;
}

static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & MMIX_IRQ_EXTERNAL);
}

static inline int arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}
#endif
