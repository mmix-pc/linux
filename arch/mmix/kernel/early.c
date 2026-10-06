// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cache.h>
#include <linux/libfdt.h>
#include <linux/serial_reg.h>
#include <asm/boot.h>
#include <asm/io.h>
#include <asm/sections.h>
#include "boot.h"
#include "smp_boot.h"
#include <asm/barrier.h>

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
	WRITE_ONCE(mmix_secondary_abort, 1);
	/* Publish the abort before entering the permanent failure loop. */
	mb();
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

static bool __init overlaps(unsigned long base, unsigned long size,
			    unsigned long other, unsigned long other_size)
{
	return base < other + other_size && other < base + size;
}

static void __init boot_cpus(const void *fdt, struct mmix_boot_info *boot)
{
	int cpus = fdt_path_offset(fdt, "/cpus");
	int reserved = fdt_path_offset(fdt, "/reserved-memory");
	int stack_nodes[MMIX_BOOT_CPU_SLOTS];
	int cpu, node, i, length, reservations;
	unsigned long base, size;

	if (cell(fdt, cpus, "#address-cells") != 1 ||
	    cell(fdt, cpus, "#size-cells") ||
	    cell(fdt, reserved, "#address-cells") != 2 ||
	    cell(fdt, reserved, "#size-cells") != 2 ||
	    !fdt_getprop(fdt, reserved, "ranges", &length) || length)
		mmix_boot_fail("CPU topology cells");
	fdt_for_each_subnode(cpu, fdt, cpus) {
		unsigned int id = cell(fdt, cpu, "reg");
		unsigned int phandle = fdt_get_phandle(fdt, cpu);

		if (id != boot->nr_cpus || id >= CONFIG_NR_CPUS ||
		    id >= MMIX_BOOT_CPU_SLOTS || !phandle ||
		    fdt_node_check_compatible(fdt, cpu, "qemu,mmix-cpu") ||
		    strcmp(boot_property(fdt, cpu, "device_type", 4), "cpu") ||
		    strcmp(boot_property(fdt, cpu, "status", 5), "okay") ||
		    strcmp(boot_property(fdt, cpu, "enable-method",
				 sizeof("qemu,mmix-immediate-entry")),
			   "qemu,mmix-immediate-entry"))
			mmix_boot_fail("CPU topology");
		node = fdt_node_offset_by_phandle(fdt,
				cell(fdt, cpu, "qemu,initial-register-stack"));
		if (fdt_node_check_compatible(fdt, node, "qemu,mmix-register-stack") ||
		    fdt_parent_offset(fdt, node) != reserved ||
		    fdt_getprop(fdt, node, "no-map", NULL) ||
		    fdt_getprop(fdt, node, "reusable", NULL) ||
		    cell(fdt, node, "qemu,cpu") != phandle ||
		    fdt_node_offset_by_phandle(fdt, phandle) != cpu)
			mmix_boot_fail("loader stack");
		boot_range(fdt, node, &base, &size);
		if (!in_ram(boot, base, size) || size != MMIX_BOOT_STACK_SIZE ||
		    (base & (PAGE_SIZE - 1)) ||
		    overlaps(base, size, boot->fdt, boot->fdt_size) ||
		    overlaps(base, size, __pa(_stext), _end - _stext) ||
		    overlaps(base, size, __pa_symbol(__boot_start), __boot_end - __boot_start))
			mmix_boot_fail("loader stack range");
		boot->loader_stack[id] = base;
		stack_nodes[id] = node;
		boot->nr_cpus++;
	}
	if (cpu != -FDT_ERR_NOTFOUND || !boot->nr_cpus ||
	    boot->loader_stack[0] != mmix_boot_handoff.loader_rs)
		mmix_boot_fail("CPU population");
	boot->stack_base = boot->loader_stack[0];
	boot->stack_size = MMIX_BOOT_STACK_SIZE;

	/* All backing remains reserved, including missing or stopped CPUs. */
	fdt_for_each_subnode(node, fdt, reserved) {
		boot_range(fdt, node, &base, &size);
		if (!in_ram(boot, base, size))
			mmix_boot_fail("boot reservation");
		for (i = 0; i < boot->nr_cpus; i++)
			if (node != stack_nodes[i] &&
			    overlaps(base, size, boot->loader_stack[i], MMIX_BOOT_STACK_SIZE))
				mmix_boot_fail("loader stack overlap");
	}
	if (node != -FDT_ERR_NOTFOUND)
		mmix_boot_fail("boot reservation structure");
	reservations = fdt_num_mem_rsv(fdt);
	if (reservations < 0)
		mmix_boot_fail("FDT reservation map");
	for (i = 0; i < reservations; i++) {
		uint64_t start, length;
		unsigned int id;

		if (fdt_get_mem_rsv(fdt, i, &start, &length) || !length ||
		    start + length < start || !in_ram(boot, start, length))
			mmix_boot_fail("FDT reservation map");
		for (id = 0; id < boot->nr_cpus; id++)
			if (overlaps(start, length, boot->loader_stack[id], MMIX_BOOT_STACK_SIZE))
				mmix_boot_fail("loader reservation overlap");
	}
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
	int node, chosen, length, depth = 0;

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
	boot_cpus(fdt, &boot);
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
