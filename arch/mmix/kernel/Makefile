# SPDX-License-Identifier: GPL-2.0-only
extra-y += vmlinux.lds
obj-y += irqflags.o
obj-y += head.o early.o setup.o
obj-y += entry.o traps.o irq.o
obj-y += process.o switch.o
obj-y += time.o
obj-y += delay.o uaccess.o ptrace.o cpuinfo.o
obj-$(CONFIG_MMIX_BOOT_TEST) += boot_test.o boot_test_context.o
obj-y += user_rstack.o
obj-$(CONFIG_MMIX_BOOT_TEST) += user_rstack_test.o user_rstack_test_context.o
obj-y += user_entry.o user_entry_context.o
obj-$(CONFIG_MMIX_BOOT_TEST) += user_entry_test.o user_entry_test_context.o
