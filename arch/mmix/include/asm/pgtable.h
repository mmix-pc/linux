/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_PGTABLE_H
#define _ASM_MMIX_PGTABLE_H

#include <asm/page.h>

#define PFN_PTE_SHIFT PAGE_SHIFT

#define PGDIR_SHIFT	33
#define PMD_SHIFT	23
#define PGDIR_SIZE	(1UL << PGDIR_SHIFT)
#define PGDIR_MASK	(~(PGDIR_SIZE - 1))
#define PMD_SIZE	(1UL << PMD_SHIFT)
#define PMD_MASK	(~(PMD_SIZE - 1))
#define PTRS_PER_PGD	1024
#define PTRS_PER_PMD	1024
#define PTRS_PER_PTE	1024

#define VMALLOC_START	0x2000000000000000UL
#define VMALLOC_END	0x2000080000000000UL

#define _PAGE_EXEC	(1UL << 0)
#define _PAGE_WRITE	(1UL << 1)
#define _PAGE_READ	(1UL << 2)
#define _PAGE_PRESENT	(1UL << 48)
#define _PAGE_YOUNG	(1UL << 49)
#define _PAGE_DIRTY	(1UL << 50)
#define _PAGE_WRITE_INTENT (1UL << 51)
#define PTE_PHYS_MASK	0x0000ffffffffe000UL

#define PAGE_NONE	__pgprot(_PAGE_PRESENT)
#define PAGE_KERNEL	__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_WRITE | \
				_PAGE_WRITE_INTENT | _PAGE_YOUNG | _PAGE_DIRTY)
#define PAGE_KERNEL_RO	__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_YOUNG)
#define PAGE_KERNEL_EXEC __pgprot(pgprot_val(PAGE_KERNEL) | _PAGE_EXEC)

#include <asm-generic/pgtable-nopud.h>

#define set_pte(ptr, val) WRITE_ONCE(*(ptr), (val))
#define set_pmd(ptr, val) WRITE_ONCE(*(ptr), (val))
#define set_pud(ptr, val) WRITE_ONCE(*(ptr), (val))

#define pte_none(pte) (!pte_val(pte))
#define pte_present(pte) (!!(pte_val(pte) & _PAGE_PRESENT))
#define pte_pfn(pte) ((pte_val(pte) & PTE_PHYS_MASK) >> PAGE_SHIFT)
#define pfn_pte(pfn, prot) __pte(((pfn) << PAGE_SHIFT) | pgprot_val(prot))
#define pte_page(pte) pfn_to_page(pte_pfn(pte))
#define pte_clear(mm, addr, ptr) set_pte(ptr, __pte(0))
#define pmd_none(pmd) (!pmd_val(pmd))
#define pmd_present(pmd) (!!(pmd_val(pmd) & _PAGE_PRESENT))
#define pmd_bad(pmd) ((pmd_val(pmd) & ~PTE_PHYS_MASK) != _PAGE_PRESENT)
#define pmd_clear(ptr) set_pmd(ptr, __pmd(0))
#define pmd_page(pmd) pfn_to_page((pmd_val(pmd) & PTE_PHYS_MASK) >> PAGE_SHIFT)
#define pmd_page_vaddr(pmd) ((unsigned long)__va(pmd_val(pmd) & PTE_PHYS_MASK))
#define pud_none(pud) (!pud_val(pud))
#define pud_present(pud) (!!(pud_val(pud) & _PAGE_PRESENT))
#define pud_bad(pud) ((pud_val(pud) & ~PTE_PHYS_MASK) != _PAGE_PRESENT)
#define pud_clear(ptr) set_pud(ptr, __pud(0))
#define pud_page(pud) pfn_to_page((pud_val(pud) & PTE_PHYS_MASK) >> PAGE_SHIFT)
#define pud_pgtable(pud) ((pmd_t *)__va(pud_val(pud) & PTE_PHYS_MASK))

#define pte_write(pte) (!!(pte_val(pte) & _PAGE_WRITE_INTENT))
#define pte_dirty(pte) (!!(pte_val(pte) & _PAGE_DIRTY))
#define pte_young(pte) (!!(pte_val(pte) & _PAGE_YOUNG))
#define pte_exec(pte) (!!(pte_val(pte) & _PAGE_EXEC))
#define pte_mkdirty(pte) __pte(pte_val(pte) | _PAGE_DIRTY)
#define pte_mkclean(pte) __pte(pte_val(pte) & ~_PAGE_DIRTY)
#define pte_mkyoung(pte) __pte(pte_val(pte) | _PAGE_YOUNG)
#define pte_mkold(pte) __pte(pte_val(pte) & ~_PAGE_YOUNG)
#define pte_wrprotect(pte) __pte(pte_val(pte) & ~(_PAGE_WRITE | _PAGE_WRITE_INTENT))
#define pte_mkwrite_novma(pte) __pte(pte_val(pte) | _PAGE_WRITE | _PAGE_WRITE_INTENT)

#define pgd_ERROR(pgd) pr_err("Bad MMIX pgd: %lx\n", pgd_val(pgd))
#define pmd_ERROR(pmd) pr_err("Bad MMIX pmd: %lx\n", pmd_val(pmd))

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];
struct vm_area_struct;
struct vm_fault;
void update_mmu_cache_range(struct vm_fault *vmf, struct vm_area_struct *vma,
			   unsigned long address, pte_t *ptep, unsigned int nr);
#endif
