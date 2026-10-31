/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_MMIX_SPAWN_H
#define _UAPI_ASM_MMIX_SPAWN_H

#define MMIX_SPAWN_QUERY 0
#define MMIX_SPAWN_CREATE 1
#define MMIX_SPAWN_VERSION 1
/* Complete v1 contract, including the pending producer creation restriction. */
#define MMIX_SPAWN_CAP_V1 1

#ifndef __ASSEMBLY__
#include <linux/types.h>

struct mmix_spawn_args {
	__u32 version;
	__u32 reserved;
	__aligned_u64 event_fd;
	__aligned_u64 pidfd;
	__aligned_u64 reserved2;
};
#endif

#define MMIX_SPAWN_PENDING 0
#define MMIX_SPAWN_RELEASED 1
/* mmix_spawn(operation, event_fd, 0, 0, 0, 0); scalar result, no buffer. */
#define MMIX_SPAWN_EVENT_QUERY 2
#define MMIX_SPAWN_EVENT_WAIT 3

#endif /* _UAPI_ASM_MMIX_SPAWN_H */
