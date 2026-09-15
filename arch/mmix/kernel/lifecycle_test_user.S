/* SPDX-License-Identifier: GPL-2.0-only */
#include <asm/unistd.h>
.text
.balign 4
.globl _start
_start:
	SETL r231,37
	SETL r237,__NR_exit_group
	TRAP 1,0,0
	JMP .
