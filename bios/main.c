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

#include "platform_config.h"

#include <assert.h>
#include <compiler.h>
#include <drivers/uart.h>
#include <inttypes.h>
#include <libfdt.h>
#include <stdio.h>
#include <string.h>
#include <types_ext.h>
#include <semihosting.h>

#ifndef MAX
#define MAX(a, b) \
	(__extension__({ __typeof__(a) _a = (a); \
			 __typeof__(b) _b = (b); \
			 _a > _b ? _a : _b; }))

#define MIN(a, b) \
	(__extension__({ __typeof__(a) _a = (a); \
			 __typeof__(b) _b = (b); \
			 _a < _b ? _a : _b; }))
#endif


/* Round up the even multiple of size, size has to be a multiple of 2 */
#define ROUNDUP(v, size) (((v) + (size - 1)) & ~(size - 1))

#define PAGE_SIZE	4096

static uint32_t kernel_entry;
static uint32_t dtb_addr;
static uint32_t rootfs_start;
static uint32_t rootfs_end;

extern const uint8_t __text_start;
extern const uint8_t __linker_secure_blob_start;
extern const uint8_t __linker_secure_blob_end;

static uint32_t main_stack[4098]
	__attribute__((section(".bss.prebss.stack"), aligned(8)));

const uint32_t main_stack_top = (uint32_t)main_stack + sizeof(main_stack);

#define CHECK(x) \
	do { \
		if ((x)) \
			check(#x, __FILE__, __LINE__); \
	} while (0)

#ifdef CONSOLE_UART_BASE
static void msg_init(void)
{
	uart_init(CONSOLE_UART_BASE);
}

void __printf(1, 2) msg(const char *fmt, ...)
{
	va_list ap;
	char buf[128];
	char *p;

	va_start(ap, fmt);
	(void)vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	for (p = buf; *p; p++) {
		uart_putc(*p, CONSOLE_UART_BASE);
		if (*p == '\n')
			uart_putc('\r', CONSOLE_UART_BASE);
	}
}
#else
static void msg_init(void)
{
}

void __printf(1, 2) msg(const char *fmt __unused, ...)
{
}
#endif

static void check(const char *expr, const char *file, int line)
{
	msg("Check \"%s\": %s:%d\n", expr, file, line);
	while (true);
}

static const void *unreloc(const void *addr)
{
	return (void *)((uint32_t)addr - (uint32_t)&__text_start);
}

static uint32_t copy_bios_image(const char *name, uint32_t dst,
		const uint8_t *start, const uint8_t *end)
{
	size_t l = (size_t)(end - start);

	msg("Copy image \"%s\" size %#zx, from %p to %p\n",
		name, l, unreloc(start), (void *)dst);

	memcpy((void *)dst, unreloc(start), l);
	return dst + l;
}

static void *open_fdt(uint32_t dst)
{
	int r;
	const void *s;

	s = (void *)dst;
	msg("Using QEMU provided DTB at %p\n", s);

	r = fdt_open_into(s, (void *)dst, DTB_MAX_SIZE);
	CHECK(r < 0);

	return (void *)dst;
}

static uint32_t copy_dtb(uint32_t dst, uint32_t src)
{
	int r;

	msg("Relocating DTB for kernel use at %p\n", (void *)dst);
	r = fdt_open_into((void *)src, (void *)dst, DTB_MAX_SIZE);
	CHECK(r < 0);
	return dst + DTB_MAX_SIZE;
}

static void copy_ns_images(void)
{
	uint32_t dst;
	long r;

	/* 32MiB above beginning of RAM */
	kernel_entry = DRAM_START + 32 * 1024 * 1024;

	/* Copy non-secure image in place */
	r = semihosting_download_file("zImage", 64 * 1024 * 1024, kernel_entry);
	CHECK(r < 0);
	dst = kernel_entry + r;

	dtb_addr = ROUNDUP(dst, PAGE_SIZE) + 96 * 1024 * 1024; /* safe spot */
	dst = copy_dtb(dtb_addr, DTB_START);

	rootfs_start = ROUNDUP(dst, PAGE_SIZE);
	r = semihosting_download_file("rootfs.cpio.gz", 32 * 1024 * 1024,
				      rootfs_start);
	CHECK(r < 0);
	rootfs_end = rootfs_start + r;
}

#define OPTEE_MAGIC		0x4554504f
#define OPTEE_VERSION		1
#define OPTEE_ARCH_ARM32	0
#define OPTEE_ARCH_ARM64	1


struct optee_header {
	uint32_t magic;
	uint8_t version;
	uint8_t arch;
	uint16_t flags;
	uint32_t init_size;
	uint32_t init_load_addr_hi;
	uint32_t init_load_addr_lo;
	uint32_t init_mem_usage;
	uint32_t paged_size;
};


struct sec_entry_arg {
	uint32_t entry;
	uint32_t paged_part;
	uint32_t fdt;
};
/* called from assembly only */
void main_init_sec(struct sec_entry_arg *arg);
void main_init_sec(struct sec_entry_arg *arg)
{
	void *fdt;
	int r;
	const uint8_t *sblob_start = &__linker_secure_blob_start;
	const uint8_t *sblob_end = &__linker_secure_blob_end;
	struct optee_header hdr;
	size_t pg_part_size;
	uint32_t pg_part_dst;

	msg_init();

	/* Find DTB */
	fdt = open_fdt(DTB_START);
	r = fdt_pack(fdt);
	CHECK(r < 0);

	/* Look for a header first */
	CHECK(((intptr_t)sblob_end - (intptr_t)sblob_start) <
		(ssize_t)sizeof(hdr));
	copy_bios_image("secure header", (uint32_t)&hdr, sblob_start,
			sblob_start + sizeof(hdr));

	CHECK(hdr.magic != OPTEE_MAGIC || hdr.version != OPTEE_VERSION);

	msg("found secure header\n");
	sblob_start += sizeof(hdr);
	CHECK(hdr.init_load_addr_hi != 0);

	pg_part_size = sblob_end - sblob_start - hdr.init_size;
	pg_part_dst = (size_t)TZ_RES_MEM_START + TZ_RES_MEM_SIZE - pg_part_size;

	copy_bios_image("secure paged part",
			pg_part_dst, sblob_start + hdr.init_size, sblob_end);

	sblob_end -= pg_part_size;

	arg->paged_part = pg_part_dst;
	arg->entry = hdr.init_load_addr_lo;

	/* Copy secure image in place */
	copy_bios_image("secure blob", hdr.init_load_addr_lo, sblob_start,
			sblob_end);

	copy_ns_images();
	arg->fdt = dtb_addr;

	msg("Initializing secure world\n");
}

static void setprop_cell(void *fdt, const char *node_path,
		const char *property, uint32_t val)
{
	int offs;
	int r;

	offs = fdt_path_offset(fdt, node_path);
	CHECK(offs < 0);

	r = fdt_setprop_cell(fdt, offs, property, val);
	CHECK(r < 0);
}

static void setprop_string(void *fdt, const char *node_path,
		const char *property, const char *string)
{
	int offs;
	int r;

	offs = fdt_path_offset(fdt, node_path);
	CHECK(offs < 0);

	r = fdt_setprop_string(fdt, offs, property, string);
	CHECK(r < 0);
}

typedef void (*kernel_ep_func)(uint32_t a0, uint32_t a1, uint32_t a2);
static void call_kernel(uint32_t entry, uint32_t dtb,
		uint32_t initrd, uint32_t initrd_end)
{
	kernel_ep_func ep = (kernel_ep_func)entry;
	void *fdt = (void *)dtb;
	const char cmdline[] = COMMAND_LINE;
	int r;
	const uint32_t a0 = 0;
	/*MACH_VEXPRESS see linux/arch/arm/tools/mach-types*/
	const uint32_t a1 = 2272;

	r = fdt_open_into(fdt, fdt, DTB_MAX_SIZE);
	CHECK(r < 0);
	setprop_cell(fdt, "/chosen", "linux,initrd-start", initrd);
	setprop_cell(fdt, "/chosen", "linux,initrd-end", initrd_end);
	setprop_string(fdt, "/chosen", "bootargs", cmdline);
	r = fdt_pack(fdt);
	CHECK(r < 0);

	msg("kernel command line: \"%s\"\n", cmdline);
	msg("Entering kernel at 0x%x with r0=0x%x r1=0x%x r2=0x%x\n",
		(uintptr_t)ep, a0, a1, dtb);
	ep(a0, a1, dtb);
}

void main_init_ns(void); /* called from assembly only */
void main_init_ns(void)
{
	call_kernel(kernel_entry, dtb_addr, rootfs_start, rootfs_end);
}
