/*
 * linux-compat.h
 *
 *  Created on: Dec 26, 2013
 *      Author: yonch
 */

#ifndef LINUX_COMPAT_H_
#define LINUX_COMPAT_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>

#ifdef _RTE_IP_H_
#define ntohs(x) rte_be_to_cpu_16(x)
#else
#include <arpa/inet.h>
#endif

#define CONFIG_64BIT

typedef _Bool			bool;

static inline void panic(void) {
	exit(-1);
}

/** from linux's include/asm-generic/bug.h */
#define BUG() do { \
	printf("BUG: failure at %s:%d/%s()!\n", __FILE__, __LINE__, __func__); \
	panic(); \
} while (0)

#define BUG_ON(condition) do { if (unlikely(condition)) BUG(); } while(0)

/* from kernel.h */
#define DIV_ROUND_UP(n,d) (((n) + (d) - 1) / (d))

/* this is a way to compute this without platform-dependent code */
#define BITS_PER_LONG 		(BITS_PER_BYTE * sizeof(long))

/* from bitops.h */
#define BIT_MASK(nr)		(1UL << ((nr) % BITS_PER_LONG))
#define BIT_WORD(nr)		((nr) / BITS_PER_LONG)
#define BITS_PER_BYTE		8
#define BITS_TO_LONGS(nr)	DIV_ROUND_UP(nr, BITS_PER_BYTE * sizeof(long))

/* include/asm-generic/bitops/non-atomic.h */
static inline void __set_bit(int nr, unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p  |= mask;
}

static inline void __clear_bit(int nr, unsigned long *addr)
{
	unsigned long mask = BIT_MASK(nr);
	unsigned long *p = ((unsigned long *)addr) + BIT_WORD(nr);

	*p &= ~mask;
}

static inline int test_bit(int nr, const volatile unsigned long *addr)
{
	return 1UL & (addr[BIT_WORD(nr)] >> (nr & (BITS_PER_LONG-1)));
}

typedef unsigned long long u64;
typedef unsigned long long __u64;
typedef long long s64;
typedef long long __s64;
typedef unsigned int u32;
typedef int s32;
typedef unsigned short u16;
typedef unsigned char u8;

enum {
	false	= 0,
	true	= 1
};

#ifndef unlikely
#define unlikely
#endif

#ifndef likely
#define likely
#endif

/* typecheck.h */
#define typecheck(type,x) \
({	type __dummy; \
	typeof(x) __dummy2; \
	(void)(&__dummy == &__dummy2); \
	1; \
})

/* jiffies.h */
#define time_after64(a,b)	\
	(typecheck(__u64, a) &&	\
	 typecheck(__u64, b) && \
	 ((__s64)((b) - (a)) < 0))
#define time_before64(a,b)	time_after64(b,a)

#define time_after_eq64(a,b)	\
	(typecheck(__u64, a) && \
	 typecheck(__u64, b) && \
	 ((__s64)((a) - (b)) >= 0))
#define time_before_eq64(a,b)	time_after_eq64(b,a)

#define time_in_range64(a, b, c) \
	(time_after_eq64(a, b) && \
	 time_before_eq64(a, c))

/* need __fls */
#define __fls(x) (BITS_PER_LONG - 1 - __builtin_clzl(x))
#define __ffs(x) (__builtin_ffsl(x) - 1)

/* from Jenkin's public domain lookup3.c at http://burtleburtle.net/bob/c/lookup3.c */
#define rot(x,k) (((x)<<(k)) | ((x)>>(32-(k))))
#define final(a,b,c) \
{ \
  c ^= b; c -= rot(b,14); \
  a ^= c; a -= rot(c,11); \
  b ^= a; b -= rot(a,25); \
  c ^= b; c -= rot(b,16); \
  a ^= c; a -= rot(c,4);  \
  b ^= a; b -= rot(a,14); \
  c ^= b; c -= rot(b,24); \
}


static inline u32 jhash_3words(u32 a, u32 b, u32 c, u32 initval)
{
	a += 0xDEADBEEF;
	b += 0xDEADBEEF;
	c += initval;
	final(a,b,c);
	return c;
}

static inline u32 jhash_1word(u32 a, u32 initval)
{
	return jhash_3words(a, 0, 0, initval);
}

/* based on rte_hash_crc from DPDK's rte_hash_crc.h, but does checksum */
static inline
uint32_t csum_partial(const void *data, uint32_t data_len, uint32_t init_val)
{
	unsigned i;
	uint32_t temp = 0;
	u64 sum = init_val;
	const uint32_t *p32 = (const uint32_t *)data;

	for (i = 0; i < data_len / 4; i++) {
		sum += *p32++;
	}

	switch (3 - (data_len & 0x03)) {
	case 0:
		temp |= *((const uint8_t *)p32 + 2) << 16;
		/* Fallthrough */
	case 1:
		temp |= *((const uint8_t *)p32 + 1) << 8;
		/* Fallthrough */
	case 2:
		temp |= *((const uint8_t *)p32);
		sum += temp;
	default:
		break;
	}

	sum = (u32)sum + (sum >> 32); /* could have overflow on bit 32 */
	return (u32)sum + (u32)(sum >> 32);    /* add the overflow */
}

static inline uint16_t csum_tcpudp_magic(uint32_t saddr, uint32_t daddr,
		uint16_t len, uint16_t proto, uint32_t sum32)
{
	uint64_t sum64 = sum32;
	sum64 += saddr + daddr + ((len + proto) << 8);

	/* now fold */
	sum64 = (u32)sum64 + (sum64 >> 32); /* could have overflow on bit 32 */
	sum32 = sum64 + (sum64 >> 32);    /* add the overflow */
	sum32 = (u16)sum32 + (sum32 >> 16); /* could have overflow on bit 16 */
	return ~((u16)sum32 + (u16)(sum32 >> 16)); /* add the overflow */
}

#endif /* LINUX_COMPAT_H_ */