/*
 * Copyright (c) 2016, ARM Limited and Contributors. All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */

#include <asm.S>

	.globl	semihosting_call

/*
 * long semihosting_call(unsigned long operation void *system_block_address)
 */
FUNC semihosting_call , :
	svc	#0x123456
	bx	lr
END_FUNC semihosting_call
