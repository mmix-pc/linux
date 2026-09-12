/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_IRQFLAGS_H
#define _ASM_MMIX_IRQFLAGS_H

#include <linux/types.h>

#define MMIX_IRQ_CONTROLLER	(1UL << 8)
#define MMIX_KERNEL_FAULT_MASK	(0xe4UL << 32)

/* UP entry code snapshots this before publishing the handler's mask. */
extern unsigned long mmix_kernel_irq_mask;

static inline unsigned long arch_local_save_flags(void)
{
	unsigned long flags;

	asm volatile("GET %0,rK" : "=r" (flags) :: "memory");
	return flags;
}

static inline void arch_local_irq_restore(unsigned long flags)
{
	/* The shadow must be resident and directly mapped before enabling IRQs. */
	asm volatile("PUT rK,0\n\t"
		     "STOU %0,%1,0\n\t"
		     "PUT rK,%0"
		     :: "r" (flags), "r" (&mmix_kernel_irq_mask) : "memory");
}

static inline void arch_local_irq_disable(void)
{
	arch_local_irq_restore(arch_local_save_flags() & ~MMIX_IRQ_CONTROLLER);
}

static inline void arch_local_irq_enable(void)
{
	arch_local_irq_restore(arch_local_save_flags() | MMIX_IRQ_CONTROLLER);
}

static inline unsigned long arch_local_irq_save(void)
{
	unsigned long flags = arch_local_save_flags();

	arch_local_irq_restore(flags & ~MMIX_IRQ_CONTROLLER);
	return flags;
}

static inline int arch_irqs_disabled_flags(unsigned long flags)
{
	return !(flags & MMIX_IRQ_CONTROLLER);
}

static inline int arch_irqs_disabled(void)
{
	return arch_irqs_disabled_flags(arch_local_save_flags());
}
#endif
