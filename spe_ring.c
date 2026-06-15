#include "spe_ring.h"

#include <linux/cpumask.h>
#include <linux/mm.h>
#include <linux/vmalloc.h>

#include <linux/slab.h>

static int spe_ring_alloc(struct spe_ring_dev *ring)
{
	size_t size;
	struct ring_shared *shared_spe_ring;

	size = sizeof(struct ring_header) +
		ring->shared_entries * sizeof(struct sample);
	pr_info("allocatred shared spe_ring\n");
	shared_spe_ring = vmalloc_user(size);

	if (!shared_spe_ring)
		return -ENOMEM;

	memset(shared_spe_ring, 0, size);

	//ring->alloc_size = PAGE_ALIGN(size);
	//ring->capacity = ring_entries;

	shared_spe_ring->hdr.capacity = ring->shared_entries;
	shared_spe_ring->hdr.head = 0;
	shared_spe_ring->hdr.tail = 0;

	spin_lock(&ring->shared_lock);
	ring->shared = shared_spe_ring;
	spin_unlock(&ring->shared_lock);
	return 0;
}

static void spe_ring_free(struct spe_ring_dev *ring)
{
	struct ring_shared *shared_ring;
	spin_lock(&ring->shared_lock);
	shared_ring = ring->shared;
	ring->shared = NULL;
	spin_unlock(&ring->shared_lock);
	pr_err("free shared spe_ring\n");
	if (shared_ring)
		vfree(shared_ring);

}

static int spe_ring_mmap(struct file *file,
			struct vm_area_struct *vma)
{
	unsigned long req_size;
	unsigned long allocated_size;
	int ret;
	struct spe_ring_dev *dev = file->private_data;

	req_size = vma->vm_end - vma->vm_start;
	allocated_size = PAGE_ALIGN( sizeof(struct ring_header) +
				dev->shared_entries *
				sizeof(struct sample));

	if (req_size > allocated_size)
		return -EINVAL;


	ret = remap_vmalloc_range(vma,
				dev->shared,
				0);
	pr_info("spe_ring_mmap: ret=%d\n",ret);
	return ret;
}

static int spe_ring_open(struct inode *inode,
			struct file *file)
{
	struct miscdevice *mdev;
	struct spe_ring_dev *dev;
	int ret;

	mdev = (struct miscdevice *)file->private_data;

	dev = container_of(mdev,
		struct spe_ring_dev,
		miscdev);

	if (atomic_cmpxchg(&dev->users, 0, 1))
		return -EBUSY;

	if (!dev->shared) {
		ret = spe_ring_alloc(dev);
		if (ret) {
			atomic_set(&dev->users, 0);
			return ret;
		}
	}

	file->private_data = dev;

	return 0;
}

static int spe_ring_release(struct inode *inode,
			struct file *file)
{
	struct spe_ring_dev *dev =
	file->private_data;

	spe_ring_free(dev);

	atomic_set(&dev->users, 0);
	return 0;
}

static const struct file_operations spe_ring_fops = {
	.owner		= THIS_MODULE,
	.open		= spe_ring_open,
	.release	= spe_ring_release,
	.mmap		= spe_ring_mmap,
};

int spe_ring_dev_register(struct spe_ring_dev **spe_rings_arr,
			unsigned int ring_entries)
{
	int ret;
	int cpu;
	int registered_misc_dev;
	struct spe_ring_dev *spe_ring_loc_arr;

	spe_ring_loc_arr = kcalloc(num_possible_cpus(),
			sizeof(struct spe_ring_dev),
			GFP_KERNEL);
	if (!spe_ring_loc_arr) {
		ret = -ENOMEM;
		goto clean_exit;
	}

	for_each_possible_cpu(cpu) {

		struct spe_ring_dev *it_dev = &spe_ring_loc_arr[cpu];
		char *name;

		name = kasprintf(GFP_KERNEL,
			"spe_ring%d",
			cpu);
		if (!name) {
			ret = -ENOMEM;
			goto free_strings;
		}
		//TODO : remember to kfree it
		it_dev->cpu = cpu;
		it_dev->shared_entries = ring_entries;
		atomic_set(&it_dev->users, 0);

		it_dev->miscdev.minor = MISC_DYNAMIC_MINOR;
		it_dev->miscdev.name = name;
		it_dev->miscdev.fops = &spe_ring_fops;
		it_dev->miscdev.mode = 0660;

		ret = misc_register(&it_dev->miscdev);
		if (ret) {
			ret = -ret;
			registered_misc_dev = cpu;
			goto deregister_misc_dev;
		}
		spin_lock_init(&it_dev->shared_lock);
	}

	*spe_rings_arr = spe_ring_loc_arr;
	return 0;

deregister_misc_dev:
	for (cpu = 0 ; cpu < registered_misc_dev ; cpu++) {
		misc_deregister(&spe_ring_loc_arr[cpu].miscdev);
	}
free_strings:
	for_each_possible_cpu(cpu) {
		kfree(spe_ring_loc_arr[cpu].miscdev.name);
	}
	kfree(spe_ring_loc_arr);
clean_exit:
	return ret;
}

void spe_ring_dev_deregister(struct spe_ring_dev *spe_rings_arr)
{
	int cpu;
	for_each_possible_cpu(cpu) {
		misc_deregister(&spe_rings_arr[cpu].miscdev);
		kfree(spe_rings_arr[cpu].miscdev.name);
		// Should de-init the spinlock, but maybe is not required
	}
	kfree(spe_rings_arr);
	return;
}

int spe_ring_push_sample(struct spe_ring_dev *ring, struct sample s)
{
	u64 head;
	u64 tail;
	u64 idx;

	if(ring == NULL)
		return -1;
	spin_lock(&ring->shared_lock);
	if(ring->shared == NULL) {
		spin_unlock(&ring->shared_lock);
		return 1;
	}
	head = READ_ONCE(ring->shared->hdr.head);

	for (;;) {
		tail = smp_load_acquire(&ring->shared->hdr.tail);

		if ((head - tail) < ring->shared->hdr.capacity)
			break;
		cpu_relax();
	}

	idx = head % ring->shared->hdr.capacity;

	ring->shared->data[idx] = s;

	smp_store_release(
			&ring->shared->hdr.head,
			head + 1);
	spin_unlock(&ring->shared_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(spe_ring_push_sample);

// If the ring buffer is full, we immediately return failure
int spe_ring_push_sample_try(struct spe_ring_dev *ring, struct sample s)
{
	u64 head;
	u64 tail;
	u64 idx;

	if(ring == NULL)
		return -1;
	spin_lock(&ring->shared_lock);
	if(ring->shared == NULL) {
		spin_unlock(&ring->shared_lock);
		return 1;
	}
	tail = smp_load_acquire(&ring->shared->hdr.tail);
	head = READ_ONCE(ring->shared->hdr.head);
	pr_debug("head %llu e tail %llu",head, tail);
	if ((head - tail) >= ring->shared->hdr.capacity) {
		spin_unlock(&ring->shared_lock);
		return -1;
	}

	idx = head % ring->shared->hdr.capacity;
	pr_debug("idx: %llu",idx);
	ring->shared->data[idx] = s;

	smp_store_release(
			&ring->shared->hdr.head,
			head + 1);
	spin_unlock(&ring->shared_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(spe_ring_push_sample_try);