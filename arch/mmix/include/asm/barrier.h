/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_BARRIER_H
#define _ASM_MMIX_BARRIER_H

/* MMIXware section 31: order all preceding and subsequent memory accesses. */
#define mb() ({ asm volatile("SYNC 3" ::: "memory"); })
/* Order preceding loads before subsequent loads. */
#define rmb() ({ asm volatile("SYNC 2" ::: "memory"); })
/* Order preceding stores before subsequent stores. */
#define wmb() ({ asm volatile("SYNC 1" ::: "memory"); })

#include <asm-generic/barrier.h>
#endif
