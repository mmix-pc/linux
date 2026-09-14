// SPDX-License-Identifier: GPL-2.0-only
#include <linux/seq_file.h>

static void *cpuinfo_start(struct seq_file *m, loff_t *pos)
{
	return *pos == 0 ? SEQ_START_TOKEN : NULL;
}

static void *cpuinfo_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void cpuinfo_stop(struct seq_file *m, void *v)
{
}

static int cpuinfo_show(struct seq_file *m, void *v)
{
	seq_puts(m, "processor\t: 0\narchitecture\t: MMIX\n");
	return 0;
}

const struct seq_operations cpuinfo_op = {
	.start = cpuinfo_start,
	.next = cpuinfo_next,
	.stop = cpuinfo_stop,
	.show = cpuinfo_show,
};
