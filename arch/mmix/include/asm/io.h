/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_IO_H
#define _ASM_MMIX_IO_H

#include <linux/bits.h>
#include <linux/types.h>
#include <asm/page.h>
#include <asm/barrier.h>

/* Order MMIO against ordinary memory on both sides of each access. */
#define __io_br() mb()
#define __io_ar(v) mb()
#define __io_bw() mb()
#define __io_aw() mb()

/* The uncached device space starts at physical address 2^48. */
#define MMIX_IO_BASE BIT(48)

static inline void __iomem *ioremap(phys_addr_t offset, size_t size)
{
	if (!size || offset < MMIX_IO_BASE || offset >= PAGE_OFFSET ||
	    size > PAGE_OFFSET - offset)
		return NULL;
	return (void __iomem *)(offset | PAGE_OFFSET);
}
#define ioremap ioremap

void __iomem *ioremap_wc(phys_addr_t offset, size_t size);
#define ioremap_wc ioremap_wc

static inline void iounmap(volatile void __iomem *addr)
{
	/* Direct aliases allocate neither page tables nor virtual ranges. */
}
#define iounmap iounmap

#include <asm-generic/io.h>
#endif
