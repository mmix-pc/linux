/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_TIME_H
#define _MMIX_TIME_H

#include <linux/clockchips.h>
#include <linux/interrupt.h>

#define MMIX_TIMER_RATE 1000000000U
#define MMIX_TIMER_MIN_DELTA 1000UL
#define MMIX_TIMER_MAX_DELTA 0x7fffffffUL

struct mmix_timer {
	void __iomem *counter;
	void __iomem *context;
	struct clocksource source;
	struct clock_event_device event;
};

/* The platform comparator and QEMU deadlines use nonnegative signed time. */
static inline int mmix_timer_deadline(u64 now, unsigned long delta, u64 *deadline)
{
	if (delta < MMIX_TIMER_MIN_DELTA || delta > MMIX_TIMER_MAX_DELTA ||
	    now > S64_MAX - delta)
		return -ETIME;
	*deadline = now + delta;
	return 0;
}

void mmix_timer_setup(struct mmix_timer *timer, void __iomem *counter,
		      void __iomem *context);
irqreturn_t mmix_timer_interrupt(int irq, void *data);
unsigned long mmix_timer_irq_count(void);
void mmix_timer_drop_events(bool drop);
int mmix_timer_test_reprogram(void);
void mmix_timer_freeze_source(void);
#endif
