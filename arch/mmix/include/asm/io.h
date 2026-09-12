/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_IO_H
#define _ASM_MMIX_IO_H

#include <linux/types.h>
#include <asm/page.h>
#include <asm/barrier.h>

/* Order MMIO against ordinary memory on both sides of each access. */
#define __io_br() mb()
#define __io_ar(v) mb()
#define __io_bw() mb()
#define __io_aw() mb()

/* Physical addresses above 2^48 are architecturally uncached device space. */
static inline void __iomem *ioremap(phys_addr_t offset, size_t size)
{
	if (!size || offset < (1UL << 48) || offset >= PAGE_OFFSET ||
	    size > PAGE_OFFSET - offset)
		return NULL;
	return (void __iomem *)(offset | PAGE_OFFSET);
}
#define ioremap ioremap

static inline void iounmap(volatile void __iomem *addr)
{
	/* Direct aliases allocate neither page tables nor virtual ranges. */
}
#define iounmap iounmap

#include <asm-generic/io.h>
#endif
