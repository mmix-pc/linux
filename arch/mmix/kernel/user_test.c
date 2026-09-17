// SPDX-License-Identifier: GPL-2.0-only
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/seq_file.h>
#include <asm/rstack.h>
#include "signal.h"
#include "process.h"
#include "user_rstack.h"

static int rstack_stats_show(struct seq_file *seq, void *unused)
{
	seq_printf(seq, "user_states_live %ld\n", mmix_user_rstack_live());
	seq_printf(seq, "kernel_stacks_allocated %lu\n", mmix_rstack_allocated());
	seq_printf(seq, "kernel_stacks_released %lu\n", mmix_rstack_released());
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(rstack_stats);

static int signal_stats_show(struct seq_file *seq, void *unused)
{
	seq_printf(seq, "domains_live %ld\n", mmix_rstack_domains_live());
	seq_printf(seq, "activations_live %ld\n", mmix_signal_live());
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(signal_stats);

static int __init user_test_init(void)
{
	struct dentry *dir, *file;

	dir = debugfs_create_dir("mmix", NULL);
	if (IS_ERR(dir))
		return PTR_ERR(dir);
	file = debugfs_create_file("rstack_stats", 0400, dir, NULL, &rstack_stats_fops);
	if (IS_ERR(file)) {
		debugfs_remove(dir);
		return PTR_ERR(file);
	}
	file = debugfs_create_file("signal_stats", 0400, dir, NULL, &signal_stats_fops);
	if (IS_ERR(file)) {
		debugfs_remove(dir);
		return PTR_ERR(file);
	}
	return 0;
}
late_initcall(user_test_init);
