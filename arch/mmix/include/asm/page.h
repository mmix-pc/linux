/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_PAGE_H
#define _ASM_MMIX_PAGE_H

#include <linux/const.h>
#include <vdso/page.h>

#define PAGE_OFFSET	_UL(0x8000000000000000)
#define PHYS_OFFSET	_UL(0)
#define MMIX_TASK_SIZE	(_UL(1) << 43)

#ifndef __ASSEMBLER__
#include <linux/string.h>

typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct page *pgtable_t;

#define pte_val(x) ((x).pte)
#define pmd_val(x) ((x).pmd)
#define pgd_val(x) ((x).pgd)
#define pgprot_val(x) ((x).pgprot)
#define __pte(x) ((pte_t) { (x) })
#define __pmd(x) ((pmd_t) { (x) })
#define __pgd(x) ((pgd_t) { (x) })
#define __pgprot(x) ((pgprot_t) { (x) })

#define __pa(x)		((unsigned long)(x) & ~PAGE_OFFSET)
#define __va(x)		((void *)((unsigned long)(x) | PAGE_OFFSET))
#define __pa_symbol(x)	__pa(x)
#define virt_to_pfn(x)	(__pa(x) >> PAGE_SHIFT)
#define virt_to_page(x)	pfn_to_page(virt_to_pfn(x))
#define page_to_virt(x)	__va(page_to_pfn(x) << PAGE_SHIFT)
#define virt_addr_valid(x) ((unsigned long)(x) >= PAGE_OFFSET && pfn_valid(virt_to_pfn(x)))
#define clear_page(x)	memset((x), 0, PAGE_SIZE)
#define copy_page(to, from) memcpy((to), (from), PAGE_SIZE)
struct page;

#define clear_user_page clear_user_page
static inline void clear_user_page(void *page, unsigned long addr, struct page *pg)
{
	clear_page(page);
}

#define copy_user_page copy_user_page
static inline void copy_user_page(void *to, void *from, unsigned long addr,
				 struct page *pg)
{
	copy_page(to, from);
}

#define VMA_DATA_DEFAULT_FLAGS VMA_DATA_FLAGS_NON_EXEC
#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>
#endif
#endif
