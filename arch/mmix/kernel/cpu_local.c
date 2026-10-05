// SPDX-License-Identifier: GPL-2.0-only
#include "cpu_local.h"

struct mmix_cpu_state mmix_cpu_states[MMIX_BOOT_CPU_SLOTS] = {
	{ .id = 0 }, { .id = 1 }, { .id = 2 }, { .id = 3 },
};
