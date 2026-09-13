/* SPDX-License-Identifier: GPL-2.0-only */
#include <generated/asm-offsets.h>

.text
.balign 4
.globl __switch_to
__switch_to:
	/* Scheduler and switch_mm have installed the incoming MM with IRQs off. */
	SAVE r255
	SETL r240,TASK_THREAD+THREAD_SAVE
	STOU r255,r231,r240
	/* The boot task, like a suspended child, returns through POP next time. */
	SETL r240,TASK_THREAD+THREAD_STARTED
	SETL r241,1
	STOU r241,r231,r240
	GETA r255,%geta(mmix_switch_prev)
	STOU r231,r255,0
	SETL r240,TASK_THREAD+THREAD_SAVE
	LDOU r255,r232,r240
	UNSAVE r255
	/* Obtain prev from this switch, never from the incoming saved arguments. */
	GETA r255,%geta(mmix_switch_prev)
	LDOU r231,r255,0
	SETL r255,TASK_THREAD+THREAD_STARTED
	LDOU r255,r230,r255
	BZ r255,1f
	POP 0,0
1:
	SETL r255,TASK_THREAD+THREAD_STARTED
	SETL r232,1
	STOU r232,r230,r255
	GETA r255,%geta(mmix_ret_from_fork)
	GO r255,r255,0

.bss
.balign 8
mmix_switch_prev:
	.space 8
