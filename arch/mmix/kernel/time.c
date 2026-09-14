// SPDX-License-Identifier: GPL-2.0-only
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/sched_clock.h>
#include <asm/io.h>
#include "irq.h"
#include "time.h"

#define MMIX_TIMER_COMPARE 0
#define MMIX_TIMER_CONTROL 8
#define MMIX_TIMER_STATUS 16
#define MMIX_TIMER_ENABLE BIT_ULL(0)
#define MMIX_TIMER_IRQ_ENABLE BIT_ULL(1)
#define MMIX_TIMER_PENDING BIT_ULL(0)

static struct mmix_timer boot_timer;
#ifdef CONFIG_MMIX_BOOT_TEST
static unsigned long timer_irqs;
static bool drop_events;
static bool freeze_source;
static u64 frozen_cycles;

void mmix_timer_drop_events(bool drop)
{
	WRITE_ONCE(drop_events, drop);
}

void mmix_timer_freeze_source(void)
{
	frozen_cycles = ioread64be(boot_timer.counter);
	WRITE_ONCE(freeze_source, true);
}

unsigned long mmix_timer_irq_count(void)
{
	return READ_ONCE(timer_irqs);
}
#endif

static u64 mmix_clocksource_read(struct clocksource *source)
{
	struct mmix_timer *timer = container_of(source, struct mmix_timer, source);

#ifdef CONFIG_MMIX_BOOT_TEST
	if (READ_ONCE(freeze_source))
		return frozen_cycles;
#endif
	return ioread64be(timer->counter);
}

static u64 notrace mmix_sched_clock_read(void)
{
	return ioread64be(boot_timer.counter);
}

static int mmix_timer_shutdown(struct clock_event_device *event)
{
	struct mmix_timer *timer = container_of(event, struct mmix_timer, event);

	/* Disable before clearing: a past comparator otherwise relatches pending. */
	iowrite64be(0, timer->context + MMIX_TIMER_CONTROL);
	iowrite64be(MMIX_TIMER_PENDING, timer->context + MMIX_TIMER_STATUS);
	return 0;
}

static int mmix_timer_next_event(unsigned long delta, struct clock_event_device *event)
{
	struct mmix_timer *timer = container_of(event, struct mmix_timer, event);
	u64 deadline;
	int err;

	mmix_timer_shutdown(event);
	err = mmix_timer_deadline(ioread64be(timer->counter), delta, &deadline);
	if (err)
		return err;
	iowrite64be(deadline, timer->context + MMIX_TIMER_COMPARE);
	iowrite64be(MMIX_TIMER_ENABLE | MMIX_TIMER_IRQ_ENABLE,
		    timer->context + MMIX_TIMER_CONTROL);
	/* Let clockevents retry an event whose programming window was missed. */
	if (ioread64be(timer->counter) >= deadline) {
		mmix_timer_shutdown(event);
		return -ETIME;
	}
	return 0;
}

irqreturn_t mmix_timer_interrupt(int irq, void *data)
{
	struct mmix_timer *timer = data;

	if (!(ioread64be(timer->context + MMIX_TIMER_STATUS) & MMIX_TIMER_PENDING))
		return IRQ_NONE;
	mmix_timer_shutdown(&timer->event);
	/* Generic IRQ EOI follows this callback, after device quiescence. */
#ifdef CONFIG_MMIX_BOOT_TEST
	timer_irqs++;
	if (READ_ONCE(drop_events))
		return IRQ_HANDLED;
#endif
	timer->event.event_handler(&timer->event);
	return IRQ_HANDLED;
}

void mmix_timer_setup(struct mmix_timer *timer, void __iomem *counter,
		      void __iomem *context)
{
	timer->counter = counter;
	timer->context = context;
	timer->source = (struct clocksource) {
		.name = "mmix-virt",
		.rating = 300,
		.read = mmix_clocksource_read,
		.mask = CLOCKSOURCE_MASK(64),
		.flags = CLOCK_SOURCE_IS_CONTINUOUS,
	};
	timer->event = (struct clock_event_device) {
		.name = "mmix-virt",
		.rating = 300,
		.features = CLOCK_EVT_FEAT_ONESHOT,
		.set_next_event = mmix_timer_next_event,
		.set_state_shutdown = mmix_timer_shutdown,
		.set_state_oneshot = mmix_timer_shutdown,
		.set_state_oneshot_stopped = mmix_timer_shutdown,
		.tick_resume = mmix_timer_shutdown,
		.cpumask = cpumask_of(0),
	};
	mmix_timer_shutdown(&timer->event);
}

#ifdef CONFIG_MMIX_BOOT_TEST
int mmix_timer_test_mask(struct mmix_timer_mask_result *result)
{
	struct irq_data *data = irq_get_irq_data(boot_timer.event.irq);
	struct irq_chip *chip;
	unsigned long flags, before, claims, completions;
	u64 start;
	unsigned int i;
	int err;

	if (!data || data->hwirq != 16)
		return -EINVAL;
	chip = irq_data_get_irq_chip(data);
	preempt_disable();
	local_irq_save(flags);
	mmix_irq_test_counts(&claims, &completions);
	before = timer_irqs;
	chip->irq_mask(data);
	err = mmix_timer_next_event(5000000, &boot_timer.event);
	local_irq_restore(flags);
	start = ioread64be(boot_timer.counter);
	for (i = 0; i < 2000000 &&
	     ioread64be(boot_timer.counter) - start < 10000000; i++)
		cpu_relax();
	local_irq_save(flags);
	result->pending = ioread64be(boot_timer.context + MMIX_TIMER_STATUS);
	result->masked_irqs = timer_irqs - before;
	chip->irq_unmask(data);
	local_irq_restore(flags);
	start = ioread64be(boot_timer.counter);
	for (i = 0; i < 2000000 && mmix_timer_irq_count() == before &&
	     ioread64be(boot_timer.counter) - start < 100000000; i++)
		cpu_relax();
	local_irq_save(flags);
	result->delivered_irqs = timer_irqs - before;
	mmix_irq_test_counts(&result->claims, &result->completions);
	result->claims -= claims;
	result->completions -= completions;
	local_irq_restore(flags);
	/* Leave the generic tick armed even if the diagnostic attempt failed. */
	if (mmix_timer_test_reprogram())
		err = -EINVAL;
	preempt_enable();
	return err;
}

int mmix_timer_test_reprogram(void)
{
	struct clock_event_device *event = &boot_timer.event;
	ktime_t saved;
	unsigned long flags;
	unsigned int delay;
	int err = 0;

	local_irq_save(flags);
	saved = event->next_event;
	event->set_state_shutdown(event);
	event->next_event_forced = 0;
	if (ioread64be(boot_timer.context + MMIX_TIMER_CONTROL) ||
	    ioread64be(boot_timer.context + MMIX_TIMER_STATUS))
		err = -EINVAL;
	event->set_state_oneshot(event);
	if (event->set_next_event(0, event) != -ETIME)
		err = -EINVAL;
	/* Exercise the public core reprogramming API's expired-deadline result. */
	event->next_event = ktime_sub_ns(ktime_get(), 1);
	if (clockevents_update_freq(event, MMIX_TIMER_RATE) != -ETIME)
		err = -EINVAL;
	event->set_state_shutdown(event);
	event->next_event_forced = 0;
	if (ioread64be(boot_timer.context + MMIX_TIMER_CONTROL) ||
	    ioread64be(boot_timer.context + MMIX_TIMER_STATUS))
		err = -EINVAL;
	event->set_state_oneshot(event);
	event->next_event = saved;
	/* Restore a real event even if the saved deadline elapsed during testing. */
	for (delay = 1; clockevents_update_freq(event, MMIX_TIMER_RATE); delay *= 2) {
		if (delay > 16)
			panic("MMIX: boot test could not restore timer");
		event->next_event = ktime_add_ms(ktime_get(), delay);
	}
	local_irq_restore(flags);
	return err;
}
#endif

void __init time_init(void)
{
	struct device_node *node;
	struct resource counter, context;
	void __iomem *counter_base, *context_base;
	u32 rate, count, stride;
	int irq, err;

	node = of_find_compatible_node(NULL, NULL, "qemu,mmix-timer");
	if (!node || !of_device_is_available(node) ||
	    of_property_read_u32(node, "clock-frequency", &rate) ||
	    rate != MMIX_TIMER_RATE ||
	    of_property_read_u32(node, "qemu,context-count", &count) || count != 1 ||
	    of_property_read_u32(node, "qemu,context-stride", &stride) || stride != 0x10000 ||
	    of_address_to_resource(node, 0, &counter) || resource_size(&counter) < 8 ||
	    of_address_to_resource(node, 1, &context) || resource_size(&context) < 24 ||
	    of_irq_count(node) != 1 || ((counter.start | context.start) & 7))
		panic("MMIX: invalid timer description");
	counter_base = ioremap(counter.start, resource_size(&counter));
	context_base = ioremap(context.start, resource_size(&context));
	irq = irq_of_parse_and_map(node, 0);
	of_node_put(node);
	if (!counter_base || !context_base || !irq)
		panic("MMIX: timer resources unavailable");
	mmix_timer_setup(&boot_timer, counter_base, context_base);
	boot_timer.event.irq = irq;
	err = request_irq(irq, mmix_timer_interrupt, IRQF_TIMER, "mmix-virt", &boot_timer);
	if (err)
		panic("MMIX: timer IRQ registration failed: %d", err);
	err = clocksource_register_hz(&boot_timer.source, rate);
	if (err)
		panic("MMIX: clocksource registration failed: %d", err);
	sched_clock_register(mmix_sched_clock_read, 64, rate);
	clockevents_config_and_register(&boot_timer.event, rate,
					MMIX_TIMER_MIN_DELTA, MMIX_TIMER_MAX_DELTA);
}
