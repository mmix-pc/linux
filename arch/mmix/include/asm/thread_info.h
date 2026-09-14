/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_THREAD_INFO_H
#define _ASM_MMIX_THREAD_INFO_H

#include <linux/const.h>

#define THREAD_SIZE_ORDER	2
#define THREAD_SIZE		_UL(32768)
#define MMIX_RSTACK_SIZE		_UL(65536)

#ifndef __ASSEMBLER__
struct thread_info {
	unsigned long flags;
	unsigned long syscall_work;
	int preempt_count;
};
#define INIT_THREAD_INFO(tsk) { .preempt_count = INIT_PREEMPT_COUNT }
#endif

#define TIF_SYSCALL_TRACE	16
#define _TIF_SYSCALL_TRACE	(1UL << TIF_SYSCALL_TRACE)

#include <asm-generic/thread_info_tif.h>
#endif
