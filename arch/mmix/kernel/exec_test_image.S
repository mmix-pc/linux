/* SPDX-License-Identifier: GPL-2.0-only */
.section .rodata,"a"
.balign 8
.globl mmix_exec_test_image
mmix_exec_test_image:
.incbin "arch/mmix/kernel/exec_test_user.elf"
.globl mmix_exec_test_end
mmix_exec_test_end:
.balign 4
.globl mmix_exec_test_launch
mmix_exec_test_launch:
	TRAP 1,0,0
	TRAP 1,0,0
	JMP .
.globl mmix_exec_test_launch_end
mmix_exec_test_launch_end:
