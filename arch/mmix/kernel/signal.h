/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _MMIX_KERNEL_SIGNAL_H
#define _MMIX_KERNEL_SIGNAL_H

#include <linux/types.h>

struct mmix_user_entry;
struct task_struct;

int mmix_signal_pending(struct mmix_user_entry *entry, bool materialized);
int mmix_signal_jump(struct mmix_user_entry *entry, unsigned long address);
int mmix_signal_return(struct mmix_user_entry *entry);
void mmix_signal_free(struct task_struct *task);
int mmix_signal_dup(struct task_struct *task);

#if defined(CONFIG_MMIX_BOOT_TEST) || defined(CONFIG_MMIX_USER_TEST)
long mmix_signal_live(void);
#endif

#endif
