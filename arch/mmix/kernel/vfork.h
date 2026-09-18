/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_KERNEL_VFORK_H
#define _MMIX_KERNEL_VFORK_H

struct kernel_clone_args;
struct task_struct;

long mmix_vfork_clone(struct kernel_clone_args *args);
int mmix_vfork_attach(struct task_struct *task);

#ifdef CONFIG_MMIX_BOOT_TEST
void mmix_vfork_fail_after(int step);
void mmix_vfork_fail_restore(void);
#endif
#if defined(CONFIG_MMIX_BOOT_TEST) || defined(CONFIG_MMIX_USER_TEST)
long mmix_vfork_live(void);
#endif

#endif
