/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_MMIX_SIGNAL_H
#define _UAPI_ASM_MMIX_SIGNAL_H

#include <asm/bitsperlong.h>

#define SA_RESTORER 0x04000000
#define MINSIGSTKSZ 32768
#define SIGSTKSZ    65536

#include <asm-generic/signal.h>

#endif /* _UAPI_ASM_MMIX_SIGNAL_H */
