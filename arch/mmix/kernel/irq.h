/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_KERNEL_IRQ_H
#define _MMIX_KERNEL_IRQ_H

unsigned long mmix_irq_claim(void);
int mmix_irq_complete(unsigned long source);

#ifdef CONFIG_MMIX_BOOT_TEST
void mmix_irq_test_counts(unsigned long *claims, unsigned long *completions);
#endif
#endif
