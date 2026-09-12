/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_ELF_H
#define _ASM_MMIX_ELF_H

#include <asm/ptrace.h>
#include <asm/page.h>

#define ELF_ARCH	EM_MMIX
#define ELF_CLASS	ELFCLASS64
#define ELF_DATA	ELFDATA2MSB
#define ELF_EXEC_PAGESIZE PAGE_SIZE
#define elf_check_arch(hdr) ((hdr)->e_machine == EM_MMIX)

typedef unsigned long elf_greg_t;
#define ELF_NGREG (sizeof(struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];
#endif
