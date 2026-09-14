/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/page.h>
#include <asm/cache.h>
#include <asm/thread_info.h>
#define LOAD_OFFSET PAGE_OFFSET
#include <asm-generic/vmlinux.lds.h>

ENTRY(_start)
PHDRS {
	boot PT_LOAD FLAGS(5);
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}

jiffies = jiffies_64;

SECTIONS {
	.boot (PAGE_OFFSET + 0x10000) : AT(0x10000) {
		__boot_start = .;
		KEEP(*(.boot.text))
		__boot_end = .;
	} :boot
	. = PAGE_OFFSET + 0x100000;
	_stext = .;
	_text = .;
	.head.text : AT(ADDR(.head.text) - LOAD_OFFSET) {
		KEEP(*(.head.text))
	} :text
	.text : AT(ADDR(.text) - LOAD_OFFSET) {
		TEXT_TEXT
		SCHED_TEXT
		LOCK_TEXT
		IRQENTRY_TEXT
		SOFTIRQENTRY_TEXT
		*(.fixup)
	}
	_etext = .;
	RO_DATA(PAGE_SIZE)
	EXCEPTION_TABLE(8)
	. = ALIGN(PAGE_SIZE);
	__init_begin = .;
	INIT_TEXT_SECTION(PAGE_SIZE)
	. = ALIGN(PAGE_SIZE);
	.init.data : AT(ADDR(.init.data) - LOAD_OFFSET) {
		INIT_DATA
		INIT_SETUP(8)
		INIT_CALLS
		CON_INITCALL
		INIT_RAM_FS
	} :data
	. = ALIGN(PAGE_SIZE);
	__init_end = .;
	_sdata = .;
	RW_DATA(L1_CACHE_BYTES, PAGE_SIZE, THREAD_SIZE)
	PERCPU_SECTION(L1_CACHE_BYTES)
	_edata = .;
	BSS_SECTION(PAGE_SIZE, 8, 8)
	. = ALIGN(PAGE_SIZE);
	_end = .;

	ASSERT(_start == __boot_start, "MMIX entry must start the bootstrap")
	ASSERT(__boot_end - LOAD_OFFSET <= 0x100000, "MMIX bootstrap overlaps kernel")
	ASSERT((_stext & (PAGE_SIZE - 1)) == 0, "MMIX kernel is unaligned")
	ASSERT(_end - LOAD_OFFSET <= 0x10000000, "MMIX image exceeds boot RAM profile")
	ASSERT((__bss_stop & 7) == 0, "MMIX BSS must end on an octa boundary")
	STABS_DEBUG
	DWARF_DEBUG
	ELF_DETAILS
	DISCARDS
}
