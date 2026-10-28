#ifndef __SPE_RING_H
#define __SPE_RING_H

#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/types.h>

/*
 * To activate sliding window regulation, enable CONFIG_DELETE_HISTORY and
 * CONFIG_SLIDING_WIN. To enable aging window, enable CONFIG_AGING_WIN and 
 * CONFIG_REG_FULL_HIST.
 */
#define CONFIG_SAMPLE_TS
// #define CONFIG_SAMPLE_VADDR
#define CONFIG_SAMPLE_LAT
#define CONFIG_SAMPLE_FULL_RECORD
// #define CONFIG_REG_FULL_HIST
#define CONFIG_DELETE_HISTORY
#define CONFIG_SLIDING_WIN
// #define CONFIG_AGING_WIN
#define CONFIG_VARIABLE_BIN
#define CONFIG_BINARY_SEARCH
#define CONFIG_TEST_BIN_ASSIGNMENT

struct sample {
#ifdef CONFIG_SAMPLE_TS
	u64 timestamp_module;
	u64 timestamp_spe;
#endif //CONFIG_SAMPLE_TS
#ifdef CONFIG_SAMPLE_VADDR	
	u64 pc;
	u64 vaddr;
#endif //CONFIG_SAMPLE_VADDR
#ifdef CONFIG_SAMPLE_LAT
	u16 issue_lat;
	u16 total_lat;
	u16 xlat_lat;
#endif //CONFIG_SAMPLE_LAT
#if !defined(CONFIG_SAMPLE_TS) 			\
	&&  !defined(CONFIG_SAMPLE_VADDR)	\
	&&  !defined(CONFIG_SAMPLE_LAT)		\
	&&  !defined(CONFIG_SAMPLE_FULL_RECORD)
	u8 pad;
#endif
};

struct ring_elem {
#ifdef CONFIG_SAMPLE_FULL_RECORD
	__uint128_t  record[64 / sizeof(__uint128_t )];
#endif //CONFIG_SAMPLE_FULL_RECORD
#if !defined(CONFIG_SAMPLE_TS) 			\
	&&  !defined(CONFIG_SAMPLE_VADDR)	\
	&&  !defined(CONFIG_SAMPLE_LAT)		\
	&&  !defined(CONFIG_SAMPLE_FULL_RECORD)
	u8 pad;
#endif
};

struct ring_header {
	u64 head;
	u8 pad1[64 - sizeof(uint64_t)];

	u64 tail;
	u8 pad2[64 - sizeof(uint64_t)];

	u64 capacity;
};

struct ring_shared {
	struct ring_header hdr;
	struct ring_elem data[];
};

struct spe_ring_dev {
	int cpu;
	atomic_t users;
	struct miscdevice miscdev;
	struct ring_shared *shared;
	spinlock_t shared_lock;
	unsigned int shared_entries;
};

int spe_ring_dev_register(struct spe_ring_dev **spe_rings_arr,
			unsigned int ring_entries);
void spe_ring_dev_deregister(struct spe_ring_dev *spe_rings_arr);

int spe_ring_push_sample(struct spe_ring_dev *ring, const struct ring_elem *s);
int spe_ring_push_sample_try(struct spe_ring_dev *ring, const struct ring_elem *s);

#endif //__SPE_RING_H