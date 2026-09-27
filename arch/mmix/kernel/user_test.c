// SPDX-License-Identifier: GPL-2.0-only
#include <linux/debugfs.h>
#include <linux/err.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <linux/kstrtox.h>
#include <linux/seq_file.h>
#include <asm/rstack.h>
#include "signal.h"
#include "process.h"
#include "user_rstack.h"
#include "user_entry.h"
#include "vfork.h"

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

static int vfork_stats_show(struct seq_file *seq, void *unused)
{
	seq_printf(seq, "sessions_live %ld\n", mmix_vfork_live());
	return 0;
}
DEFINE_SHOW_ATTRIBUTE(vfork_stats);

static int domain_failure;
static int user_state_failure;
static int entry_failure;

static ssize_t fail_after_write(struct file *file, const char __user *buffer,
				size_t count, loff_t *position)
{
	int step, error;

	if (*position)
		return -EINVAL;
	error = kstrtoint_from_user(buffer, count, 10, &step);
	if (error)
		return error;
	if (step < -1 || step > 32)
		return -EINVAL;
	if (file->private_data == &domain_failure)
		mmix_rstack_domain_fail_after(step);
	else if (file->private_data == &user_state_failure)
		mmix_user_rstack_fail_after(step);
	else if (file->private_data == &entry_failure)
		mmix_user_entry_fail_after(step);
	else
		mmix_rstack_fail_after(step);
	*position += count;
	return count;
}

static const struct file_operations fail_after_fops = {
	.open = simple_open,
	.write = fail_after_write,
	.llseek = noop_llseek,
};

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
	file = debugfs_create_file("vfork_stats", 0400, dir, NULL, &vfork_stats_fops);
	if (IS_ERR(file)) {
		debugfs_remove(dir);
		return PTR_ERR(file);
	}
	file = debugfs_create_file("rstack_fail_after", 0200, dir, NULL,
				   &fail_after_fops);
	if (IS_ERR(file)) {
		debugfs_remove(dir);
		return PTR_ERR(file);
	}
	file = debugfs_create_file("domain_fail_after", 0200, dir,
				   &domain_failure, &fail_after_fops);
	if (IS_ERR(file)) {
		debugfs_remove(dir);
		return PTR_ERR(file);
	}
	file = debugfs_create_file("user_state_fail_after", 0200, dir,
				   &user_state_failure, &fail_after_fops);
	if (IS_ERR(file)) {
		debugfs_remove(dir);
		return PTR_ERR(file);
	}
	file = debugfs_create_file("entry_fail_after", 0200, dir,
				   &entry_failure, &fail_after_fops);
	if (IS_ERR(file)) {
		debugfs_remove(dir);
		return PTR_ERR(file);
	}
	return 0;
}
late_initcall(user_test_init);
