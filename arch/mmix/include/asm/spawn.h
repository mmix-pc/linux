/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _ASM_MMIX_SPAWN_H
#define _ASM_MMIX_SPAWN_H

struct task_struct;

long mmix_spawn(unsigned long operation, unsigned long request,
		unsigned long size, unsigned long reserved1,
		unsigned long reserved2, unsigned long reserved3);
void mmix_spawn_attach(struct task_struct *task);
void mmix_spawn_release(struct task_struct *task);
void mmix_spawn_drop(struct task_struct *task);

#ifdef CONFIG_MMIX_USER_TEST
long mmix_spawn_events_live(void);
long mmix_spawn_producers_live(void);
void mmix_spawn_fail_after(int steps);
#endif

#endif /* _ASM_MMIX_SPAWN_H */
