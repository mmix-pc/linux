/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_KERNEL_IRQ_H
#define _MMIX_KERNEL_IRQ_H

#ifdef CONFIG_MMIX_BOOT_TEST
void mmix_irq_test_counts(unsigned long *claims, unsigned long *completions);
#endif
#endif
