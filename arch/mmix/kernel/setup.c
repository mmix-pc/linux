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

	mmix_reserve_boot_memory();
	parse_early_param();
	unflatten_device_tree();
	paging_init();
}

static int __init mmix_devices_init(void)
{
	return of_platform_default_populate(NULL, NULL, NULL);
}
arch_initcall(mmix_devices_init);
