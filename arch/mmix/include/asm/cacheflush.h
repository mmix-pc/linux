/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_CACHEFLUSH_H
#define _ASM_MMIX_CACHEFLUSH_H

struct mm_struct;
struct vm_area_struct;
struct page;

/* Do not inherit no-op cache maintenance before MMIX cache handling exists. */
void flush_cache_all(void);
#define flush_cache_all flush_cache_all
void flush_cache_mm(struct mm_struct *mm);
#define flush_cache_mm flush_cache_mm
void flush_cache_dup_mm(struct mm_struct *mm);
#define flush_cache_dup_mm flush_cache_dup_mm
void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
		       unsigned long end);
#define flush_cache_range flush_cache_range
void flush_cache_page(struct vm_area_struct *vma, unsigned long addr,
		      unsigned long pfn);
#define flush_cache_page flush_cache_page
void flush_dcache_page(struct page *page);
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
void flush_icache_range(unsigned long start, unsigned long end);
#define flush_icache_range flush_icache_range

#include <asm-generic/cacheflush.h>
#endif
