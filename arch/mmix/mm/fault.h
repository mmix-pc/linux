/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_MM_FAULT_H
#define _MMIX_MM_FAULT_H

#include <asm/ptrace.h>

/* Zero means resolved; a positive signal number denotes an unhandled fault. */
int mmix_handle_page_fault(struct pt_regs *regs);
int mmix_refresh_translation(struct pt_regs *regs);

#endif
