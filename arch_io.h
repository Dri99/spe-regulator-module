/* SPDX-License-Identifier: MIT */
/* Copyright 2013, 2015, 2017 Alexander Zuepke */
/*
 * arch_io.h
 *
 * ARM specific I/O accessors.
 *
 * NOTE: uses a different order than Linux:
 *   out8(port, value);
 *   write32(addr, value);
 *
 * azuepke, 2013-09-11: initial
 * azuepke, 2015-07-13: 64-bit port
 * azuepke, 2015-07-15: split 32-bit and 64-bit version
 * azuepke, 2017-07-17: adapted to mini-OS
 * azuepke, 2017-09-29: imported and merged again (as io.h)
 * azuepke, 2023-09-23: former arm_io.h
 */

#ifndef ARCH_IO_H_
#define ARCH_IO_H_

#include <linux/types.h>

#ifdef __aarch64__
#define W0 "%w0"
#else
#define W0 "%0"
#endif

static inline volatile void *io_ptr(uintptr_t base, size_t offset)
{
	return (volatile void *)(base + offset);
}

static inline u8 read8(const volatile void *addr)
{
	u8 ret;
	__asm__ volatile("ldrb " W0 ", %1" : "=r" (ret)
	                 : "m" (*(const volatile u8 *)addr) : "memory");
	return ret;
}

static inline u16 read16(const volatile void *addr)
{
	u16 ret;
	__asm__ volatile("ldrh " W0 ", %1" : "=r" (ret)
	                 : "m" (*(const volatile u16 *)addr) : "memory");
	return ret;
}

static inline u32 read32(const volatile void *addr)
{
	u32 ret;
	__asm__ volatile("ldr " W0 ", %1" : "=r" (ret)
	                 : "m" (*(const volatile u32 *)addr) : "memory");
	return ret;
}

#ifdef __aarch64__
/* NOTE: LDRD is atomic on ARMv7 with LPAE, but not on older ones */
static inline u64 read64(const volatile void *addr)
{
	u64 ret;
	__asm__ volatile("ldr %0, %1" : "=r" (ret)
	                 : "m" (*(const volatile u64 *)addr) : "memory");
	return ret;
}
#endif

static inline void write8(volatile void *addr, u8 val)
{
	__asm__ volatile ("strb " W0 ", %1" : : "r" (val),
	                  "m" (*(volatile u8 *)addr) : "memory");
}

static inline void write16(volatile void *addr, u16 val)
{
	__asm__ volatile ("strh " W0 ", %1" : : "r" (val),
	                  "m" (*(volatile u16 *)addr) : "memory");
}

static inline void write32(volatile void *addr, u32 val)
{
	__asm__ volatile ("str " W0 ", %1" : : "r" (val),
	                  "m" (*(volatile u32 *)addr) : "memory");
}

#ifdef __aarch64__
static inline void write64(volatile void *addr, u64 val)
{
	__asm__ volatile ("str %0, %1" : : "r" (val),
	                  "m" (*(volatile u64 *)addr) : "memory");
}
#endif

/* I/O write barrier:
 * to be used when ordering writes to memory mapped devices
 * think of "flush write buffer"
 */
static inline void io_write_barrier(void)
{
	__asm__ volatile ("dsb SY" : : : "memory");
}

#endif
