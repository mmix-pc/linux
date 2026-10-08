/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_KERNEL_IPI_H
#define _MMIX_KERNEL_IPI_H

#include <linux/init.h>
#include <linux/bits.h>
#include <linux/types.h>

/* Internal prepared-CPU leaf call; descriptors remain owned until ack. */
struct mmix_ipi_work {
	void (*function)(void *argument);
	void *argument;
	unsigned long generation, ack, state;
	unsigned int target;
	int result;
};

#define MMIX_FLUSH_TLB BIT(0)
#define MMIX_FLUSH_CACHE BIT(1)

#ifdef CONFIG_SMP
void __init mmix_ipi_init(void);
void mmix_ipi_cpu_start(void);
void mmix_ipi_cpu_retire(void);
void mmix_ipi_interrupt(void);
void mmix_ipi_send_stop(unsigned int cpu);
void mmix_boot_ipi_entry(void);
int mmix_ipi_work_init(struct mmix_ipi_work *work, void (*function)(void *),
		       void *argument, unsigned long generation);
int mmix_ipi_call(unsigned int cpu, struct mmix_ipi_work *work);
int mmix_ipi_queue(unsigned int cpu, struct mmix_ipi_work *work);
void mmix_ipi_kick(unsigned long targets);
bool mmix_ipi_ready(unsigned int cpu);
void mmix_ipi_flush(unsigned long operations);
void mmix_ipi_flush_poll(void);
#else
static inline void mmix_ipi_init(void) { }
static inline void mmix_ipi_interrupt(void) { }
#endif
#endif
