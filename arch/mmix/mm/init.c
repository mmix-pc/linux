// SPDX-License-Identifier: GPL-2.0-only
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/of_fdt.h>
#include <linux/vmalloc.h>
#include <asm/boot.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>
#include "mmu.h"

static struct vm_struct initial_stacks;

static void __init reserve_boot_range(phys_addr_t base, phys_addr_t size)
{
	if (!size || base + size < base || !memblock_is_region_memory(base, size))
		panic("MMIX: boot reservation outside RAM");
	if (memblock_reserve(base, size))
		panic("MMIX: cannot reserve boot memory");
}

void __init mmix_reserve_boot_memory(void)
{
	const struct mmix_boot_info *boot = mmix_get_boot_info();

	reserve_boot_range(__pa_symbol(__boot_start), __boot_end - __boot_start);
	reserve_boot_range(__pa_symbol(_stext), _end - _stext);
	reserve_boot_range(boot->stack_base, boot->stack_size);
	reserve_boot_range(boot->fdt, boot->fdt_size);
	if (IS_ENABLED(CONFIG_BLK_DEV_INITRD) && phys_initrd_size)
		reserve_boot_range(phys_initrd_start, phys_initrd_size);
	early_init_fdt_scan_reserved_mem();
}

void __init paging_init(void)
{
	/* Reserve virtual guards as well as mappings before vmalloc starts. */
	initial_stacks.addr = (void *)VMALLOC_START;
	initial_stacks.size = MMIX_INIT_RSTACK_END + PAGE_SIZE - VMALLOC_START;
	initial_stacks.flags = VM_ALLOC;
	vm_area_add_early(&initial_stacks);

	min_low_pfn = PFN_UP(memblock_start_of_DRAM());
	max_low_pfn = PFN_DOWN(memblock_end_of_DRAM());
	max_pfn = max_low_pfn;
	memblock_set_current_limit(PFN_PHYS(max_low_pfn));
	flush_tlb_all();
}

void __init arch_zone_limits_init(unsigned long *max_zone_pfns)
{
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;
}

pgd_t *pgd_alloc(struct mm_struct *mm)
{
	/* Kernel translations use swapper_pg_dir, never a user-owned root. */
	return __pgd_alloc(mm, 0);
}

pgprot_t vm_get_page_prot(unsigned long flags)
{
	unsigned long prot = _PAGE_PRESENT | _PAGE_YOUNG;

	if (flags & (VM_READ | VM_WRITE))
		prot |= _PAGE_READ;
	if (flags & VM_EXEC)
		prot |= _PAGE_EXEC;
	if ((flags & (VM_WRITE | VM_SHARED)) == (VM_WRITE | VM_SHARED))
		prot |= _PAGE_WRITE | _PAGE_WRITE_INTENT;
	return __pgprot(prot);
}
