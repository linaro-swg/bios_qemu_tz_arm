/*
 * Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <platform_config.h>

#include <asm.S>
#include <arm32.h>
#include <arm32_macros.S>

.section .text.boot
FUNC _start , :
	b	reset
	b	.	/* Undef */
	b	.	/* Syscall */
	b	.	/* Prefetch abort */
	b	.	/* Data abort */
	b	.	/* Reserved */
	b	.	/* IRQ */
	b	.	/* FIQ */
END_FUNC _start

/*
 * Binary is linked against BIOS_RAM_START, but starts to execute from
 * address 0. The branching etc before relocation works because the
 * assembly code is only using relative addressing.
 */
LOCAL_FUNC reset , :
	read_sctlr r0
	orr	r0, r0, #SCTLR_A
	write_sctlr r0
	isb

	/* Setup vector */
	adr	r0, _start
	write_vbar r0

	/* Park secondary cores until we're about to enter OP-TEE */
	read_mpidr r0
	/* Calculate CorePos = (ClusterId * 4) + CoreId */
	and	r1, r0, #MPIDR_CPU_MASK
	and	r0, r0, #MPIDR_CLUSTER_MASK
	add	r0, r1, r0, LSR #6
	cmp	r0, #0
	bne	secondary_hold

	/* Relocate bios to RAM */
	mov	r5, pc
	ldr	r0, __location_tag_addr
__location_tag:
	sub	r0, r0, r5
	ldr	r1, =__text_start
	ldr	r2, =__data_end
	sub	r0, r1, r0
	sub	r2, r2, r1
	bl	copy_blob

	/* R5 will state if booting as bios or 2nd boot stage */
	mov_imm	r5, DRAM_START
	cmp	r5, pc
	movlt	r5, #0		/* Boot with secure state */
	movge	r5, #1		/* Boot with non-secure state */

	/* Jump to new location in RAM */
	ldr	ip, =new_loc
	bx	ip
new_loc:

	/* Setup vector again, now to the new location */
	adr	r0, _start
	write_vbar r0

	/* Zero bss */
	ldr	r0, =__bss_start
	ldr	r1, =__bss_end
	sub	r1, r1, r0
	bl	zero_mem

	/* Setup stack */
	ldr	ip, =main_stack_top;
	ldr	sp, [ip]

	push	{r0, r1, r2}
	mov	r0, sp
	mov	r1, r5
	ldr	ip, =main_init
	blx	ip
	pop	{r0, r1, r2}

	cmp	r5, #0
	beq	1f

	/* Boot the secure image with its expected arguments */
	mov	ip, r0	/* entry address */
	mov	r0, r1	/* argument (address of pagable part if != 0) */
	blx	ip

1:
	/*
	 * Setup stack again as we're in non-secure mode now and have
	 * new registers.
	 */
	ldr	ip, =main_stack_top;
	ldr	sp, [ip]

	ldr	ip, =main_init_ns
	bx	ip
END_FUNC reset

LOCAL_FUNC copy_blob , :
	ldrb	r4, [r0], #1
	strb	r4, [r1], #1
	subs	r2, r2, #1
	bne	copy_blob
	bx	lr
END_FUNC copy_blob

LOCAL_FUNC zero_mem , :
	cmp	r1, #0
	bxeq	lr
	mov	r4, #0
	strb	r4, [r0], #1
	sub	r1, r1, #1
	b	zero_mem
END_FUNC zero_mem

/*
 * struct mailbox {
 *	uint32_t entry_addr;		LSB stores the 32bit entry address.
 *	uint32_t reserved;
 *	uint64_t pen[NB_CPU];		Core released if 32b LSB pen[core]!=0.
 * }
 */
LOCAL_FUNC secondary_hold , :
	mov_imm	r2, SECURE_RAM_START
	add	r0, r2, r0, LSL #3
	mov	r1, #0
	str	r1, [r0, #8]
1:
	ldr	r1, [r0, #8]
	cmp	r1, #0
	wfeeq
	beq	1b
	dsb
	ldr	r1, [r2]
	bx	r1
END_FUNC secondary_hold

.align 2
__location_tag_addr:
	.long __location_tag
