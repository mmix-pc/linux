// SPDX-License-Identifier: GPL-2.0-only
#include <linux/dma-map-ops.h>

void arch_setup_dma_ops(struct device *dev, bool coherent)
{
	/* QEMU virt shares RAM with devices and does not model CPU data caches. */
	dev_assign_dma_coherent(dev, coherent);
	/* Noncoherent platforms need cache and allocation support of their own. */
	if (!coherent)
		set_dma_ops(dev, &dma_dummy_ops);
}
