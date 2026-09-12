/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_PGALLOC_H
#define _ASM_MMIX_PGALLOC_H

#include <linux/mm.h>
#include <asm-generic/pgalloc.h>

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
				      pte_t *pte)
{
	set_pmd(pmd, __pmd(__pa(pte) | _PAGE_PRESENT));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd, pgtable_t page)
{
	pmd_populate_kernel(mm, pmd, page_address(page));
}

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	set_pud(pud, __pud(__pa(pmd) | _PAGE_PRESENT));
}

pgd_t *pgd_alloc(struct mm_struct *mm);
#define __pte_free_tlb(tlb, pte, addr) tlb_remove_ptdesc((tlb), page_ptdesc(pte))
#define __pmd_free_tlb(tlb, pmd, addr) tlb_remove_ptdesc((tlb), virt_to_ptdesc(pmd))
#endif
