/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_ELF_H
#define _ASM_MMIX_ELF_H

#include <asm/ptrace.h>
#include <asm/page.h>

#define ELF_ARCH EM_MMIX
#define ELF_CLASS ELFCLASS64
#define ELF_DATA ELFDATA2MSB
#define ELF_EXEC_PAGESIZE PAGE_SIZE
#define ELF_HWCAP 0
#define ARCH_DLINFO                                       \
	do {                                              \
		NEW_AUX_ENT(AT_MINSIGSTKSZ, MINSIGSTKSZ); \
	} while (0)
#define ELF_PLATFORM NULL
#define ELF_ET_DYN_BASE (TASK_SIZE / 3 * 2)

#define elf_check_arch(hdr) mmix_elf_check(hdr)
struct elf64_hdr;
struct file;
struct linux_binprm;
struct arch_elf_state {
	unsigned long unused;
};

#define INIT_ARCH_ELF_STATE { 0 }

int mmix_elf_check(const struct elf64_hdr *hdr);
int arch_elf_pt_proc(void *ehdr, void *phdr, struct file *file, bool is_interp,
		     struct arch_elf_state *state);
int arch_check_elf(void *ehdr, bool has_interp, void *interp_ehdr, struct arch_elf_state *state);
#define ARCH_HAS_SETUP_ADDITIONAL_PAGES 1
#define ARCH_SETUP_ADDITIONAL_PAGES(bprm, ex, interp) mmix_setup_exec(bprm, ex)
int mmix_setup_exec(struct linux_binprm *bprm, const struct elf64_hdr *hdr);

typedef unsigned long elf_greg_t;
#define ELF_NGREG (sizeof(struct user_regs_struct) / sizeof(elf_greg_t))
typedef elf_greg_t elf_gregset_t[ELF_NGREG];
/* No independent floating-register bank or NT_PRFPREG payload. */
typedef struct mmix_fpregset elf_fpregset_t;
#endif
