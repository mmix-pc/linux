/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_SMP_H
#define _ASM_MMIX_SMP_H

#include <asm/cpu_local.h>

#define raw_smp_processor_id() mmix_cpu_id()

struct cpumask;
void arch_smp_send_reschedule(int cpu);
void arch_send_call_function_single_ipi(int cpu);
void arch_send_call_function_ipi_mask(const struct cpumask *mask);
void mmix_smp_stop(void);

#endif
