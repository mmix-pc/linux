// SPDX-License-Identifier: GPL-2.0-only
#include <linux/binfmts.h>
#include <linux/elf.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/sizes.h>
#include <asm/elf.h>
#include <asm/rstack.h>
#include "user_rstack.h"

int mmix_elf_check(const struct elf64_hdr *hdr)
{
	return hdr->e_machine == EM_MMIX && hdr->e_type == ET_EXEC &&
	       hdr->e_version == EV_CURRENT && hdr->e_ehsize == sizeof(*hdr) &&
	       hdr->e_ident[EI_VERSION] == EV_CURRENT && hdr->e_ident[EI_CLASS] == ELFCLASS64 &&
	       hdr->e_ident[EI_DATA] == ELFDATA2MSB &&
	       (hdr->e_ident[EI_OSABI] == ELFOSABI_NONE ||
		hdr->e_ident[EI_OSABI] == ELFOSABI_LINUX) &&
	       !hdr->e_flags && !(hdr->e_entry & 3) && hdr->e_entry >= PAGE_SIZE &&
	       hdr->e_entry < TASK_SIZE;
}

int arch_elf_pt_proc(void *ehdr, void *phdr, struct file *file, bool is_interp,
		     struct arch_elf_state *state)
{
	return 0;
}

int arch_check_elf(void *ehdr, bool has_interp, void *interp_ehdr, struct arch_elf_state *state)
{
	/* Dynamic loading requires its separate entry and runtime contract. */
	return has_interp ? -ENOEXEC : 0;
}

int mmix_setup_exec(struct linux_binprm *bprm, const struct elf64_hdr *hdr)
{
	struct mmix_user_rstack_state *stack;
	struct mmix_user_state *state;
	struct vm_area_struct *vma;
	unsigned long mapped;
	u64 chain;
	int error = -ENOEXEC;

	mmap_read_lock(current->mm);
	vma = find_vma(current->mm, 0);
	if (vma && vma->vm_start < PAGE_SIZE)
		goto unlock;
	vma = find_vma(current->mm, hdr->e_entry);
	if (vma && vma->vm_start <= hdr->e_entry && (vma->vm_flags & VM_EXEC))
		error = 0;
unlock:
	mmap_read_unlock(current->mm);
	if (error)
		return error;
	stack = mmix_user_rstack_alloc();
	if (!stack)
		return -ENOMEM;
	error = mmix_rstack_domain_create(current->mm, 0, &chain, &mapped);
	if (error)
		goto free;
	current->thread.rstack_chain = chain;
	state = mmix_user_rstack_state(stack);
	state->regs.r_o = mapped;
	state->pending.start = mapped;
	current->thread.user_state = stack;
	return 0;
free:
	mmix_user_rstack_free(&stack);
	return error;
}

void start_thread(struct pt_regs *regs, unsigned long pc, unsigned long sp)
{
	struct mmix_user_state *state = mmix_user_rstack_state(current->thread.user_state);
	unsigned long base = state->regs.r_o;

	memset(state, 0, sizeof(*state));
	state->regs.pc = pc;
	state->regs.regs[254] = sp;
	state->regs.r_g = 230;
	state->regs.r_o = base;
	state->pending.start = base;
	memset(regs, 0, sizeof(*regs));
	regs->pc = pc;
	regs->r_ww = pc;
	regs->regs[254] = sp;
	regs->r_g = 230;
	regs->r_o = base;
	regs->r_s = base;
	regs->r_xx = 1UL << 63;
	regs->syscall_nr = -1;
	current->thread.exec_pending = true;
}
