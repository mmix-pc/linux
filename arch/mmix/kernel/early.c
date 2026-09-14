// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cache.h>
#include <linux/libfdt.h>
#include <linux/serial_reg.h>
#include <asm/boot.h>
#include <asm/io.h>
#include <asm/sections.h>
#include "boot.h"

static struct mmix_boot_info boot_info __ro_after_init;

const struct mmix_boot_info * __init mmix_get_boot_info(void)
{
	return &boot_info;
}

/* Bounded polled output, also available before the FDT has been validated. */
void __init mmix_boot_puts(const char *text)
{
	void __iomem *uart = (void __iomem *)(boot_info.uart ?: MMIX_BOOT_UART);
	unsigned int count, length;

	for (length = 0; length < 128 && text[length]; length++) {
		for (count = 0; count < 65536; count++)
			if (readb(uart + UART_LSR) & UART_LSR_THRE)
				break;
		if (count == 65536)
			return;
		writeb(text[length], uart + UART_TX);
	}
}

void __init __noreturn mmix_boot_fail(const char *reason)
{
	asm volatile("PUT rK,0" ::: "memory");
	mmix_boot_puts("MMIX: boot failure: ");
	mmix_boot_puts(reason);
	mmix_boot_puts("\n");
	for (;;)
		asm volatile("SWYM 0,0,0" ::: "memory");
}

static const void * __init boot_property(const void *fdt, int node, const char *name,
				   int size)
{
	int length;
	const void *p = fdt_getprop(fdt, node, name, &length);

	if (!p || length != size)
		mmix_boot_fail("FDT property");
	return p;
}

static unsigned long __init cell(const void *fdt, int node, const char *name)
{
	return fdt32_ld(boot_property(fdt, node, name, sizeof(fdt32_t)));
}

static void __init boot_range(const void *fdt, int node, unsigned long *base,
			 unsigned long *size)
{
	const fdt64_t *reg = boot_property(fdt, node, "reg", 2 * sizeof(*reg));

	*base = fdt64_ld(reg);
	*size = fdt64_ld(reg + 1);
	if (!*size || *base + *size < *base)
		mmix_boot_fail("FDT range");
}

static bool __init in_ram(const struct mmix_boot_info *boot,
			 unsigned long base, unsigned long size)
{
	return size && base < boot->ram_size &&
		size <= boot->ram_size - base;
}

void __init __noreturn mmix_early_boot(void)
{
	struct mmix_boot_info boot = {
		.fdt = mmix_boot_handoff.fdt,
		.cpu = mmix_boot_handoff.cpu,
	};
	const void *fdt = __va(boot.fdt);
	const char *path;
	unsigned long base, size;
	int node, cpu, chosen, length, depth = 0;

	if (boot.cpu || !boot.fdt || (boot.fdt & 7) ||
	    boot.fdt >= (1UL << 48) - MMIX_BOOT_FDT_MAX)
		mmix_boot_fail("handoff");
	if (fdt_check_header(fdt))
		mmix_boot_fail("FDT header");
	boot.fdt_size = fdt_totalsize(fdt);
	if (boot.fdt_size > MMIX_BOOT_FDT_MAX)
		mmix_boot_fail("FDT size");
	/* Walk the complete structure before using its nodes or properties. */
	for (node = fdt_next_node(fdt, -1, &depth); node >= 0 && depth >= 0;
	     node = fdt_next_node(fdt, node, &depth))
		;
	if (node != -FDT_ERR_NOTFOUND && depth >= 0)
		mmix_boot_fail("FDT structure");
	if (fdt_node_check_compatible(fdt, 0, "qemu,mmix-virt") ||
	    fdt_address_cells(fdt, 0) != 2 || fdt_size_cells(fdt, 0) != 2)
		mmix_boot_fail("platform");
	node = fdt_path_offset(fdt, "/memory@0");
	boot_range(fdt, node, &base, &size);
	if (base || size >= (1UL << 48) || (size & (PAGE_SIZE - 1)))
		mmix_boot_fail("RAM geometry");
	boot.ram_size = size;
	if (!in_ram(&boot, boot.fdt, boot.fdt_size) ||
	    !in_ram(&boot, __pa(_stext), _end - _stext) ||
	    !in_ram(&boot, __pa_symbol(__boot_start), __boot_end - __boot_start))
		mmix_boot_fail("boot ranges");
	cpu = fdt_path_offset(fdt, "/cpus/cpu@0");
	if (cell(fdt, cpu, "reg") != boot.cpu)
		mmix_boot_fail("CPU");
	node = fdt_node_offset_by_phandle(fdt,
			cell(fdt, cpu, "qemu,initial-register-stack"));
	if (fdt_node_check_compatible(fdt, node, "qemu,mmix-register-stack") ||
	    fdt_parent_offset(fdt, node) != fdt_path_offset(fdt, "/reserved-memory") ||
	    cell(fdt, node, "qemu,cpu") != fdt_get_phandle(fdt, cpu))
		mmix_boot_fail("loader stack");
	boot_range(fdt, node, &boot.stack_base, &boot.stack_size);
	if (!in_ram(&boot, boot.stack_base, boot.stack_size) ||
	    boot.stack_size != MMIX_BOOT_STACK_SIZE ||
	    boot.stack_base != mmix_boot_handoff.loader_rs ||
	    (boot.stack_base & (PAGE_SIZE - 1)))
		mmix_boot_fail("loader stack range");
	chosen = fdt_path_offset(fdt, "/chosen");
	path = fdt_getprop(fdt, chosen, "stdout-path", &length);
	if (!path || length <= 1 || path[length - 1])
		mmix_boot_fail("stdout");
	node = fdt_path_offset_namelen(fdt, path, strcspn(path, ":"));
	if (fdt_node_check_compatible(fdt, node, "ns16550a") ||
	    cell(fdt, node, "reg-shift") || cell(fdt, node, "reg-io-width") != 1)
		mmix_boot_fail("UART");
	boot_range(fdt, node, &base, &size);
	boot.uart = (unsigned long)ioremap(base, size);
	if (!boot.uart || size < 8)
		mmix_boot_fail("UART range");
	/* Publish only a completely validated snapshot to later boot stages. */
	boot_info = boot;
	mmix_boot_puts("MMIX: bootstrap ready\n");
	mmix_boot_mmu();
}
