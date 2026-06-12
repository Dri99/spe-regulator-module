#ifndef __SPE_RING_H
#define __SPE_RING_H

#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>
#include <linux/types.h>


struct sample {
	u64 timestamp_module;
	u64 timestamp_spe;
	u64 pc;
	u64 vaddr;
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
	struct sample data[];
};

struct spe_ring_dev {
	int cpu;
	atomic_t users;
	struct miscdevice miscdev;
	struct ring_shared *shared;
	spinlock_t shared_lock;
	unsigned int shared_entries;
	// TODO: to be removed - no because used in mmap
	// TODO: to be removed
	//unsigned int capacity;
};

int spe_ring_dev_register(struct spe_ring_dev **spe_rings_arr, 
			unsigned int ring_entries);
void spe_ring_dev_deregister(struct spe_ring_dev *spe_rings_arr);

int spe_ring_push_sample(struct spe_ring_dev *ring, struct sample s);
int spe_ring_push_sample_try(struct spe_ring_dev *ring, struct sample s);

#endif //__SPE_RING_H