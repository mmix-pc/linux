// SPDX-License-Identifier: GPL-2.0-only
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/of_fdt.h>
#include <linux/of_platform.h>
#include <asm/boot.h>
#include <asm/sections.h>

void __init early_init_dt_add_memory_arch(u64 base, u64 size)
{
	if (base || size != mmix_get_boot_info()->ram_size)
		panic("MMIX: inconsistent RAM description");
	if (memblock_add(base, size))
		panic("MMIX: cannot register RAM");
}

void __init setup_arch(char **cmdline_p)
{
	const struct mmix_boot_info *boot = mmix_get_boot_info();

	if (!early_init_dt_scan(__va(boot->fdt), boot->fdt))
		panic("MMIX: invalid boot FDT");
	setup_initial_init_mm(_stext, _etext, _edata, _end);
	*cmdline_p = boot_command_line;

	/* Reserve every live boot input before permitting memblock allocation. */
	memblock_reserve((unsigned long)__boot_start, __boot_end - __boot_start);
	memblock_reserve(__pa_symbol(_stext), _end - _stext);
	memblock_reserve(boot->stack_base, boot->stack_size);
	early_init_fdt_reserve_self();
	if (IS_ENABLED(CONFIG_BLK_DEV_INITRD) && phys_initrd_size)
		memblock_reserve(phys_initrd_start, phys_initrd_size);
	early_init_fdt_scan_reserved_mem();

	min_low_pfn = PFN_UP(memblock_start_of_DRAM());
	max_low_pfn = PFN_DOWN(memblock_end_of_DRAM());
	max_pfn = max_low_pfn;
	memblock_set_current_limit(PFN_PHYS(max_low_pfn));
	parse_early_param();
	unflatten_device_tree();
	paging_init();
}

static int __init mmix_devices_init(void)
{
	return of_platform_default_populate(NULL, NULL, NULL);
}
arch_initcall(mmix_devices_init);
