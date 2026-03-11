
#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/printk.h> 

#include <asm/cpufeature.h>
#include <asm/sysreg.h>

#include <linux/bitfield.h>
#include <linux/bitops.h>
#include <linux/bug.h>
#include <linux/capability.h>
#include <linux/cpuhotplug.h>
#include <linux/cpumask.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/errno.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/perf_event.h>
#include <linux/perf/arm_pmu.h>
#include <linux/platform_device.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/smp.h>
#include <asm/unaligned.h>
#include <linux/vmalloc.h>

#include <asm/barrier.h>
#include <asm/cpufeature.h>
#include <asm/mmu.h>
#include <asm/sysreg.h>

#define BIT_GET(_fld, _reg) (_reg & BIT(_fld ## _SHIFT))
#define BIT_SET(_fld) (_reg & BIT(_fld ## _SHIFT))
#define FIELD_GET_LOCAL(_fld, _reg) (_reg >> _fld ## _SHIFT & _fld ## _MASK)
#define FIELD_SET_LOCAL(_fld, _reg) ((_reg & _fld ## _MASK) << _fld ## _SHIFT )
//assert(typeof(var1)==typeof(var2))
#define SWAP(var1, var2) 	\
do {				\
	typeof(var1) temp;	\
	temp = var1;		\
	var1 = var2;		\
	var2 = temp;		\
}while(0);
//#define MY_READ_SYSREG(reg) 

//TODO: this should be a module parameter
#define BUFFER_SIZE (1 << 21)
#define SPE_BW_PERIOD 4096
#define WATERMARK_NUM 1
#define WATERMARK_DEN 2

#define OTHER_BUFFER(active_one) ((active_one + 1) % 2)
/**************************************************************************
 * Public Types
 **************************************************************************/

struct spe_buffer {
	void *buf;
	void *limit;
	void *watermark;
	char ready;
};

struct core_info{
	struct spe_buffer primary;
	struct spe_buffer secondary;
	unsigned int active_buffer;
	void *buffer_base;
	/*
	 * TODO: [IDEA] pass only core_info and have it pointing to 
	 * global spe_ctrl
	 * struct spe_ctrl *spe_ctrl;
	*/
};

struct spe_ctrl {
	size_t size;
	// void *buf;
	// void *base;
	// void* limit;
	// void* water_mark;

	// void *secondary_buf, *secondary_limit,*secondary_watermark;
	// unsigned int active_buffer;
	//interrupts = <0x01 0x05 0x04>;
	//		<Type(PPI) Number TriggerType(active high level sensitive)>
	int irq;

	bool filter_ld;
	bool ts_enable;
	bool pa_enable;
	bool pct_enable;
	bool exclude_user;
	bool exclude_kernel;
	bool cx_enable;
	unsigned int period;
	unsigned int advance;

	cpumask_var_t target_cpu;
	int reader_cpu;

	struct task_struct *reader_task;
	struct platform_device *pdev;
	bool running;
};

enum spe_bw_buf_fault_action {
	SPE_BW_BUF_FAULT_ACT_SPURIOUS,
	SPE_BW_BUF_FAULT_ACT_FATAL,
	SPE_BW_BUF_FAULT_ACT_OK,
};

/**************************************************************************
 * Global Variables
 **************************************************************************/
static struct spe_ctrl spe;
static struct core_info __percpu *core_info;
static unsigned int extended_packets = 0;

static struct kobject *spe_kobj;

unsigned int irq_called = 0;
unsigned int spe_bw_management_called[NR_CPUS];
static char string_buffer[255];
static char string_short_buffer[255];
//struct kobj_attribute etx_attr = __ATTR(etx_value, 0660, sysfs_show, sysfs_store);
/**************************************************************************
 * Local Function Prototypes
 **************************************************************************/

static ssize_t sysfs_store_control(struct kobject *kobj, struct kobj_attribute *attr,
			     const char *buf, size_t count);
static ssize_t sysfs_store_target_cpu(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count);
static ssize_t sysfs_store_reader_cpu(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count);

static struct kobj_attribute control_attr = __ATTR(control, 0664, NULL,
							 sysfs_store_control);
static struct kobj_attribute reader_cpu_attr =  __ATTR(reader_cpu, 0664, NULL, 
							sysfs_store_reader_cpu);
static struct kobj_attribute target_cpu_attr = __ATTR(target_cpu, 0664, NULL, 
							sysfs_store_target_cpu);


/**************************************************************************
 * Module main code
 **************************************************************************/
static void spe_bw_disable_and_drain_local(void)
{
	/* Disable profiling at EL0 and EL1 */
	write_sysreg_s(0, SYS_PMSCR_EL1);
	isb();

	/* Drain any buffered data */
	psb_csync();
	dsb(nsh);

	/* Disable the profiling buffer */
	write_sysreg_s(0, SYS_PMBLIMITR_EL1);
	isb();
}

/* Driver and device probing */
static int spe_bw_irq_probe(struct spe_ctrl *spe_ctrl)
{
	struct platform_device *pdev = spe_ctrl->pdev;
	int irq = platform_get_irq(pdev, 0);

	if (irq < 0)
		return -ENXIO;

	if (!irq_is_percpu(irq)) {
		dev_err(&pdev->dev, "expected PPI but got SPI (%d)\n", irq);
		return -EINVAL;
	}

	spe_ctrl->irq = irq;

	return 0;
}

/* Convert between user ABI and register values */
static u64 spe_bw_fill_pmscr(void)
{
	u64 reg = 0;

	reg |= spe.ts_enable << SYS_PMSCR_EL1_TS_SHIFT;
	reg |= spe.pa_enable << SYS_PMSCR_EL1_PA_SHIFT;
	reg |= spe.pct_enable << SYS_PMSCR_EL1_PCT_SHIFT;
	
	if (!spe.exclude_user)
		reg |= BIT(SYS_PMSCR_EL1_E0SPE_SHIFT);

	if (!spe.exclude_kernel)
		reg |= BIT(SYS_PMSCR_EL1_E1SPE_SHIFT);

	if (spe.cx_enable)
		reg |= BIT(SYS_PMSCR_EL1_CX_SHIFT);

	return reg;
}

static u64 spe_bw_get_record_timestamp(void *payload_addr){
	u64 timestamp;
	if(likely(((u64)payload_addr & (0x7U)) == 0U)){
		timestamp = *(u64 *)payload_addr;
	}else{
		timestamp = get_unaligned((u64 *)payload_addr);
	}
	return timestamp;
}

static void spe_bw_manage_buffer(void *info){
	unsigned int cpu = smp_processor_id();
	struct spe_ctrl *spe_ctrl = (struct spe_ctrl*) info;
	struct core_info *cinfo = this_cpu_ptr(core_info);
	void* secondary_buf,*secondary_limit;
	u64 reg;

	spe_bw_management_called[cpu]++;
	pr_info("Management function triggered on cpu %d", cpu);
	spe_bw_disable_and_drain_local();
	reg = read_sysreg_s(SYS_PMBPTR_EL1);
	
	//Communicate limit
	cinfo->primary.limit = (void*) reg;
	
	reg = read_sysreg_s(SYS_PMBSR_EL1);
	//TODO: Missing check for syndrome
	if(reg != 0){
		pr_warn("spe_bw_manage_buffer(): SYS_PMBSR_EL1: %llx",reg);
	} 

	write_sysreg_s(0, SYS_PMBSR_EL1);

	//Allocate secondary_buffer
	secondary_buf = cinfo->buffer_base
			+ OTHER_BUFFER(cinfo->active_buffer) 
			* spe_ctrl->size;
	memset(secondary_buf,0xff,spe_ctrl->size);
	secondary_limit = secondary_buf + spe_ctrl->size;
	// Ensure memset is completed
	wmb();
	write_sysreg_s(secondary_buf, SYS_PMBPTR_EL1);

	reg = (u64)secondary_limit;
	reg |= BIT(SYS_PMBLIMITR_EL1_E_SHIFT);
	write_sysreg_s(reg, SYS_PMBLIMITR_EL1);
	isb();

	cinfo->secondary.buf = secondary_buf;
	cinfo->secondary.limit = secondary_limit;
	cinfo->secondary.watermark = secondary_buf 
			+ (spe_ctrl->size)
			* WATERMARK_NUM
			/ WATERMARK_DEN;
	wmb();
	cinfo->secondary.ready = 1;
	// Set other flags and start sampling
	reg = spe_bw_fill_pmscr();
	write_sysreg_s(reg, SYS_PMSCR_EL1);
	isb();
	
}

static int process_record(void *base){
	bool end = false;
	void *header_addr = base;
	u8 header;
	unsigned int payload_size = 0;
	unsigned int header_size = 0;
	unsigned int packets_found = 0;
	bool header_match;
	string_buffer[0] = '\0';

	while(header_addr < (base + 64UL) && !end){
		header = READ_ONCE(*(u8 *)header_addr);
		header_size = 1;
		payload_size = 0;
		if(header < 0x20){
			pr_debug("Header-only: %x\n", header);
			if(header == 0x01){
				pr_info("%s",string_buffer);
				pr_info("End packet\n");
				end = true;
			}

		}else if(header < 0x40){
			pr_debug("Extended:");
			header_size = 2;
			header = *(u8 *)(header_addr+1U);
			//TODO: make this readable
			payload_size = 1 << (header >> 4 & 0x3U);
			
			extended_packets++;
		}else{
			payload_size = 1 << (header >> 4 & 0x3U);
			header_match = false;
			//TODO: Make these masks readable
			switch(header | 0x30){
				case 0x71:
				pr_debug("Timestamp addr:%llx",(u64)(header_addr+1));
				pr_debug("TImestamp:%lld",spe_bw_get_record_timestamp(header_addr+1));
				sprintf(string_short_buffer, "Timestamp packet(%d B), end\n", payload_size);
				strncat(string_buffer, string_short_buffer
				, 255 - strlen(string_buffer) - 1);
				pr_debug("%s",string_buffer);
				header_match = true;
				end = true;
				break;
				case 0x72:
				sprintf(string_short_buffer, "Events packet (%d B), ", payload_size);
				strncat(string_buffer, string_short_buffer
				, 255 - strlen(string_buffer) - 1);
				
				header_match = true;
				break;
				case 0x73:
				sprintf(string_short_buffer, "Data src packet (%d B), ", payload_size);
				strncat(string_buffer, string_short_buffer
				, 255 - strlen(string_buffer) - 1);
				
				header_match = true;
				break;
				case 0xFF:
				pr_debug("Uninitialised!\n");
				return -0xFF;
				default:
				// Do nothing
				break;

			}
			switch(header | 0x03){
				case 0x67:
				sprintf(string_short_buffer, "Context packet (%d B), ", payload_size);
				strncat(string_buffer, string_short_buffer
				, 255 - strlen(string_buffer) - 1);
				
				header_match = true;
				break;
				case 0x4B:
				sprintf(string_short_buffer, "OP type packet (%d B), ", payload_size);
				strncat(string_buffer, string_short_buffer
				, 255 - strlen(string_buffer) - 1);
				
				header_match = true;
				break;
				
				case 0xB3:
				case 0xB7:
				sprintf(string_short_buffer, "Address packet (%d B), ", payload_size);
				strncat(string_buffer, string_short_buffer
				, 255 - strlen(string_buffer) - 1);
				
				header_match = true;
				break;
				
				case 0x9B:
				case 0x9F:
				sprintf(string_short_buffer, "Counter packet (%d B), ", payload_size);
				strncat(string_buffer, string_short_buffer
				, 255 - strlen(string_buffer) - 1);
				
				header_match = true;
				break;
				default:
				// Do nothing
				break;
			}
			if(!header_match)
				pr_debug("Unknown packet (%d B)(%x), ", payload_size,header);
		}
		// This counts all non-padding packets
		if(header != 0x00){
			packets_found++;		
		}
		header_addr = header_addr + payload_size + header_size;
	}
	pr_debug("Found %d packets\n", packets_found);
	return 0;
}


static void spe_enable_cpu(void *info)
{
	struct spe_ctrl *s = info;
	u64 reg = 0;
	struct core_info *cinfo = this_cpu_ptr(core_info);
	smp_rmb();
	pr_info("cpu %d : spe_enable",smp_processor_id());
	pr_debug("core_info address %llx",(u64)core_info);
	pr_debug("Buffer at address %llx\n", (u64)READ_ONCE(cinfo->buffer_base));

	enable_percpu_irq(s->irq, IRQ_TYPE_NONE);
	pr_debug("Enabling irq %d on cpu %d", s->irq, smp_processor_id());
	// Enable Filter by type
	if(s->filter_ld)
		reg |= BIT(SYS_PMSFCR_EL1_LD_SHIFT);
	// If at least one filter is active, activate them all
	if (reg)
		reg |= BIT(SYS_PMSFCR_EL1_FT_SHIFT);

	write_sysreg_s(reg, SYS_PMSFCR_EL1);

	// Set Interval counter
	reg = 0;
	//reg |= FIELD_SET_LOCAL(SYS_PMSIRR_EL1_INTERVAL, s->period);
	reg |=  s->period;
	write_sysreg_s(reg, SYS_PMSIRR_EL1);
	
	/* Program SPE buffer registers */
	write_sysreg_s(cinfo->primary.buf, SYS_PMBPTR_EL1);

	reg = (u64)(cinfo->primary.limit);
	reg |= BIT(SYS_PMBLIMITR_EL1_E_SHIFT);
	write_sysreg_s(reg, SYS_PMBLIMITR_EL1);
	isb();
	reg = read_sysreg_s(SYS_PMBLIMITR_EL1);
	pr_debug("SYS_PMBLIMITR_EL1 status: %llx\n",reg);
	
	
	//Clear any interrupt state
	write_sysreg_s(0, SYS_PMBSR_EL1);
	isb();

	// Set other flags and start sampling
	reg = spe_bw_fill_pmscr();
	write_sysreg_s(reg, SYS_PMSCR_EL1);
    	
    	isb();
	pr_debug("SPE engine started on CPU %d\n",smp_processor_id());
}

static void spe_disable_cpu(void *info)
{
    /* Disable SPE */
    u64 reg;
    u64 pmblimitr;
    struct spe_ctrl* spe_ctrl = (struct spe_ctrl*)info;

    disable_percpu_irq(spe_ctrl->irq);

    pmblimitr = read_sysreg_s(SYS_PMBLIMITR_EL1);
    write_sysreg_s(0, SYS_PMBLIMITR_EL1);
    write_sysreg_s(read_sysreg_s(SYS_PMSCR_EL1) & ~1, SYS_PMSCR_EL1);
    isb();
    reg = read_sysreg_s(SYS_PMBSR_EL1);
    pr_debug("PMBSR_EL1 status: %llx\n",reg);
    reg = read_sysreg_s(SYS_PMBPTR_EL1);
    pr_debug("SYS_PMBPTR_EL1 status: %llx\n", reg);
    pr_debug("SYS_PMBLIMITR_EL1 status: %llx\n", reg);
    pr_debug("The IRQ had been called %d times", irq_called);
}


static int spe_reader(void *data)
{
	struct spe_ctrl *s = data;
	int ret;
	unsigned int cpu;
	u64 ones_mask = ~0x0; // This should make an all ones mask
	bool at_least_one_reading = true;

	pr_info("spe_reader(): Starting on CPU %d\n", smp_processor_id());


	while (at_least_one_reading || !kthread_should_stop() ) {
		at_least_one_reading = false;
		
		for_each_cpu(cpu, s->target_cpu){
			struct core_info * cinfo = per_cpu_ptr(core_info,cpu);
			/*
			 * spe_buf::limit can change out of the control of 
			 * current thread's control.
			*/
			void *limit = READ_ONCE(cinfo->primary.limit);
			smp_rmb();
			if (cinfo->primary.buf < limit && 
				READ_ONCE(
					*(u64 *)(cinfo->primary.buf + s->advance)
				) 
				!= ones_mask) 
			{

				pr_debug("SPE record at %llx\n",(u64) cinfo->primary.buf);

				ret = process_record(cinfo->primary.buf);
				if(ret < 0){
					pr_info("Unrecognised packet header: %x", (unsigned int)(-ret));
				} else {
					cinfo->primary.buf += 64UL;

					//With the equal comparison, it will only trigger once
					if (cinfo->primary.buf == cinfo->primary.watermark ){
						pr_debug("spe_reader():watermark hit\n");
						if(0 != smp_call_function_single(cpu,
							spe_bw_manage_buffer,
							s, 0))
						{				
							pr_warn("error in calling " 
								"spe_bw_manage_buffer");
						}	
						
					}
				}
			}
		
			// Restore a waiting buffer, if the secondary is ready
			if(cinfo->primary.buf == limit && 
				READ_ONCE(cinfo->secondary.ready))
			{
				cinfo->active_buffer = OTHER_BUFFER(cinfo->active_buffer);
				SWAP(cinfo->primary.buf,cinfo->secondary.buf);
				SWAP(cinfo->primary.limit,cinfo->secondary.limit);
				SWAP(cinfo->primary.watermark,cinfo->secondary.watermark);
				smp_wmb();
				cinfo->secondary.ready = 0;
				// cinfo->primary
				// s->buf = s->secondary_buf;
				// s->limit = s->secondary_limit;
				// s->water_mark = s->secondary_watermark;
			}
		}
		cpu_relax();
	}
	pr_info("spe_reader(): stopped on CPU %d\n", smp_processor_id());
	return 0;
}

static ssize_t sysfs_store_control(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count)
{	
	// Resetting extended packets counter;
	bool alloc_failed = false;
	unsigned int i;
	extended_packets = 0;
	if (sysfs_streq(buf, "start")) {
		if (spe.running)
			return count;
		
		spe.size = BUFFER_SIZE;
		for_each_cpu(i, spe.target_cpu){
			struct core_info *cinfo = per_cpu_ptr(core_info,i);
			// Allocate 2 buffers of size BUFFER_SIZE per core
			cinfo->buffer_base = kmalloc(spe.size * 2, GFP_KERNEL);
			if(!cinfo->buffer_base)
				alloc_failed = true;
			else
				memset(cinfo->buffer_base, 0xff, spe.size * 2);
			//TODO: This will be refactored to be more concise
			cinfo->primary = (struct spe_buffer){
				.buf = cinfo->buffer_base,
				.limit = cinfo->buffer_base + spe.size,
				.watermark = cinfo->buffer_base + 
					(
						spe.size
						*WATERMARK_DEN
						/WATERMARK_DEN
					),
				.ready = 0
			};
			cinfo->secondary = (struct spe_buffer){
				.buf = cinfo->buffer_base + spe.size,
				.limit = cinfo->buffer_base + 2 * spe.size,
				.watermark = cinfo->buffer_base 
					+ spe.size
					+(
						spe.size
						*WATERMARK_DEN
						/WATERMARK_DEN
					),
				.ready = 0
			};

			cinfo->active_buffer = 0;
		}
		if(alloc_failed){
			for_each_cpu(i, spe.target_cpu){
				struct core_info *cinfo = per_cpu_ptr(core_info,i);
				kfree(cinfo-> buffer_base);
				cinfo-> buffer_base = NULL;
			}
			return -ENOMEM;
		}
		
		smp_wmb();
		memset(spe_bw_management_called,0,sizeof(spe_bw_management_called));
		/* Start reader thread pinned */
		spe.reader_task =
			kthread_create(spe_reader, &spe, "spe_reader");
		if(!spe.reader_task){
			return -ENOMEM;
		}

		kthread_bind(spe.reader_task, spe.reader_cpu);

		wake_up_process(spe.reader_task);

		/* Enable SPE on target CPU */
		// TODO: remove wait true and change with semaphore
		for_each_cpu(i, spe.target_cpu){
			smp_call_function_single(i,
					spe_enable_cpu,
					&spe, 1);
		}
		
		spe.running = true;
		pr_info("SPE started\n");
	}

	else if (sysfs_streq(buf, "stop")) {

		if (!spe.running)
			return count;

		for_each_cpu(i, spe.target_cpu){
			smp_call_function_single(i, spe_disable_cpu,
					&spe, 1);
		}

		if (spe.reader_task)
			kthread_stop(spe.reader_task);

		for_each_cpu(i, spe.target_cpu){
			struct core_info *cinfo = per_cpu_ptr(core_info,i);
			kfree(cinfo-> buffer_base);
			cinfo-> buffer_base = NULL;
		}

		spe.running = false;

		pr_info("sysfs_store_control: SPE stopped\n");
		pr_debug("sysfs_store_control: Found %d extended packets\n",extended_packets);	
	}

	return count;
}

static ssize_t sysfs_store_target_cpu(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
	int ret;
	if((ret = cpumask_parse(buf, spe.target_cpu)) < 0)
		return ret;
	return count;
}

static ssize_t sysfs_store_reader_cpu(struct kobject *kobj,
			     struct kobj_attribute *attr,
			     const char *buf, size_t count)
{
    if(kstrtoint(buf, 10, &spe.reader_cpu) < 0)
	return 0;
    return count;
}

static int spe_bw_init_debugfs(void){
	//TODO: to add some files here
	return 0;
}

/* IRQ handling */
static enum spe_bw_buf_fault_action
spe_bw_buf_fault_act_get(struct core_info *cinfo)
{
	const char *err_str;
	u64 pmbsr;
	enum spe_bw_buf_fault_action ret;

	/*
	 * Ensure new profiling data is visible to the CPU and any external
	 * aborts have been resolved.
	 */
	psb_csync();
	dsb(nsh);

	/* Ensure hardware updates to PMBPTR_EL1 are visible */
	isb();

	/* Service required? */
	pmbsr = read_sysreg_s(SYS_PMBSR_EL1);
	pr_info("spe_bw: irq_handler PMBSR_EL1: %8llx", pmbsr);
	if (!(pmbsr & BIT(SYS_PMBSR_EL1_S_SHIFT)))
		return SPE_BW_BUF_FAULT_ACT_SPURIOUS;

	/*
	 * If we've lost data, disable profiling and also set the PARTIAL
	 * flag to indicate that the last record is corrupted.
	 */
	if (pmbsr & BIT(SYS_PMBSR_EL1_DL_SHIFT)){
		//TODO: Manage it, may just cancel align backwards the PMBPTR
	}
	/* Report collisions to userspace so that it can up the period */
	if (pmbsr & BIT(SYS_PMBSR_EL1_COLL_SHIFT)){
		//TODO: We may don't care this
	}

	/* We only expect buffer management events */
	switch (pmbsr & (SYS_PMBSR_EL1_EC_MASK << SYS_PMBSR_EL1_EC_SHIFT)) {
	case SYS_PMBSR_EL1_EC_BUF:
		/* Handled below */
		break;
	case SYS_PMBSR_EL1_EC_FAULT_S1:
	case SYS_PMBSR_EL1_EC_FAULT_S2:
		err_str = "Unexpected buffer fault";
		goto out_err;
	default:
		err_str = "Unknown error code";
		goto out_err;
	}

	/* Buffer management event */
	switch (pmbsr &
		(SYS_PMBSR_EL1_BUF_BSC_MASK << SYS_PMBSR_EL1_BUF_BSC_SHIFT)) {
	case SYS_PMBSR_EL1_BUF_BSC_FULL:
		ret = SPE_BW_BUF_FAULT_ACT_OK;
		goto out_stop;
	default:
		err_str = "Unknown buffer status code";
	}

out_err:
	pr_err_ratelimited("%s on CPU %d [PMBSR=0x%016llx, PMBPTR=0x%016llx, PMBLIMITR=0x%016llx]\n",
			   err_str, smp_processor_id(), pmbsr,
			   read_sysreg_s(SYS_PMBPTR_EL1),
			   read_sysreg_s(SYS_PMBLIMITR_EL1));
	ret = SPE_BW_BUF_FAULT_ACT_FATAL;

out_stop:
	/*
	 * TODO: Fix this as I don't like calling spe_disable_cpu from 2 
	 * different places
	 */
	spe_disable_cpu(&spe);
	if (spe.reader_task)
		kthread_stop(spe.reader_task);

	/*
	 * TODO: We just stopped the thread, so maybe freeing 
	 * already the buffer is too early
	*/
	kfree(cinfo->buffer_base);
	cinfo-> buffer_base = NULL;

	spe.running = false;
	return ret;
}


static irqreturn_t spe_bw_irq_handler(int irq, void *dev)
{
	struct core_info *cinfo = this_cpu_ptr(core_info);
	
	enum spe_bw_buf_fault_action act;
	if (!cinfo->buffer_base)
		return IRQ_NONE;
	
	pr_info("Irq called\n");
	act = spe_bw_buf_fault_act_get(cinfo);
	if (act == SPE_BW_BUF_FAULT_ACT_SPURIOUS)
		return IRQ_NONE;
	irq_called ++;
	switch (act) {
	case SPE_BW_BUF_FAULT_ACT_FATAL:
		/*
		 * If a fatal exception occurred then leaving the profiling
		 * buffer enabled is a recipe waiting to happen. Since
		 * fatal faults don't always imply truncation, make sure
		 * that the profiling buffer is disabled explicitly before
		 * clearing the syndrome register.
		 */
		spe_bw_disable_and_drain_local();
		break;
	case SPE_BW_BUF_FAULT_ACT_OK:
		/*
		 * We handled the fault (the buffer was full), so resume
		 * profiling as long as we didn't detect truncation.
		 * PMBPTR might be misaligned, but we'll burn that bridge
		 * when we get to it.
		 */
		//TODO: We might need to reset in some way the buffer
		// if (!(handle->aux_flags & PERF_AUX_FLAG_TRUNCATED)) {
		// 	arm_spe_perf_aux_output_begin(handle, event);
		// 	isb();
		// }
		break;
	case SPE_BW_BUF_FAULT_ACT_SPURIOUS:
		/* We've seen you before, but GCC has the memory of a sieve. */
		break;
	}

	/* The buffer pointers are now sane, so resume profiling. */
	write_sysreg_s(0, SYS_PMBSR_EL1);
	return IRQ_HANDLED;
}

static int spe_bw_device_probe(struct platform_device *pdev)
{
	int ret;
	struct spe_ctrl *spe_ctrl = &spe;
	//struct device *dev = &pdev->dev;

	pr_info("Hello from probe\n");
	// spe_pmu = devm_kzalloc(dev, sizeof(*spe_pmu), GFP_KERNEL);
	// if (!spe_pmu) {
	// 	dev_err(dev, "failed to allocate spe_pmu\n");
	// 	return -ENOMEM;
	// }

	// spe_pmu->handle = alloc_percpu(typeof(*spe_pmu->handle));
	// if (!spe_pmu->handle)
	// 	return -ENOMEM;

	spe_ctrl->pdev = pdev;
	/*
	 * Only needed if allocating stuff inside probe, as from remove we 
	 * would not have it 
	 */
	platform_set_drvdata(pdev, spe_ctrl);

	ret = spe_bw_irq_probe(spe_ctrl);
	if (ret)
		goto out_free_handle;


	/* Request our PPIs (note that the IRQ is still disabled) */
	ret = request_percpu_irq(spe_ctrl->irq, spe_bw_irq_handler, "spe_bw",
				 spe_ctrl);
	if (ret)
		return ret;

	// ret = arm_spe_pmu_dev_init(spe_pmu);
	// if (ret)
	// 	goto out_free_handle;

	return 0;

//out_teardown_dev:
	// Again, only when will allocate here 
	//arm_spe_pmu_dev_teardown(spe_pmu);
out_free_handle:
	//Again, only when will allocate per cpu
	//free_percpu(spe_ctrl->handle);
	return ret;
}

static int spe_bw_device_remove(struct platform_device *pdev)
{
	struct spe_ctrl *spe_ctrl = platform_get_drvdata(pdev);
	pr_info("Hello from remove\n");
	//arm_spe_pmu_perf_destroy(spe_ctrl);
	//arm_spe_pmu_dev_teardown(spe_ctrl);
	free_percpu_irq(spe_ctrl->irq, spe_ctrl);
	//free_percpu(spe_ctrl->handle);
	return 0;
}

static const struct of_device_id spe_bw_of_match[] = {
	{ .compatible = "arm,statistical-profiling-extension-v1", .data = (void *)1 },
	{ /* Sentinel */ },
};
MODULE_DEVICE_TABLE(of, spe_bw_of_match);

static struct platform_driver spe_bw_driver = {
	.driver	= {
		.name		= "spe_bw",
		.of_match_table	= of_match_ptr(spe_bw_of_match),
		.suppress_bind_attrs = true,
	},
	.probe	= spe_bw_device_probe,
	// No dynamic allocation to free
	.remove	= spe_bw_device_remove, 
};

// Initialization function (called when the module is loaded)
static int __init spe_guard_init(void)
{
	struct spe_ctrl *global = &spe;
	u64 reg;
	u64 reg_after;
	int fld;
	int interval;
	int max_record_sz;
	int counter_sz;
	int ret;
	u16 pmsver;

	fld = cpuid_feature_extract_unsigned_field(read_cpuid(ID_AA64DFR0_EL1),
						ID_AA64DFR0_PMSVER_SHIFT);
	pmsver = (u16)fld;
	pr_info("Probe spe v1.%d\n", pmsver-1);

	//reg = read_sysreg_s(SYS_CPUECTLR_EL1);
	__asm__ volatile ("mrs %0, S3_0_C15_C1_4": "=r"(reg)::);
	pr_debug("CPUECTLR_EL1=%llx/n",reg);

	reg = read_sysreg_s(SYS_CNTFRQ_EL0);
	pr_debug("CNTFRQ_EL0=%llu/n",reg);

	pr_debug("Sleeping 100ms...");
	//reg = read_sysreg_s(cntpct_el0);
	asm volatile("mrs %0, cntpct_el0" : "=r" (reg));
	mdelay(100);
	//reg = read_sysreg_s(SYS_CNTPCT_EL0) - reg;
	asm volatile("mrs %0, cntpct_el0" : "=r" (reg_after));
	pr_debug("Slept for %llu clocks", reg_after - reg);
	
	reg = read_sysreg_s(SYS_PMBIDR_EL1);
	pr_debug("SYS_PMBIDR_EL1=%08llx/n",reg);
	reg = read_sysreg_s(SYS_PMBIDR_EL1);
	if (reg & BIT(SYS_PMBIDR_EL1_P_SHIFT)) {
		pr_warn("profiling buffer owned by higher exception level\n");
	}
	else {
		pr_debug("profiling available\n");
	}

	/* Minimum alignment. If it's out-of-range, then fail the probe */
	fld = reg >> SYS_PMBIDR_EL1_ALIGN_SHIFT & SYS_PMBIDR_EL1_ALIGN_MASK;
	pr_debug("Minimum alignment: %d\n", 1 << fld);

	/* It's now safe to read PMSIDR and figure out what we've got */
	pr_debug("Reading PMSIDR\n");
	reg = read_sysreg_s(SYS_PMSIDR_EL1);
	pr_debug("Entire PMSIDR: 0x%08llx\n", reg);

	if (BIT_GET(SYS_PMSIDR_EL1_FE, reg))
		pr_debug("FE bit set\n");

	// if (BIT_GET(SYS_PMSIDR_EL1_FnE, reg))
	// 	pr_info("FnE bit set\n");

	if (BIT_GET(SYS_PMSIDR_EL1_FT, reg))
		pr_debug("FT bit set\n");

	if (BIT_GET(SYS_PMSIDR_EL1_FL, reg))
		pr_debug("FL bit set\n");

	if (BIT_GET(SYS_PMSIDR_EL1_ARCHINST, reg))
		pr_debug("ARCHINST bit set\n");
	else
			pr_debug("ARCHINST bit not set\n");

	if (BIT_GET(SYS_PMSIDR_EL1_LDS, reg))
		pr_debug("LDS bit set\n");

	if (BIT_GET(SYS_PMSIDR_EL1_ERND, reg))
		pr_debug("ERND bit set\n");
	else
		pr_debug("ERND bit not set\n");

	/* This field has a spaced out encoding, so just use a look-up */
	fld = FIELD_GET(SYS_PMSIDR_EL1_INTERVAL_MASK<<SYS_PMSIDR_EL1_INTERVAL_SHIFT, reg);
	switch (fld) {
	case 0:
		interval = 256;
		break;
	case 2:
		interval = 512;
		break;
	case 3:
		interval = 768;
		break;
	case 4:
		interval = 1024;
		break;
	case 5:
		interval = 1536;
		break;
	case 6:
		interval = 2048;
		break;
	case 7:
		interval = 3072;
		break;
	default:
		fallthrough;
	case 8:
		interval = 4096;
	}
	pr_debug("Interval=%d\n",interval);

	/* Maximum record size. If it's out-of-range, then fail the probe */

	fld = FIELD_GET_LOCAL(SYS_PMSIDR_EL1_MAXSIZE, reg);
	max_record_sz = 1 << fld;
	pr_debug("Max record size: %d\n", max_record_sz);


	fld = FIELD_GET_LOCAL(SYS_PMSIDR_EL1_COUNTSIZE, reg);
	switch (fld) {
	default:
		counter_sz = -1;
		fallthrough;
	case 2:
		counter_sz = 12;
		break;
	case 3:
		counter_sz = 16;
	}
	pr_debug("Countsize is %d\n",counter_sz);

	memset(global, 0, sizeof(struct spe_ctrl));
	zalloc_cpumask_var(&global->target_cpu, GFP_NOWAIT);
		
	global->filter_ld = true;
	global->ts_enable = true;
	global->pa_enable = true;
	global->pct_enable = true;
	global->exclude_user = false;
	global->exclude_kernel = false;
	global->cx_enable = true;
	
	global->period = SPE_BW_PERIOD;
	global->advance = 0;

	core_info = alloc_percpu(struct core_info);

	spe_kobj = kobject_create_and_add("spe_regulator", kernel_kobj);
	if (!spe_kobj){
		ret = -ENOMEM;
		goto failed_kobj;
	}

	if(sysfs_create_file(spe_kobj, &control_attr.attr)<0){
		ret = -ENOMEM;
		goto failed_file_creation;
	}
	if(sysfs_create_file(spe_kobj, &target_cpu_attr.attr)<0){
		ret = -ENOMEM;
		goto failed_file_creation;
	}
	if(sysfs_create_file(spe_kobj, &reader_cpu_attr.attr)<0){
		ret = -ENOMEM;
		goto failed_file_creation;
	}

	spe_bw_init_debugfs();
	
	ret = platform_driver_register(&spe_bw_driver);
	if(ret)
		return ret;
	
	return 0;  // Return 0 means success
	//platform_driver_unregister(&spe_bw_driver);
	failed_file_creation:
	sysfs_remove_file(spe_kobj, &control_attr.attr);
	sysfs_remove_file(spe_kobj, &target_cpu_attr.attr);
	sysfs_remove_file(spe_kobj, &reader_cpu_attr.attr);
	failed_kobj:

	kobject_put(spe_kobj);
	return ret;
}

// Exit function (called when the module is removed)
static void __exit spe_guard_exit(void)
{
	platform_driver_unregister(&spe_bw_driver);
	sysfs_remove_file(spe_kobj, &control_attr.attr);
	sysfs_remove_file(spe_kobj, &target_cpu_attr.attr);
	sysfs_remove_file(spe_kobj, &reader_cpu_attr.attr);
	kobject_put(spe_kobj);
	printk(KERN_INFO "Goodbye, world!\n");
}

// Register the functions
module_init(spe_guard_init);
module_exit(spe_guard_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SPE based Memeguard Implementation");
MODULE_AUTHOR("Alessandro Mandrile");