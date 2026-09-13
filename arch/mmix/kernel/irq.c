// SPDX-License-Identifier: GPL-2.0-only
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqchip.h>
#include <linux/irqdomain.h>
#include <linux/of_address.h>
#include <asm/io.h>

#define MMIX_INTC_SOURCES 8192
#define MMIX_INTC_WORDS (MMIX_INTC_SOURCES / 64)
#define MMIX_INTC_CLAIM 0x800
#define MMIX_INTC_COMPLETE 0x808

static void __iomem *intc_context;
static struct irq_domain *intc_domain;
static DEFINE_RAW_SPINLOCK(intc_lock);

static void mmix_irq_enable(struct irq_data *data, bool enable)
{
	void __iomem *word = intc_context + (data->hwirq / 64) * 8;
	unsigned long flags;
	u64 value, bit = BIT_ULL(data->hwirq % 64);

	raw_spin_lock_irqsave(&intc_lock, flags);
	value = ioread64be(word);
	iowrite64be(enable ? value | bit : value & ~bit, word);
	raw_spin_unlock_irqrestore(&intc_lock, flags);
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
	/* Device service must quiesce the level before releasing its claim. */
	iowrite64be(data->hwirq, intc_context + MMIX_INTC_COMPLETE);
}

static struct irq_chip mmix_irq_chip = {
	.name = "MMIX virt",
	.irq_mask = mmix_irq_mask,
	.irq_unmask = mmix_irq_unmask,
	.irq_eoi = mmix_irq_eoi,
};

static int mmix_irq_map(struct irq_domain *domain, unsigned int irq,
			irq_hw_number_t hwirq)
{
	if (!hwirq || hwirq >= MMIX_INTC_SOURCES)
		return -EINVAL;
	irq_set_chip_and_handler(irq, &mmix_irq_chip, handle_fasteoi_irq);
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
		source = ioread64be(intc_context + MMIX_INTC_CLAIM);
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
	unsigned int i;

	if (parent || intc_domain ||
	    of_property_read_u32(node, "qemu,source-count", &sources) ||
	    of_property_read_u32(node, "qemu,context-count", &contexts) ||
	    of_property_read_u32(node, "qemu,context-stride", &stride) ||
	    sources != MMIX_INTC_SOURCES || contexts != 1 || stride != 0x10000 ||
	    of_address_to_resource(node, 0, &global) ||
	    of_address_to_resource(node, 1, &context) ||
	    resource_size(&global) < 16 || resource_size(&context) < 0x810)
		return -EINVAL;
	base = ioremap(global.start, resource_size(&global));
	intc_context = ioremap(context.start, resource_size(&context));
	if (!base || !intc_context || ioread64be(base) != sources ||
	    ioread64be(base + 8) != contexts)
		return -EINVAL;
	for (i = 0; i < MMIX_INTC_WORDS; i++)
		iowrite64be(0, intc_context + i * 8);
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
}
