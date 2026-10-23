/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_MM_FAULT_H
#define _MMIX_MM_FAULT_H

#include <asm/ptrace.h>

struct mmix_fault_info {
	unsigned long address;
	int code;
};

/*
 * Zero means resolved; a positive signal number denotes an unhandled fault.
 * Optional info describes SIGSEGV/SIGBUS; kernel fixup callers may pass NULL.
 */
int mmix_handle_page_fault(struct pt_regs *regs, struct mmix_fault_info *info);
int mmix_refresh_translation(struct pt_regs *regs);

#endif
