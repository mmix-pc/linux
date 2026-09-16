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

obj-$(CONFIG_MMIX_BOOT_TEST) += user_fault_test.o
obj-y += syscall.o signal.o
obj-$(CONFIG_MMIX_BOOT_TEST) += syscall_test.o syscall_test_context.o
obj-y += exec.o
obj-$(CONFIG_MMIX_BOOT_TEST) += exec_test.o exec_test_image.o
ifeq ($(CONFIG_MMIX_BOOT_TEST),y)
targets += exec_test_user.o exec_test_user.elf
quiet_cmd_exec_test = LD      $@
      cmd_exec_test = $(LD) -z max-page-size=8192 -T $(src)/exec_test_user.lds -o $@ $(filter %.o,$^)
$(obj)/exec_test_user.elf: $(obj)/exec_test_user.o $(src)/exec_test_user.lds FORCE
	$(call if_changed,exec_test)
$(obj)/exec_test_image.o: $(obj)/exec_test_user.elf
endif
obj-$(CONFIG_MMIX_BOOT_TEST) += lifecycle_test.o lifecycle_test_context.o
ifeq ($(CONFIG_MMIX_BOOT_TEST),y)
targets += lifecycle_test_user.o lifecycle_test_user.elf
$(obj)/lifecycle_test_user.elf: $(obj)/lifecycle_test_user.o $(src)/exec_test_user.lds FORCE
	$(call if_changed,exec_test)
$(obj)/lifecycle_test_context.o: $(obj)/lifecycle_test_user.elf
endif
obj-$(CONFIG_MMIX_USER_TEST) += user_test.o
obj-$(CONFIG_MMIX_BOOT_TEST) += signal_test.o signal_test_context.o
