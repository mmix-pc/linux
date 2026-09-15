/* SPDX-License-Identifier: GPL-2.0-only */
ENTRY(_start)
PHDRS { text PT_LOAD FILEHDR PHDRS FLAGS(5); data PT_LOAD FLAGS(6); stack PT_GNU_STACK FLAGS(6); }
SECTIONS {
 . = 0x10000 + SIZEOF_HEADERS;
 .text : { *(.text .text.*) } :text
 .rodata : { *(.rodata .rodata.*) } :text
 . = ALIGN(8192);
 .data : { *(.data .data.*) } :data
 .bss (NOLOAD) : { *(.bss .bss.*) *(COMMON) } :data
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
