// SPDX-License-Identifier: GPL-2.0-only
#include <linux/init.h>
#include <linux/bitmap.h>
#include <linux/cpumask.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <asm/io.h>
#include <asm/boot.h>
#include "irq.h"
#include "ipi.h"

#define MMIX_INTC_SOURCES 8192
#define MMIX_INTC_WORDS (MMIX_INTC_SOURCES / 64)
#define MMIX_INTC_CLAIM 0x800
#define MMIX_INTC_COMPLETE 0x808

static void __iomem *intc_context[CONFIG_NR_CPUS];
static unsigned long intc_claimed[CONFIG_NR_CPUS];
static unsigned int intc_cpus;
static u8 intc_target[MMIX_INTC_SOURCES];
static DECLARE_BITMAP(intc_enabled, MMIX_INTC_SOURCES);
static struct irq_domain *intc_domain;
static DEFINE_RAW_SPINLOCK(intc_lock);
#ifdef CONFIG_MMIX_BOOT_TEST
static unsigned long timer_claims, timer_completions;

void mmix_irq_test_counts(unsigned long *claims, unsigned long *completions)
{
	unsigned long flags;

	local_irq_save(flags);
	*claims = timer_claims;
	*completions = timer_completions;
	local_irq_restore(flags);
}
#endif

static void context_enable(unsigned int cpu, irq_hw_number_t source, bool enable)
{
	void __iomem *word = intc_context[cpu] + (source / 64) * 8;
	u64 value = ioread64be(word), bit = BIT_ULL(source % 64);

	iowrite64be(enable ? value | bit : value & ~bit, word);
}

static void mmix_irq_enable(struct irq_data *data, bool enable)
{
	unsigned long flags;

	raw_spin_lock_irqsave(&intc_lock, flags);
	if (enable)
		__set_bit(data->hwirq, intc_enabled);
	else
		__clear_bit(data->hwirq, intc_enabled);
	context_enable(intc_target[data->hwirq], data->hwirq, enable);
	raw_spin_unlock_irqrestore(&intc_lock, flags);
}

static int mmix_irq_affinity(struct irq_data *data, const struct cpumask *mask, bool force)
{
	unsigned int cpu;
	unsigned long flags;
	bool timer = data->hwirq >= 16 && data->hwirq < 16 + intc_cpus;

	if (timer) {
		cpu = data->hwirq - 16;
		if (!cpumask_test_cpu(cpu, mask) || (!force && !cpu_online(cpu)))
			return -EINVAL;
	} else {
		cpu = force ? cpumask_any_and(mask, cpu_present_mask) :
			cpumask_any_and(mask, cpu_online_mask);
		if (cpu >= intc_cpus)
			return -EINVAL;
	}
	raw_spin_lock_irqsave(&intc_lock, flags);
	/* A claimed source stays owned by its original context until that CPU EOIs. */
	context_enable(intc_target[data->hwirq], data->hwirq, false);
	intc_target[data->hwirq] = cpu;
	if (test_bit(data->hwirq, intc_enabled))
		context_enable(cpu, data->hwirq, true);
	irq_data_update_effective_affinity(data, cpumask_of(cpu));
	raw_spin_unlock_irqrestore(&intc_lock, flags);
	return IRQ_SET_MASK_OK;
}

unsigned long mmix_irq_claim(void)
{
	unsigned int cpu = mmix_cpu_id();
	unsigned long source;

	if (READ_ONCE(intc_claimed[cpu]))
		return 0;
	source = ioread64be(intc_context[cpu] + MMIX_INTC_CLAIM);
	if (source < MMIX_INTC_SOURCES)
		WRITE_ONCE(intc_claimed[cpu], source);
#ifdef CONFIG_MMIX_BOOT_TEST
	if (source == 16)
		timer_claims++;
#endif
	return source;
}

int mmix_irq_complete(unsigned long source)
{
	unsigned int cpu = mmix_cpu_id();

	if (!source || READ_ONCE(intc_claimed[cpu]) != source)
		return -EINVAL;
	iowrite64be(source, intc_context[cpu] + MMIX_INTC_COMPLETE);
	WRITE_ONCE(intc_claimed[cpu], 0);
#ifdef CONFIG_MMIX_BOOT_TEST
	if (source == 16)
		timer_completions++;
#endif
	return 0;
}

static void mmix_irq_mask(struct irq_data *data)
{
	mmix_irq_enable(data, false);
}

static void mmix_irq_unmask(struct irq_data *data)
{
	mmix_irq_enable(data, true);
}

static void mmix_irq_eoi(struct irq_data *data)
{
	/* Only the claiming CPU can complete a source; affinity changes do not move it. */
	WARN_ON_ONCE(mmix_irq_complete(data->hwirq));
}

static struct irq_chip mmix_irq_chip = {
	.name = "MMIX virt",
	.irq_mask = mmix_irq_mask,
	.irq_unmask = mmix_irq_unmask,
	.irq_eoi = mmix_irq_eoi,
	.irq_set_affinity = mmix_irq_affinity,
};

static int mmix_irq_map(struct irq_domain *domain, unsigned int irq,
			irq_hw_number_t hwirq)
{
	if (!hwirq || hwirq >= MMIX_INTC_SOURCES ||
	    (hwirq >= 16 && hwirq < 1039 && hwirq >= 16 + intc_cpus))
		return -EINVAL;
	irq_set_chip_and_handler(irq, &mmix_irq_chip, handle_fasteoi_irq);
	if (hwirq >= 16 && hwirq < 16 + intc_cpus) {
		intc_target[hwirq] = hwirq - 16;
		irq_set_status_flags(irq, IRQ_NO_BALANCING);
	}
	irq_data_update_effective_affinity(irq_get_irq_data(irq),
					 cpumask_of(intc_target[hwirq]));
	irq_set_probe(irq);
	return 0;
}

static const struct irq_domain_ops mmix_irq_ops = {
	.map = mmix_irq_map,
	.xlate = irq_domain_xlate_onecell,
};

static void mmix_handle_irq(struct pt_regs *regs)
{
	unsigned int count;
	u64 source;

	for (count = 0; count < MMIX_INTC_SOURCES; count++) {
		source = mmix_irq_claim();
		if (!source)
			return;
		if (source >= MMIX_INTC_SOURCES)
			panic("MMIX: invalid interrupt claim");
		if (generic_handle_domain_irq(intc_domain, source)) {
			struct irq_data data = { .hwirq = source };

			mmix_irq_mask(&data);
			mmix_irq_eoi(&data);
			pr_err_ratelimited("MMIX: unmapped interrupt %llu\n", source);
		}
	}
}

static int __init mmix_intc_init(struct device_node *node,
			       struct device_node *parent)
{
	struct resource global, context;
	void __iomem *base;
	u32 sources, contexts, stride;
	unsigned int cpu, i;

	if (parent || intc_domain ||
	    of_property_read_u32(node, "qemu,source-count", &sources) ||
	    of_property_read_u32(node, "qemu,context-count", &contexts) ||
	    of_property_read_u32(node, "qemu,context-stride", &stride) ||
	    sources != MMIX_INTC_SOURCES ||
	    contexts != mmix_get_boot_info()->nr_cpus || stride != 0x10000 ||
	    of_address_to_resource(node, 0, &global) ||
	    of_address_to_resource(node, 1, &context) ||
	    resource_size(&global) < 16 || resource_size(&context) < contexts * stride)
		return -EINVAL;
	base = ioremap(global.start, resource_size(&global));
	if (!base || ioread64be(base) != sources ||
	    ioread64be(base + 8) != contexts)
		return -EINVAL;
	for (cpu = 0; cpu < contexts; cpu++) {
		intc_context[cpu] = ioremap(context.start + cpu * stride, stride);
		if (!intc_context[cpu])
			return -ENOMEM;
		for (i = 0; i < MMIX_INTC_WORDS; i++)
			iowrite64be(0, intc_context[cpu] + i * 8);
	}
	intc_cpus = contexts;
	intc_domain = irq_domain_create_linear(of_fwnode_handle(node), sources,
					      &mmix_irq_ops, NULL);
	if (!intc_domain)
		return -ENOMEM;
	set_handle_irq(mmix_handle_irq);
	return 0;
}
IRQCHIP_DECLARE(mmix_intc, "qemu,mmix-intc", mmix_intc_init);

void __init init_IRQ(void)
{
	irqchip_init();
	if (!intc_domain)
		panic("MMIX: interrupt controller unavailable");
	mmix_ipi_init();
}
